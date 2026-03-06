// =============================================================================
// logic_sim.hpp  —  Logic Gate Simulator  v5  (wire connections added)

#pragma once

#include "flux.hpp"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Extra GL procs
// ---------------------------------------------------------------------------
using PFNGLUNIFORM4FPROC_LS = void(APIENTRY *)(GLint, GLfloat, GLfloat, GLfloat, GLfloat);
using PFNGLUNIFORM2FPROC_LS = void(APIENTRY *)(GLint, GLfloat, GLfloat);

namespace ls_gl {
inline PFNGLUNIFORM4FPROC_LS uniform4f = nullptr;
inline PFNGLUNIFORM2FPROC_LS uniform2f = nullptr;
inline void init() {
    if (uniform4f) return;
    HMODULE gl32 = GetModuleHandleA("opengl32.dll");
    auto load = [&](const char *n) -> void * {
        void *p = (void *)wglGetProcAddress(n);
        if (!p || p==(void*)1 || p==(void*)2 || p==(void*)3 || p==(void*)-1)
            p = (void *)GetProcAddress(gl32, n);
        return p;
    };
    uniform4f = (PFNGLUNIFORM4FPROC_LS)load("glUniform4f");
    uniform2f = (PFNGLUNIFORM2FPROC_LS)load("glUniform2f");
    assert(uniform4f && uniform2f);
}
} // namespace ls_gl

// =============================================================================
// §1  DATA MODEL
// =============================================================================

enum class GateType {
    AND=0, OR, NOT, NAND, NOR, XOR, XNOR, INPUT, OUTPUT, COUNT
};
static constexpr int kGTC = (int)GateType::COUNT;
static constexpr int kInPins[kGTC]  = {2,2,1,2,2,2,2,0,1};
static constexpr int kOutPins[kGTC] = {1,1,1,1,1,1,1,1,0};
static const char *kGateName[kGTC]  = {
    "AND","OR","NOT","NAND","NOR","XOR","XNOR","INPUT","OUTPUT"};

static constexpr float kGW     = 88.f;
static constexpr float kGH     = 60.f;
static constexpr float kPinR   = 5.f;
static constexpr float kPinLen = 16.f;
static constexpr float kCorner = 8.f;

struct Pin { float cx=0, cy=0; bool connected=false; };

struct Gate {
    int id=0;
    GateType type=GateType::AND;
    float x=0, y=0;
    bool selected=false;
    bool inputVal=false;
    std::vector<Pin> inPins, outPins;

    void recomputePins() {
        int ni=kInPins[(int)type], no=kOutPins[(int)type];
        inPins.resize(ni); outPins.resize(no);
        float L=x-kGW*.5f, R=x+kGW*.5f, B=y-kGH*.5f;
        for (int i=0;i<ni;i++) {
            float t=(ni==1)?.5f:float(i)/(ni-1);
            inPins[i]={L-kPinLen, B+t*kGH};
        }
        for (int i=0;i<no;i++) {
            float t=(no==1)?.5f:float(i)/(no-1);
            outPins[i]={R+kPinLen, B+t*kGH};
        }
    }
};

// ── Wire types ───────────────────────────────────────────────────────────────

struct WireEndpoint {
    int  gateId  = -1;
    int  pinIdx  = 0;
    bool isOutput= false;
};

struct Wire {
    int          id       = 0;
    WireEndpoint src;       // output pin side
    WireEndpoint dst;       // input  pin side
    bool         value    = false;
    bool         selected = false;
};

struct PinRef {
    int   gateId;
    int   pinIdx;
    bool  isOutput;
    float cx, cy;   // world-space coords
};

// ── Circuit ──────────────────────────────────────────────────────────────────

struct Circuit {
    std::vector<Gate> gates;
    std::vector<Wire> wires;
    int nextId     = 1;
    int nextWireId = 1;

    // Gate helpers
    int add(GateType t, float x, float y) {
        Gate g; g.id=nextId++; g.type=t; g.x=x; g.y=y;
        g.recomputePins(); gates.push_back(g); return g.id;
    }
    Gate *find(int id) {
        for (auto &g:gates) if (g.id==id) return &g; return nullptr;
    }
    const Gate *find(int id) const {
        for (auto &g:gates) if (g.id==id) return &g; return nullptr;
    }
    void remove(int id) {
        gates.erase(std::remove_if(gates.begin(),gates.end(),
            [id](const Gate &g){return g.id==id;}),gates.end());
    }

    // Wire helpers
    int connect(int srcGateId, int srcPin, int dstGateId, int dstPin) {
        Wire w;
        w.id  = nextWireId++;
        w.src = {srcGateId, srcPin, true };
        w.dst = {dstGateId, dstPin, false};
        wires.push_back(w);
        markPin(srcGateId, srcPin, true,  true);
        markPin(dstGateId, dstPin, false, true);
        return w.id;
    }

    void disconnect(int wireId) {
        for (int i=int(wires.size())-1;i>=0;--i) {
            if (wires[i].id!=wireId) continue;
            auto &w=wires[i];
            recomputePin(w.src.gateId, w.src.pinIdx, true,  wireId);
            recomputePin(w.dst.gateId, w.dst.pinIdx, false, wireId);
            wires.erase(wires.begin()+i);
            return;
        }
    }

    void removeGateWires(int gateId) {
        std::vector<int> ids;
        for (auto &w:wires)
            if (w.src.gateId==gateId||w.dst.gateId==gateId)
                ids.push_back(w.id);
        for (int id:ids) disconnect(id);
    }

    Wire *findWire(int id) {
        for (auto &w:wires) if (w.id==id) return &w; return nullptr;
    }

    void clearAll() {
        gates.clear(); wires.clear();
        nextId=1; nextWireId=1;
    }

    // Multi-pass combinational evaluation
    void evaluate() {
        for (int pass=0;pass<12;++pass) {
            bool changed=false;
            for (auto &g:gates) {
                bool out=computeGate(g);
                for (auto &w:wires) {
                    if (w.src.gateId!=g.id) continue;
                    if (w.value!=out){w.value=out;changed=true;}
                }
            }
            if (!changed) break;
        }
    }

private:
    void markPin(int gateId, int pinIdx, bool isOut, bool val) {
        Gate *g=find(gateId); if(!g) return;
        if ( isOut && pinIdx<(int)g->outPins.size()) g->outPins[pinIdx].connected=val;
        if (!isOut && pinIdx<(int)g->inPins.size())  g->inPins[pinIdx].connected =val;
    }
    void recomputePin(int gateId, int pinIdx, bool isOut, int skipId) {
        bool still=false;
        for (auto &w:wires) {
            if (w.id==skipId) continue;
            if ( isOut && w.src.gateId==gateId && w.src.pinIdx==pinIdx){still=true;break;}
            if (!isOut && w.dst.gateId==gateId && w.dst.pinIdx==pinIdx){still=true;break;}
        }
        markPin(gateId,pinIdx,isOut,still);
    }
    bool computeGate(const Gate &g) {
        if (g.type==GateType::INPUT) return g.inputVal;
        bool ins[2]={false,false};
        for (auto &w:wires)
            if (w.dst.gateId==g.id && w.dst.pinIdx<2)
                ins[w.dst.pinIdx]=w.value;
        bool a=ins[0], b=ins[1];
        switch(g.type){
            case GateType::AND:  return a&&b;
            case GateType::OR:   return a||b;
            case GateType::NOT:  return !a;
            case GateType::NAND: return !(a&&b);
            case GateType::NOR:  return !(a||b);
            case GateType::XOR:  return a^b;
            case GateType::XNOR: return !(a^b);
            default:             return false;
        }
    }
};

// =============================================================================
// §2  VERTEX HELPERS
// =============================================================================

static void pushQuad(std::vector<float> &v,
                     float x0,float y0,float x1,float y1) {
    v.insert(v.end(),{x0,y0, x1,y0, x1,y1, x1,y1, x0,y1, x0,y0});
}

static void pushLine(std::vector<float> &v,
                     float x0,float y0,float x1,float y1,float hw) {
    float dx=x1-x0,dy=y1-y0,len=sqrtf(dx*dx+dy*dy);
    if (len<0.001f){pushQuad(v,x0-hw,y0-hw,x0+hw,y0+hw);return;}
    float nx=-dy/len*hw, ny=dx/len*hw;
    v.insert(v.end(),{x0+nx,y0+ny, x0-nx,y0-ny, x1-nx,y1-ny,
                      x1-nx,y1-ny, x1+nx,y1+ny, x0+nx,y0+ny});
}

static void pushCircle(std::vector<float> &v,
                       float cx,float cy,float r,int segs=16) {
    for (int i=0;i<segs;i++) {
        float a0=float(i)/segs*6.2831853f, a1=float(i+1)/segs*6.2831853f;
        v.insert(v.end(),{cx,cy,
                          cx+cosf(a0)*r,cy+sinf(a0)*r,
                          cx+cosf(a1)*r,cy+sinf(a1)*r});
    }
}

static void pushRoundRectFilled(std::vector<float> &v,
                                float L,float B,float R2,float T,float cr) {
    cr=min(cr,min((R2-L)*.45f,(T-B)*.45f));
    pushQuad(v,L+cr,B,R2-cr,T);
    pushQuad(v,L,B+cr,L+cr,T-cr);
    pushQuad(v,R2-cr,B+cr,R2,T-cr);
    int segs=8;
    float ccx[4]={R2-cr,L+cr,L+cr,R2-cr}, ccy[4]={T-cr,T-cr,B+cr,B+cr};
    float sa[4]={0,1.5708f,3.1416f,4.7124f};
    for (int c=0;c<4;c++)
        for (int i=0;i<segs;i++) {
            float a0=sa[c]+float(i)/segs*1.5708f, a1=sa[c]+float(i+1)/segs*1.5708f;
            v.insert(v.end(),{ccx[c],ccy[c],
                              ccx[c]+cosf(a0)*cr,ccy[c]+sinf(a0)*cr,
                              ccx[c]+cosf(a1)*cr,ccy[c]+sinf(a1)*cr});
        }
}

static void pushRoundRect(std::vector<float> &v,
                          float L,float B,float R2,float T,float cr,float hw,
                          int cornSegs=6) {
    cr=min(cr,min((R2-L)*.4f,(T-B)*.4f));
    pushLine(v,L+cr,B,R2-cr,B,hw); pushLine(v,R2,B+cr,R2,T-cr,hw);
    pushLine(v,R2-cr,T,L+cr,T,hw); pushLine(v,L,T-cr,L,B+cr,hw);
    float ox[4]={R2-cr,L+cr,L+cr,R2-cr}, oy[4]={T-cr,T-cr,B+cr,B+cr};
    float sa[4]={0,1.5708f,3.1416f,4.7124f};
    for (int c=0;c<4;c++)
        for (int i=0;i<cornSegs;i++) {
            float a0=sa[c]+float(i)/cornSegs*1.5708f,
                  a1=sa[c]+float(i+1)/cornSegs*1.5708f;
            pushLine(v,ox[c]+cosf(a0)*cr,oy[c]+sinf(a0)*cr,
                       ox[c]+cosf(a1)*cr,oy[c]+sinf(a1)*cr,hw);
        }
}

// ── Bezier wire geometry ──────────────────────────────────────────────────────

static void bezierPt(float x0,float y0,float x1,float y1,
                     float x2,float y2,float x3,float y3,
                     float t,float &ox,float &oy) {
    float u=1-t,u2=u*u,u3=u2*u,t2=t*t,t3=t2*t;
    ox=u3*x0+3*u2*t*x1+3*u*t2*x2+t3*x3;
    oy=u3*y0+3*u2*t*y1+3*u*t2*y2+t3*y3;
}

static void pushBezierWire(std::vector<float> &v,
                           float x0,float y0,float x3,float y3,
                           float hw,int segs=24) {
    float spread=fabsf(x3-x0)*0.52f+20.f;
    float x1=x0+spread,y1=y0, x2=x3-spread,y2=y3;
    float px=0,py=0;
    for (int i=0;i<=segs;i++) {
        float t=float(i)/segs,qx,qy;
        bezierPt(x0,y0,x1,y1,x2,y2,x3,y3,t,qx,qy);
        if (i>0) pushLine(v,px,py,qx,qy,hw);
        px=qx; py=qy;
    }
}

static void pushBezierDashes(std::vector<float> &v,
                              float x0,float y0,float x3,float y3,
                              float hw,float dashLen,float gapLen,
                              float phase=0.f,int segs=48) {
    float spread=fabsf(x3-x0)*0.52f+20.f;
    float x1=x0+spread,y1=y0, x2=x3-spread,y2=y3;
    std::vector<std::pair<float,float>> pts(segs+1);
    for (int i=0;i<=segs;i++) {
        float t=float(i)/segs;
        bezierPt(x0,y0,x1,y1,x2,y2,x3,y3,t,pts[i].first,pts[i].second);
    }
    float acc=fmodf(phase,dashLen+gapLen);
    bool  inDash=acc<dashLen;
    float remain=inDash?(dashLen-acc):(dashLen+gapLen-acc);
    for (int i=1;i<(int)pts.size();i++) {
        float ex=pts[i-1].first,ey=pts[i-1].second;
        float nx=pts[i].first, ny=pts[i].second;
        float dx=nx-ex,dy=ny-ey,segLen=sqrtf(dx*dx+dy*dy);
        if (segLen<0.001f) continue;
        float ux=dx/segLen,uy=dy/segLen,walked=0;
        while (walked<segLen) {
            float take=min(remain,segLen-walked);
            if (inDash) {
                float sx=ex+ux*walked,sy=ey+uy*walked;
                float tx=ex+ux*(walked+take),ty=ey+uy*(walked+take);
                pushLine(v,sx,sy,tx,ty,hw);
            }
            walked+=take; remain-=take;
            if (remain<=0.f){inDash=!inDash;remain=inDash?dashLen:gapLen;}
        }
    }
}

static bool wireHitTest(float x0,float y0,float x3,float y3,
                         float px,float py,float thr=8.f,int segs=24) {
    float spread=fabsf(x3-x0)*0.52f+20.f;
    float x1=x0+spread,y1=y0,x2=x3-spread,y2=y3;
    float t2=thr*thr,lx=x0,ly=y0;
    for (int i=1;i<=segs;i++) {
        float t=float(i)/segs,qx,qy;
        bezierPt(x0,y0,x1,y1,x2,y2,x3,y3,t,qx,qy);
        float dx=qx-lx,dy=qy-ly,len2=dx*dx+dy*dy;
        float s=(len2>0)?((px-lx)*dx+(py-ly)*dy)/len2:0;
        s=max(0.f,min(1.f,s));
        float cx2=lx+dx*s-px,cy2=ly+dy*s-py;
        if (cx2*cx2+cy2*cy2<=t2) return true;
        lx=qx; ly=qy;
    }
    return false;
}

static std::vector<PinRef> collectPins(const Circuit &c) {
    std::vector<PinRef> out;
    for (auto &g:c.gates) {
        for (int i=0;i<(int)g.outPins.size();i++)
            out.push_back({g.id,i,true, g.outPins[i].cx,g.outPins[i].cy});
        for (int i=0;i<(int)g.inPins.size();i++)
            out.push_back({g.id,i,false,g.inPins[i].cx,g.inPins[i].cy});
    }
    return out;
}

static bool nearestPin(const std::vector<PinRef> &pins,
                        float wx,float wy,float radius,
                        bool mustOut,bool mustIn,PinRef &out) {
    float best=radius*radius; bool found=false;
    for (auto &p:pins) {
        if (mustOut && !p.isOutput) continue;
        if (mustIn  &&  p.isOutput) continue;
        float dx=p.cx-wx,dy=p.cy-wy,d2=dx*dx+dy*dy;
        if (d2<best){best=d2;out=p;found=true;}
    }
    return found;
}

// =============================================================================
// §3  SHADERS
// =============================================================================

static const char *kGVert = R"GLSL(
#version 330 core
layout(location=0) in vec2 aPos;
uniform vec2 uViewSize;
void main(){
    vec2 ndc = aPos / uViewSize * 2.0 - 1.0;
    ndc.y = -ndc.y;
    gl_Position = vec4(ndc, 0.0, 1.0);
}
)GLSL";

static const char *kGFrag = R"GLSL(
#version 330 core
out vec4 fragColor;
uniform vec4 uColor;
void main(){ fragColor = uColor; }
)GLSL";

// =============================================================================
// §4  GATE COLORS
// =============================================================================

struct GateColors { float body[4], border[4], label[4]; };

static GateColors gateColors(GateType t, bool selected) {
    static const float bodies[kGTC][4] = {
        {0.08f,0.15f,0.30f,1},{0.08f,0.25f,0.15f,1},{0.22f,0.10f,0.28f,1},
        {0.26f,0.08f,0.10f,1},{0.20f,0.08f,0.22f,1},{0.08f,0.20f,0.30f,1},
        {0.12f,0.12f,0.28f,1},{0.08f,0.22f,0.13f,1},{0.24f,0.14f,0.05f,1},
    };
    static const float borders[kGTC][4] = {
        {0.27f,0.47f,0.86f,1},{0.27f,0.75f,0.43f,1},{0.66f,0.27f,0.86f,1},
        {0.86f,0.27f,0.27f,1},{0.78f,0.27f,0.67f,1},{0.27f,0.67f,0.86f,1},
        {0.43f,0.43f,0.86f,1},{0.27f,0.82f,0.43f,1},{0.86f,0.51f,0.16f,1},
    };
    int i=(int)t;
    GateColors c;
    memcpy(c.body,  bodies[i], 16);
    memcpy(c.label, borders[i],16);
    if (selected){
        c.border[0]=0.80f;c.border[1]=0.65f;
        c.border[2]=0.97f;c.border[3]=1.f;
    } else {
        memcpy(c.border,borders[i],16);
    }
    return c;
}

// =============================================================================
// §5  CIRCUIT SURFACE
// =============================================================================

class CircuitSurface : public RenderSurface {
public:
    Circuit circuit;

    std::function<void()>            onCircuitChanged;
    std::function<void(const char*)> onStatusMessage;
    std::function<void()>            onRedrawNeeded;
    std::function<void(float)>       onZoomChanged;

    // ── Camera controls ───────────────────────────────────────────────────
    void resetZoom() { zoom_=1.f; camX_=0.f; camY_=0.f; redraw(); }
    void fitToView() {
        if (circuit.gates.empty()){resetZoom();return;}
        float minX=1e9,minY=1e9,maxX=-1e9,maxY=-1e9;
        for (auto &g:circuit.gates) {
            minX=min(minX,g.x-kGW); maxX=max(maxX,g.x+kGW);
            minY=min(minY,g.y-kGH); maxY=max(maxY,g.y+kGH);
        }
        float ww=maxX-minX,wh=maxY-minY;
        if (ww<1||wh<1){resetZoom();return;}
        float pad=60.f;
        float zx=(viewW_-pad*2)/ww, zy=(viewH_-pad*2)/wh;
        zoom_=std::clamp(min(zx,zy),0.05f,4.f);
        camX_=(minX+maxX)*.5f-viewW_*.5f/zoom_;
        camY_=(minY+maxY)*.5f-viewH_*.5f/zoom_;
        redraw();
    }
    void setZoomLevel(float z) {
        zoom_=std::clamp(z,0.05f,8.f); redraw();
        if (onZoomChanged) onZoomChanged(zoom_);
    }
    float getZoom() const { return zoom_; }

    void dropGate(GateType type) {
        float wx=camX_+viewW_*.5f/zoom_, wy=camY_+viewH_*.5f/zoom_;
        static int n=0; int slot=(n++)%7;
        wx+=float(slot-3)*(kGW+24.f);
        wy+=float(slot%3-1)*(kGH+18.f);
        circuit.add(type,wx,wy);
        if (onCircuitChanged) onCircuitChanged();
        redraw();
    }

    // ── RenderSurface interface ───────────────────────────────────────────
    void initialize(int w, int h) override {
        ls_gl::init();
        viewW_=w; viewH_=h;
        camX_=-w*.5f; camY_=-h*.5f;
        buildShader(); buildVAO();
    }
    void resize(int w, int h) override { viewW_=w; viewH_=h; }

    void update(double dt) override {
        bool anyHigh=false;
        for (auto &w:circuit.wires) if(w.value){anyHigh=true;break;}
        if (anyHigh||dragWire_)
            flowPhase_=fmodf(flowPhase_+float(dt)*55.f,200.f);
    }

    void render(const float*) override {
        glViewport(0,0,viewW_,viewH_);
        glClearColor(0.055f,0.055f,0.08f,1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
        GL.useProgram(prog_);
        ls_gl::uniform2f(uViewSize_,float(viewW_),float(viewH_));
        GL.bindVertexArray(vao_);

        drawGrid();
        drawAllWires();
        drawDragPreview();
        for (auto &g:circuit.gates) drawGate(g);
        drawPinHovers();

        GL.bindVertexArray(0);
        GL.useProgram(0);
        glDisable(GL_BLEND);
    }

    void destroy() override {
        if (prog_){GL.deleteProgram(prog_);       prog_=0;}
        if (vao_) {GL.deleteVertexArrays(1,&vao_);vao_=0;}
        if (vbo_) {GL.deleteBuffers(1,&vbo_);     vbo_=0;}
    }

    bool needsContinuousRedraw() const override {
        if (dragWire_) return true;
        for (auto &w:circuit.wires) if(w.value) return true;
        return false;
    }

    // ── Mouse ─────────────────────────────────────────────────────────────
    void onMouseDown(float sx, float sy) override {
        lastSX_=sx; lastSY_=sy;
        float wx,wy; s2w(sx,sy,wx,wy);

        // 1. Start wire drag from output pin
        {
            auto pins=collectPins(circuit);
            PinRef src{};
            if (nearestPin(pins,wx,wy,16.f/zoom_,true,false,src)) {
                dragWire_=true; dragSrc_=src;
                dragCurWX_=wx; dragCurWY_=wy;
                hasDstSnap_=false;
                for (auto &g:circuit.gates) g.selected=false;
                for (auto &w:circuit.wires)  w.selected=false;
                if (onStatusMessage)
                    onStatusMessage("Drag to an input pin to connect  ·  Esc to cancel");
                redraw(); return;
            }
        }

        // 2. Click to select a wire
        for (auto &w:circuit.wires) w.selected=false;
        for (auto &w:circuit.wires) {
            auto [sx0,sy0,sx1,sy1]=wireScrCoords(w);
            if (wireHitTest(sx0,sy0,sx1,sy1,sx,sy,8.f)) {
                w.selected=true;
                for (auto &g:circuit.gates) g.selected=false;
                if (onStatusMessage)
                    onStatusMessage("Wire selected  ·  Del to remove  ·  Right-click to delete");
                redraw(); return;
            }
        }

        // 3. Select / drag a gate
        for (auto &g:circuit.gates) g.selected=false;
        int hit=hitGate(wx,wy);
        if (hit>=0) {
            Gate *g=circuit.find(hit);
            if (g){
                g->selected=true;
                dragId_=hit; dragOx_=wx-g->x; dragOy_=wy-g->y;
            }
        } else { dragId_=-1; }
        redraw();
    }

    void onMouseMove(float sx, float sy) override {
        float wx,wy; s2w(sx,sy,wx,wy);

        if (mmb_) {
            camX_-=(sx-lastSX_)/zoom_;
            camY_-=(sy-lastSY_)/zoom_;
            lastSX_=sx; lastSY_=sy;
            redraw(); return;
        }
        lastSX_=sx; lastSY_=sy;

        // Wire drag: snap to nearest valid input pin
        if (dragWire_) {
            dragCurWX_=wx; dragCurWY_=wy;
            auto pins=collectPins(circuit);
            PinRef dst{};
            hasDstSnap_=nearestPin(pins,wx,wy,20.f/zoom_,false,true,dst);
            if (hasDstSnap_ && dst.gateId==dragSrc_.gateId) hasDstSnap_=false;
            if (hasDstSnap_) dstSnap_=dst;
            redraw(); return;
        }

        // Hover glow over pins
        {
            auto pins=collectPins(circuit);
            PinRef h{};
            bool found=nearestPin(pins,wx,wy,16.f/zoom_,false,false,h);
            bool changed=(found!=hovPinValid_);
            if (!changed&&found)
                changed=(h.gateId!=hovPin_.gateId||h.pinIdx!=hovPin_.pinIdx
                         ||h.isOutput!=hovPin_.isOutput);
            hovPinValid_=found; if(found) hovPin_=h;
            if (changed) redraw();
        }

        // Gate drag
        if (dragId_>=0) {
            Gate *g=circuit.find(dragId_);
            if (g){g->x=wx-dragOx_; g->y=wy-dragOy_; g->recomputePins(); redraw();}
        }
    }

    void onMouseUp(float sx, float sy) override {
        mmb_=false;

        if (dragWire_) {
            dragWire_=false;
            if (hasDstSnap_) {
                // Reject if input already occupied
                bool occupied=false;
                for (auto &w:circuit.wires)
                    if (w.dst.gateId==dstSnap_.gateId&&w.dst.pinIdx==dstSnap_.pinIdx)
                    {occupied=true;break;}
                if (!occupied) {
                    circuit.connect(dragSrc_.gateId,dragSrc_.pinIdx,
                                    dstSnap_.gateId, dstSnap_.pinIdx);
                    circuit.evaluate();
                    if (onCircuitChanged) onCircuitChanged();
                    if (onStatusMessage)
                        onStatusMessage("Connected  ·  Right-click or double-click wire to remove");
                } else {
                    if (onStatusMessage)
                        onStatusMessage("Input already connected — remove existing wire first");
                }
            }
            hasDstSnap_=false;
            redraw(); return;
        }

        if (dragId_>=0) {
            dragId_=-1;
            if (onCircuitChanged) onCircuitChanged();
        }
    }

    void onRightMouseDown(float sx, float sy) override {
        // Right-click wire → delete immediately
        for (int i=int(circuit.wires.size())-1;i>=0;--i) {
            auto [sx0,sy0,sx1,sy1]=wireScrCoords(circuit.wires[i]);
            if (wireHitTest(sx0,sy0,sx1,sy1,sx,sy,10.f)) {
                circuit.disconnect(circuit.wires[i].id);
                circuit.evaluate();
                if (onCircuitChanged) onCircuitChanged();
                if (onStatusMessage) onStatusMessage("Wire removed");
                redraw(); return;
            }
        }
    }

    // Call this from the app when a WM_LBUTTONDBLCLK arrives on the canvas
    void onDoubleClick(float sx, float sy) {
        float wx,wy; s2w(sx,sy,wx,wy);
        // Double-click wire → delete
        for (int i=int(circuit.wires.size())-1;i>=0;--i) {
            auto [sx0,sy0,sx1,sy1]=wireScrCoords(circuit.wires[i]);
            if (wireHitTest(sx0,sy0,sx1,sy1,sx,sy,10.f)) {
                circuit.disconnect(circuit.wires[i].id);
                circuit.evaluate();
                if (onCircuitChanged) onCircuitChanged();
                if (onStatusMessage) onStatusMessage("Wire removed");
                redraw(); return;
            }
        }
        // Double-click INPUT gate → toggle value
        int hit=hitGate(wx,wy);
        if (hit>=0) {
            Gate *g=circuit.find(hit);
            if (g&&g->type==GateType::INPUT) {
                g->inputVal=!g->inputVal;
                circuit.evaluate();
                if (onCircuitChanged) onCircuitChanged();
                redraw();
            }
        }
    }

    void onMiddleDown(float sx,float sy){mmb_=true;lastSX_=sx;lastSY_=sy;}
    void onMiddleUp()                   {mmb_=false;}

    void onScroll(float cx,float cy,float dy,bool ctrl) {
        if (ctrl) {
            float wx0,wy0; s2w(cx,cy,wx0,wy0);
            float f=(dy>0)?1.12f:1.f/1.12f;
            zoom_=std::clamp(zoom_*f,0.05f,8.f);
            camX_=wx0-cx/zoom_; camY_=wy0-cy/zoom_;
            if (onZoomChanged) onZoomChanged(zoom_);
        } else {
            camY_+=dy*40.f/zoom_;
        }
        redraw();
    }

    void onKeyDown(int key) override {
        if (key==VK_ESCAPE&&dragWire_) {
            dragWire_=false; hasDstSnap_=false; redraw(); return;
        }
        // Space toggles selected INPUT gate
        if (key==VK_SPACE) {
            for (auto &g:circuit.gates)
                if (g.selected&&g.type==GateType::INPUT) {
                    g.inputVal=!g.inputVal;
                    circuit.evaluate();
                    if (onCircuitChanged) onCircuitChanged();
                    redraw();
                }
        }
        if (key==VK_DELETE||key==VK_BACK) {
            // Delete selected wires first
            bool anyWire=false;
            for (int i=int(circuit.wires.size())-1;i>=0;--i)
                if (circuit.wires[i].selected)
                    {circuit.disconnect(circuit.wires[i].id);anyWire=true;}
            if (anyWire) {
                circuit.evaluate();
                if (onCircuitChanged) onCircuitChanged();
                redraw(); return;
            }
            // Then delete selected gates + their wires
            bool anyGate=false;
            for (int i=int(circuit.gates.size())-1;i>=0;--i)
                if (circuit.gates[i].selected) {
                    circuit.removeGateWires(circuit.gates[i].id);
                    circuit.remove(circuit.gates[i].id);
                    anyGate=true;
                }
            if (anyGate) {
                circuit.evaluate();
                if (onCircuitChanged) onCircuitChanged();
                redraw();
            }
        }
    }

private:
    int   viewW_=1, viewH_=1;
    float camX_=0, camY_=0, zoom_=1.f;

    GLuint prog_=0, vao_=0, vbo_=0;
    GLint  uViewSize_=-1, uColor_=-1;

    int   dragId_=-1;
    float dragOx_=0, dragOy_=0;
    float lastSX_=0, lastSY_=0;
    bool  mmb_=false;

    // Wire drag
    bool   dragWire_   = false;
    PinRef dragSrc_    = {};
    float  dragCurWX_  = 0, dragCurWY_=0;
    bool   hasDstSnap_ = false;
    PinRef dstSnap_    = {};

    // Pin hover
    bool   hovPinValid_= false;
    PinRef hovPin_     = {};

    float  flowPhase_  = 0.f;

    void redraw(){if(onRedrawNeeded)onRedrawNeeded();}

    void w2s(float wx,float wy,float &sx,float &sy) const {
        sx=(wx-camX_)*zoom_;
        sy=viewH_-(wy-camY_)*zoom_;
    }
    void s2w(float sx,float sy,float &wx,float &wy) const {
        wx=camX_+sx/zoom_;
        wy=camY_+sy/zoom_;
    }

    std::tuple<float,float,float,float> wireScrCoords(const Wire &w) const {
        float sx0=0,sy0=0,sx1=0,sy1=0;
        if (auto *g=circuit.find(w.src.gateId))
            if (w.src.pinIdx<(int)g->outPins.size())
                w2s(g->outPins[w.src.pinIdx].cx,g->outPins[w.src.pinIdx].cy,sx0,sy0);
        if (auto *g=circuit.find(w.dst.gateId))
            if (w.dst.pinIdx<(int)g->inPins.size())
                w2s(g->inPins[w.dst.pinIdx].cx,g->inPins[w.dst.pinIdx].cy,sx1,sy1);
        return {sx0,sy0,sx1,sy1};
    }

    int hitGate(float wx,float wy) const {
        for (int i=int(circuit.gates.size())-1;i>=0;--i) {
            const Gate &g=circuit.gates[i];
            if (wx>=g.x-kGW*.5f&&wx<=g.x+kGW*.5f&&
                wy>=g.y-kGH*.5f&&wy<=g.y+kGH*.5f) return g.id;
        }
        return -1;
    }

    void buildShader() {
        prog_=glutil::linkProgram(kGVert,kGFrag);
        assert(prog_);
        uViewSize_=GL.getUniformLocation(prog_,"uViewSize");
        uColor_   =GL.getUniformLocation(prog_,"uColor");
    }
    void buildVAO() {
        GL.genVertexArrays(1,&vao_); GL.genBuffers(1,&vbo_);
        GL.bindVertexArray(vao_);
        GL.bindBuffer(GL_ARRAY_BUFFER,vbo_);
        GL.enableVertexAttribArray(0);
        GL.vertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,2*sizeof(float),(void*)0);
        GL.bindVertexArray(0);
    }
    void upload(const std::vector<float> &v) {
        GL.bindBuffer(GL_ARRAY_BUFFER,vbo_);
        GL.bufferData(GL_ARRAY_BUFFER,(GLsizeiptr)(v.size()*sizeof(float)),
                      v.data(),GL_DYNAMIC_DRAW);
    }
    void drawColored(const std::vector<float> &v,
                     float r,float g,float b,float a=1.f) {
        if(v.empty()) return;
        upload(v);
        ls_gl::uniform4f(uColor_,r,g,b,a);
        glDrawArrays(GL_TRIANGLES,0,(GLsizei)(v.size()/2));
    }

    // ── Grid ─────────────────────────────────────────────────────────────
    void drawGrid() {
        float gs=40.f,step=gs*zoom_;
        if(step<4.f) return;
        float vw=float(viewW_),vh=float(viewH_);
        std::vector<float> lines; lines.reserve(2048);
        float startWX=std::floor(camX_/gs)*gs;
        for (float wx=startWX;;wx+=gs) {
            float sx=(wx-camX_)*zoom_; if(sx>vw+step) break;
            pushLine(lines,sx,0,sx,vh,0.5f);
        }
        float startWY=std::floor(camY_/gs)*gs;
        for (float wy=startWY;;wy+=gs) {
            float sy=vh-(wy-camY_)*zoom_; if(sy<-step) break;
            pushLine(lines,0,sy,vw,sy,0.5f);
        }
        drawColored(lines,0.12f,0.13f,0.19f,1.f);
        float ox,oy; w2s(0,0,ox,oy);
        std::vector<float> cross;
        pushLine(cross,ox-10,oy,ox+10,oy,1.f);
        pushLine(cross,ox,oy-10,ox,oy+10,1.f);
        drawColored(cross,0.22f,0.23f,0.36f,1.f);
    }

    // ── All committed wires ───────────────────────────────────────────────
    void drawAllWires() {
        for (auto &w:circuit.wires) {
            auto [sx0,sy0,sx1,sy1]=wireScrCoords(w);
            float hw=max(1.0f,zoom_*1.4f);

            // Selection glow
            if (w.selected) {
                std::vector<float> halo;
                pushBezierWire(halo,sx0,sy0,sx1,sy1,hw+4.f);
                drawColored(halo,0.9f,0.78f,0.2f,0.28f);
            }

            if (w.value) {
                // HIGH: dark green base + animated bright dashes
                {std::vector<float> base;
                 pushBezierWire(base,sx0,sy0,sx1,sy1,hw);
                 drawColored(base,0.08f,0.35f,0.12f,1.f);}
                {std::vector<float> dash;
                 pushBezierDashes(dash,sx0,sy0,sx1,sy1,hw*0.8f,10.f,7.f,flowPhase_);
                 if(w.selected) drawColored(dash,1.f,0.95f,0.3f,1.f);
                 else           drawColored(dash,0.28f,1.f,0.48f,1.f);}
            } else {
                // LOW: dim blue + faint static dots
                {std::vector<float> line;
                 pushBezierWire(line,sx0,sy0,sx1,sy1,hw);
                 if(w.selected) drawColored(line,0.88f,0.75f,0.18f,1.f);
                 else           drawColored(line,0.22f,0.25f,0.42f,1.f);}
                if(zoom_>0.35f) {
                    std::vector<float> dots;
                    pushBezierDashes(dots,sx0,sy0,sx1,sy1,hw*0.45f,3.f,16.f,0.f);
                    if(w.selected) drawColored(dots,1.f,0.9f,0.35f,0.55f);
                    else           drawColored(dots,0.32f,0.36f,0.60f,0.65f);
                }
            }

            // Junction dots at pin ends
            {std::vector<float> ep;
             float dr=max(2.8f,hw*1.5f);
             pushCircle(ep,sx0,sy0,dr); pushCircle(ep,sx1,sy1,dr);
             if(w.value)         drawColored(ep,0.28f,1.f,0.48f,1.f);
             else if(w.selected) drawColored(ep,0.88f,0.75f,0.18f,1.f);
             else                drawColored(ep,0.28f,0.32f,0.55f,1.f);}
        }
    }

    // ── In-progress wire drag preview ─────────────────────────────────────
    void drawDragPreview() {
        if (!dragWire_) return;
        float sx0,sy0;
        {
            auto *g=circuit.find(dragSrc_.gateId);
            if (!g||dragSrc_.pinIdx>=(int)g->outPins.size()) return;
            w2s(g->outPins[dragSrc_.pinIdx].cx,
                g->outPins[dragSrc_.pinIdx].cy,sx0,sy0);
        }
        float sx1,sy1;
        if (hasDstSnap_) w2s(dstSnap_.cx,dstSnap_.cy,sx1,sy1);
        else             w2s(dragCurWX_,dragCurWY_,sx1,sy1);

        float hw=max(1.f,zoom_*1.3f);
        bool  snapped=hasDstSnap_;

        // Outer glow
        {std::vector<float> glow;
         pushBezierWire(glow,sx0,sy0,sx1,sy1,hw+5.f);
         drawColored(glow,snapped?.25f:.50f,snapped?.90f:.45f,snapped?.45f:.95f,0.22f);}
        // Wire body
        {std::vector<float> line;
         pushBezierWire(line,sx0,sy0,sx1,sy1,hw);
         drawColored(line,snapped?.28f:.52f,snapped?1.f:.50f,snapped?.52f:.98f,0.88f);}
        // Animated dashes
        {std::vector<float> dash;
         pushBezierDashes(dash,sx0,sy0,sx1,sy1,hw*0.7f,8.f,6.f,flowPhase_);
         drawColored(dash,snapped?.75f:.72f,snapped?1.f:.68f,snapped?.80f:1.f,0.9f);}
        // Cursor dot
        {std::vector<float> dot;
         pushCircle(dot,sx1,sy1,max(4.f,hw*2.f));
         drawColored(dot,snapped?.28f:.52f,snapped?1.f:.50f,snapped?.52f:.98f,1.f);}
    }

    // ── Pin hover glows ───────────────────────────────────────────────────
    void drawPinHovers() {
        // Snap ring on destination pin while dragging
        if (dragWire_&&hasDstSnap_) {
            float sx,sy; w2s(dstSnap_.cx,dstSnap_.cy,sx,sy);
            std::vector<float> ring;
            pushCircle(ring,sx,sy,max(6.f,kPinR*zoom_*2.f),24);
            drawColored(ring,0.28f,1.f,0.5f,0.45f);
        }
        // General hover glow when idle
        if (!dragWire_&&hovPinValid_) {
            float sx,sy; w2s(hovPin_.cx,hovPin_.cy,sx,sy);
            std::vector<float> ring;
            pushCircle(ring,sx,sy,max(6.f,kPinR*zoom_*2.f),24);
            if (hovPin_.isOutput) drawColored(ring,1.f,0.72f,0.28f,0.40f);
            else                  drawColored(ring,0.4f,0.6f, 1.f,  0.40f);
        }
    }

    // ── Gate drawing ──────────────────────────────────────────────────────
    void drawGate(const Gate &g) {
        float sx,sy; w2s(g.x,g.y,sx,sy);
        float z=zoom_;
        float hw=kGW*z*.5f, hh=kGH*z*.5f;
        float L=sx-hw,R=sx+hw, yT=sy-hh,yB=sy+hh;
        float cr=kCorner*z, bw=max(1.f,z);

        GateColors col=gateColors(g.type,g.selected);

        // Shadow
        {std::vector<float> v; pushRoundRectFilled(v,L+3,yT+3,R+3,yB+3,cr);
         drawColored(v,0,0,0,0.45f);}
        // Body
        {std::vector<float> v; pushRoundRectFilled(v,L,yT,R,yB,cr);
         drawColored(v,col.body[0],col.body[1],col.body[2],col.body[3]);}
        // Border
        {std::vector<float> v; pushRoundRect(v,L,yT,R,yB,cr,bw*.5f+.5f);
         drawColored(v,col.border[0],col.border[1],col.border[2],col.border[3]);}
        // Selection inner ring
        if (g.selected) {
            std::vector<float> v; pushRoundRect(v,L+2,yT+2,R-2,yB-2,cr-2,bw*.5f);
            drawColored(v,col.border[0],col.border[1],col.border[2],0.3f);
        }
        // Top colour band
        {float bh=max(3.f,hh*.22f);
         std::vector<float> v;
         pushQuad(v,L+cr,yT,R-cr,yT+bh);
         pushRoundRectFilled(v,L,yT,R,yT+bh*1.5f,cr);
         drawColored(v,col.border[0]*.4f,col.border[1]*.4f,col.border[2]*.4f,0.8f);}
        // Label bar
        {float lh=max(1.5f,z*1.5f),lw=hw*0.55f;
         std::vector<float> v;
         pushQuad(v,sx-lw,sy-lh*.5f,sx+lw,sy+lh*.5f);
         drawColored(v,col.border[0],col.border[1],col.border[2],0.55f);}

        // Input pin stubs + dots
        for (auto &pin:g.inPins) {
            float psx,psy; w2s(pin.cx,pin.cy,psx,psy);
            {std::vector<float> v; pushLine(v,L,psy,psx,psy,max(0.8f,z*.7f));
             drawColored(v,0.38f,0.42f,0.60f,1.f);}
            {std::vector<float> v; pushCircle(v,psx,psy,max(2.5f,kPinR*z));
             drawColored(v,
               pin.connected?1.f:0.f,
               pin.connected?.86f:.31f,
               pin.connected?.47f:.51f,1.f);}
        }
        // Output pin stubs + dots
        for (auto &pin:g.outPins) {
            float psx,psy; w2s(pin.cx,pin.cy,psx,psy);
            {std::vector<float> v; pushLine(v,R,psy,psx,psy,max(0.8f,z*.7f));
             drawColored(v,0.38f,0.42f,0.60f,1.f);}
            {std::vector<float> v; pushCircle(v,psx,psy,max(2.5f,kPinR*z));
             drawColored(v,
               pin.connected?1.f:.86f,
               pin.connected?.86f:.51f,
               pin.connected?.47f:.16f,1.f);}
        }

        // INPUT value badge
        if (g.type==GateType::INPUT) {
            float cr2=max(5.f,8.f*z);
            float bcx=R-cr2-2*z, bcy=yT+cr2+2*z;
            std::vector<float> v; pushCircle(v,bcx,bcy,cr2);
            if(g.inputVal) drawColored(v,0.31f,0.86f,0.47f,1.f);
            else           drawColored(v,0.86f,0.31f,0.31f,1.f);
        }

        // OUTPUT indicator — shows live evaluated value
        if (g.type==GateType::OUTPUT) {
            bool live=false;
            for (auto &w:circuit.wires)
                if (w.dst.gateId==g.id){live=w.value;break;}
            float cr2=max(4.f,7.f*z);
            float bcx=R-cr2-2*z, bcy=yT+cr2+2*z;
            {std::vector<float> v; pushCircle(v,bcx,bcy,cr2);
             drawColored(v,live?.31f:.86f,live?.86f:.51f,live?.47f:.16f,0.4f);}
            {std::vector<float> v; pushCircle(v,bcx,bcy,cr2*.6f);
             drawColored(v,live?.31f:.86f,live?.86f:.51f,live?.47f:.16f,0.9f);}
        }
    }
};

// =============================================================================
// §6  APP
// =============================================================================

class LogicSimApp : public Component {
    State<int>         gateCount;
    State<std::string> statusMsg;
    State<double>      zoomPct;

    std::shared_ptr<CircuitSurface> surface_;
    CanvasWidget *canvas_ = nullptr;

    static constexpr int kSW=180, kTH=46, kHH=24;

    void notifyZoom(float z) {
        double p=double(z)*100.0;
        if (std::abs(p-zoomPct.get())>0.4) zoomPct.set(p);
    }
    void applyZoomFromSlider(double p) {
        if (!surface_) return;
        surface_->setZoomLevel(float(p/100.0));
        if (canvas_) canvas_->redraw();
    }

public:
    LogicSimApp()
        : gateCount(0,context),
          statusMsg("Click a gate type to place it  ·  Drag output pin to connect  ·  Del to remove",context),
          zoomPct(100.0,context) {}

    WidgetPtr build() override {
        int SW=GetSystemMetrics(SM_CXSCREEN);
        int SH=GetSystemMetrics(SM_CYSCREEN);
        int vW=SW-kSW, vH=SH-kTH-kHH;

        auto cv=std::make_shared<CanvasWidget>()->setSize(vW,vH);
        cv->setCanvasSize(vW,vH);
        cv->setViewportEnabled(false);
        surface_=cv->setSurface<CircuitSurface>();
        canvas_ =cv.get();

        surface_->onCircuitChanged=[this](){
            gateCount.set(int(surface_->circuit.gates.size()));
        };
        surface_->onStatusMessage=[this](const char *m){statusMsg.set(m);};
        surface_->onRedrawNeeded =[this](){if(canvas_)canvas_->redraw();};
        surface_->onZoomChanged  =[this](float z){notifyZoom(z);};

        zoomPct.listen([this](double p){applyZoomFromSlider(p);});

        // Colors
        COLORREF kBg     = RGB(12,12,18);
        COLORREF kCard   = RGB(18,18,28);
        COLORREF kBord   = RGB(40,42,62);
        COLORREF kAccent = RGB(174,129,255);
        COLORREF kGreen  = RGB(148,226,213);
        COLORREF kDim    = RGB(80,84,110);
        COLORREF kText   = RGB(192,202,232);

        // Sidebar tiles
        struct TileInfo { GateType type; COLORREF ac; const char *sub; };
        const std::vector<TileInfo> tiles = {
            {GateType::AND,   RGB(70,120,220), "A & B"},
            {GateType::OR,    RGB(70,190,110), "A | B"},
            {GateType::NOT,   RGB(170,70,220), "! A"},
            {GateType::NAND,  RGB(220,70,70),  "!(A & B)"},
            {GateType::NOR,   RGB(200,70,170), "!(A | B)"},
            {GateType::XOR,   RGB(70,170,220), "A ^ B"},
            {GateType::XNOR,  RGB(110,110,220),"!(A ^ B)"},
            {GateType::INPUT, RGB(70,210,110), "Toggle 0/1"},
            {GateType::OUTPUT,RGB(220,130,40), "LED output"},
        };

        auto makeTile=[&](const TileInfo &ti)->WidgetPtr {
            GateType t=ti.type; COLORREF ac=ti.ac;
            return GestureDetector(
                Container(
                    Row(
                        Container(nullptr)
                            ->setWidth(3)->setHeight(34)
                            ->setBackgroundColor(ac)->setBorderRadius(2),
                        SizedBox(8,0),
                        Column(
                            Text(kGateName[(int)t])
                                ->setFontSize(11)->setFontWeight(FontWeight::Bold)
                                ->setTextColor(ac),
                            SizedBox(0,2),
                            Text(ti.sub)->setFontSize(8)->setTextColor(kDim)
                        )->setSpacing(0)
                    )->setSpacing(0)->setCrossAxisAlignment(CrossAxisAlignment::Center)
                )->setWidth(kSW-20)->setHeight(42)
                 ->setBackgroundColor(kCard)->setBorderRadius(6)
                 ->setBorderWidth(1)->setBorderColor(kBord)
                 ->setHoverBackgroundColor(RGB(26,26,42))
            )->setOnTap([this,t](){
                if(!surface_) return;
                surface_->dropGate(t);
                statusMsg.set(std::string("Placed ")+kGateName[(int)t]
                              +" gate  ·  Drag its output pin (right side) to connect");
            });
        };

        auto tileList=std::make_shared<ColumnWidget>(); tileList->setSpacing(5);
        for (auto &ti:tiles) tileList->addChild(makeTile(ti));

        auto sidebar=Container(
            Column(
                Text("GATES")->setFontSize(8)
                    ->setFontWeight(FontWeight::Bold)->setTextColor(RGB(70,74,100)),
                SizedBox(0,10),
                tileList
            )->setSpacing(0)
        )->setWidth(kSW)->setBackgroundColor(kBg)->setPaddingAll(10,10,10,10);

        auto mkBtn=[&](const std::string &lbl,COLORREF col,
                       std::function<void()> fn)->WidgetPtr {
            return Button(lbl,fn)
                ->setBackgroundColor(RGB(24,24,38))->setTextColor(col)
                ->setBorderRadius(5)->setHeight(26)->setPadding(4);
        };

        auto toolbar=Container(
            Row(
                Text("⚡ Logic Sim")
                    ->setFontSize(13)->setFontWeight(FontWeight::Bold)
                    ->setTextColor(kAccent),
                SizedBox(16,0),
                Text("Zoom")->setFontSize(9)->setTextColor(kDim),
                SizedBox(4,0),
                Slider(10.0,400.0,1.0)->setValue(zoomPct)
                    ->setTrackFillColor(kAccent)->setWidth(100),
                SizedBox(4,0),
                Text(zoomPct,[](double v){
                    char b[16]; _snprintf_s(b,sizeof(b),_TRUNCATE,"%.0f%%",v);
                    return std::string(b);
                })->setFontSize(10)->setTextColor(kText)->setMinWidth(40),
                mkBtn("Fit",kAccent,[this](){
                    if(surface_){surface_->fitToView();notifyZoom(surface_->getZoom());}
                }),
                mkBtn("1:1",kAccent,[this](){
                    if(surface_){surface_->resetZoom();notifyZoom(1.f);}
                }),
                SizedBox(14,0),
                mkBtn("🗑 Clear",RGB(243,139,168),[this](){
                    if(!surface_) return;
                    surface_->circuit.clearAll();   // clears gates + wires
                    gateCount.set(0);
                    if(canvas_) canvas_->redraw();
                }),
                SizedBox(14,0),
                Text(gateCount,[](int n){
                    return std::to_string(n)+(n==1?" gate":" gates");
                })->setFontSize(10)->setTextColor(kDim)
            )->setSpacing(8)->setCrossAxisAlignment(CrossAxisAlignment::Center)
        )->setBackgroundColor(kBg)->setPaddingAll(12,8,12,8)->setHeight(kTH);

        auto hints=Container(
            Row(
                Text(statusMsg,[](const std::string &s){return s;})
                    ->setFontSize(10)->setTextColor(kGreen),
                SizedBox(16,0),
                Text("Drag output pin→input pin=connect  ·  Right-click/Dbl-click wire=remove  ·  Space=toggle INPUT  ·  Del=remove  ·  Ctrl+Scroll=zoom")
                    ->setFontSize(9)->setTextColor(RGB(48,52,72))
            )->setSpacing(0)
        )->setBackgroundColor(kBg)->setPaddingAll(10,3,10,3)->setHeight(kHH);

        auto canvasCol=Column(toolbar,hints,cv)->setSpacing(0);
        return Scaffold(Row(sidebar,canvasCol)->setSpacing(0));
    }
};

// =============================================================================
// §7  ENTRY POINT
// =============================================================================

int WINAPI WinMain(HINSTANCE hInst,HINSTANCE,LPSTR,int) {
    FluxUI app(hInst);
    app.build([&](){
        return FluxApp("Logic Sim",BuildComponent<LogicSimApp>(),AppTheme::dark());
    });
    RECT wa; SystemParametersInfo(SPI_GETWORKAREA,0,&wa,0);
    app.createWindow("FluxUI – Logic Sim",wa.right-wa.left,wa.bottom-wa.top);
    return app.run();
}