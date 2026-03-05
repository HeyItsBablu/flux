// =============================================================================
// logic_sim.hpp  —  Logic Gate Simulator  v4


#pragma once

#include "flux.hpp"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Extra GL proc we need (uniform2f) — loaded once
// ---------------------------------------------------------------------------
using PFNGLUNIFORM4FPROC_LS = void(APIENTRY *)(GLint, GLfloat, GLfloat,
                                               GLfloat, GLfloat);
using PFNGLUNIFORM2FPROC_LS = void(APIENTRY *)(GLint, GLfloat, GLfloat);

namespace ls_gl {
inline PFNGLUNIFORM4FPROC_LS uniform4f = nullptr;
inline PFNGLUNIFORM2FPROC_LS uniform2f = nullptr;
inline void init() {
  if (uniform4f)
    return;
  HMODULE gl32 = GetModuleHandleA("opengl32.dll");
  auto load = [&](const char *n) -> void * {
    void *p = (void *)wglGetProcAddress(n);
    if (!p || p == (void *)1 || p == (void *)2 || p == (void *)3 ||
        p == (void *)-1)
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
  AND = 0, OR, NOT, NAND, NOR, XOR, XNOR, INPUT, OUTPUT, COUNT
};
static constexpr int kGTC = (int)GateType::COUNT;
static constexpr int kInPins[kGTC]  = {2, 2, 1, 2, 2, 2, 2, 0, 1};
static constexpr int kOutPins[kGTC] = {1, 1, 1, 1, 1, 1, 1, 1, 0};
static const char *kGateName[kGTC]  = {
    "AND","OR","NOT","NAND","NOR","XOR","XNOR","INPUT","OUTPUT"};

static constexpr float kGW     = 88.f;
static constexpr float kGH     = 60.f;
static constexpr float kPinR   = 5.f;
static constexpr float kPinLen = 16.f;
static constexpr float kCorner = 8.f;

struct Pin { float cx = 0, cy = 0; bool connected = false; };

struct Gate {
  int id = 0;
  GateType type = GateType::AND;
  float x = 0, y = 0;         // canvas coords (world space)
  bool selected  = false;
  bool inputVal  = false;
  std::vector<Pin> inPins, outPins;

  void recomputePins() {
    int ni = kInPins[(int)type], no = kOutPins[(int)type];
    inPins.resize(ni); outPins.resize(no);
    float L = x - kGW*.5f, R = x + kGW*.5f, B = y - kGH*.5f;
    for (int i = 0; i < ni; i++) {
      float t = (ni == 1) ? .5f : float(i)/(ni-1);
      inPins[i] = {L - kPinLen, B + t*kGH};
    }
    for (int i = 0; i < no; i++) {
      float t = (no == 1) ? .5f : float(i)/(no-1);
      outPins[i] = {R + kPinLen, B + t*kGH};
    }
  }
};

struct Circuit {
  std::vector<Gate> gates;
  int nextId = 1;

  int add(GateType t, float x, float y) {
    Gate g; g.id = nextId++; g.type = t; g.x = x; g.y = y;
    g.recomputePins(); gates.push_back(g); return g.id;
  }
  Gate *find(int id) {
    for (auto &g : gates) if (g.id == id) return &g; return nullptr;
  }
  void remove(int id) {
    gates.erase(std::remove_if(gates.begin(), gates.end(),
                [id](const Gate &g){ return g.id == id; }), gates.end());
  }
  void clear() { gates.clear(); nextId = 1; }
};

// =============================================================================
// §2  VERTEX HELPERS
// =============================================================================

static void pushQuad(std::vector<float> &v,
                     float x0,float y0,float x1,float y1) {
  v.insert(v.end(), {x0,y0, x1,y0, x1,y1, x1,y1, x0,y1, x0,y0});
}

static void pushLine(std::vector<float> &v,
                     float x0,float y0,float x1,float y1,float hw) {
  float dx=x1-x0, dy=y1-y0, len=sqrtf(dx*dx+dy*dy);
  if (len < 0.001f) { pushQuad(v,x0-hw,y0-hw,x0+hw,y0+hw); return; }
  float nx=-dy/len*hw, ny=dx/len*hw;
  v.insert(v.end(), {x0+nx,y0+ny, x0-nx,y0-ny, x1-nx,y1-ny,
                     x1-nx,y1-ny, x1+nx,y1+ny, x0+nx,y0+ny});
}

static void pushCircle(std::vector<float> &v,
                       float cx,float cy,float r,int segs=16) {
  for (int i=0;i<segs;i++) {
    float a0=float(i)/segs*6.2831853f, a1=float(i+1)/segs*6.2831853f;
    v.insert(v.end(), {cx,cy,
                       cx+cosf(a0)*r,cy+sinf(a0)*r,
                       cx+cosf(a1)*r,cy+sinf(a1)*r});
  }
}

static void pushRoundRectFilled(std::vector<float> &v,
                                float L,float B,float R2,float T,float cr) {
  cr = min(cr, min((R2-L)*.45f,(T-B)*.45f));
  pushQuad(v, L+cr,B, R2-cr,T);
  pushQuad(v, L,B+cr, L+cr,T-cr);
  pushQuad(v, R2-cr,B+cr, R2,T-cr);
  int segs=8;
  float ccx[4]={R2-cr,L+cr,L+cr,R2-cr}, ccy[4]={T-cr,T-cr,B+cr,B+cr};
  float sa[4]={0,1.5708f,3.1416f,4.7124f};
  for (int c=0;c<4;c++)
    for (int i=0;i<segs;i++) {
      float a0=sa[c]+float(i)/segs*1.5708f, a1=sa[c]+float(i+1)/segs*1.5708f;
      v.insert(v.end(), {ccx[c],ccy[c],
                         ccx[c]+cosf(a0)*cr, ccy[c]+sinf(a0)*cr,
                         ccx[c]+cosf(a1)*cr, ccy[c]+sinf(a1)*cr});
    }
}

static void pushRoundRect(std::vector<float> &v,
                          float L,float B,float R2,float T,float cr,float hw,
                          int cornSegs=6) {
  cr = min(cr, min((R2-L)*.4f,(T-B)*.4f));
  pushLine(v,L+cr,B,R2-cr,B,hw); pushLine(v,R2,B+cr,R2,T-cr,hw);
  pushLine(v,R2-cr,T,L+cr,T,hw); pushLine(v,L,T-cr,L,B+cr,hw);
  float ox[4]={R2-cr,L+cr,L+cr,R2-cr}, oy[4]={T-cr,T-cr,B+cr,B+cr};
  float sa[4]={0,1.5708f,3.1416f,4.7124f};
  for (int c=0;c<4;c++)
    for (int i=0;i<cornSegs;i++) {
      float a0=sa[c]+float(i)/cornSegs*1.5708f,
            a1=sa[c]+float(i+1)/cornSegs*1.5708f;
      pushLine(v, ox[c]+cosf(a0)*cr,oy[c]+sinf(a0)*cr,
                  ox[c]+cosf(a1)*cr,oy[c]+sinf(a1)*cr, hw);
    }
}

// =============================================================================
// §3  SHADERS  — screen-pixel coords, Y-down, origin top-left
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
  memcpy(c.body,   bodies[i],  16);
  memcpy(c.label,  borders[i], 16); // reuse border color for label tint
  if (selected) {
    c.border[0]=0.80f; c.border[1]=0.65f;
    c.border[2]=0.97f; c.border[3]=1.f;
  } else {
    memcpy(c.border, borders[i], 16);
  }
  return c;
}

// =============================================================================
// §5  CIRCUIT SURFACE  — owns pan/zoom, no external Viewport
// =============================================================================

class CircuitSurface : public RenderSurface {
public:
  Circuit circuit;

  std::function<void()>         onCircuitChanged;
  std::function<void(const char*)> onStatusMessage;
  std::function<void()>         onRedrawNeeded;
  std::function<void(float)>    onZoomChanged;   // notifies app of new zoom

  // Called by app toolbar buttons
  void resetZoom()  { zoom_=1.f; camX_=0.f; camY_=0.f; redraw(); }
  void fitToView()  {
    if (circuit.gates.empty()) { resetZoom(); return; }
    // compute bounding box of all gates
    float minX= 1e9,minY= 1e9,maxX=-1e9,maxY=-1e9;
    for (auto &g : circuit.gates) {
      minX=min(minX,g.x-kGW); maxX=max(maxX,g.x+kGW);
      minY=min(minY,g.y-kGH); maxY=max(maxY,g.y+kGH);
    }
    float worldW=maxX-minX, worldH=maxY-minY;
    if (worldW<1||worldH<1){resetZoom();return;}
    float pad=60.f;
    float zx=(viewW_-pad*2)/worldW, zy=(viewH_-pad*2)/worldH;
    zoom_=std::clamp(min(zx,zy),0.05f,4.f);
    // center
    camX_=(minX+maxX)*.5f - viewW_*.5f/zoom_;
    camY_=(minY+maxY)*.5f - viewH_*.5f/zoom_;
    redraw();
  }
  void setZoomLevel(float z) {
    zoom_=std::clamp(z,0.05f,8.f);
    redraw();
    if (onZoomChanged) onZoomChanged(zoom_);
  }
  float getZoom() const { return zoom_; }

  // Place a gate at the current screen center
  void dropGate(GateType type) {
    // screen center → world
    float wx = camX_ + viewW_*.5f/zoom_;
    float wy = camY_ + viewH_*.5f/zoom_;
    // jitter so stacked gates are visible
    static int n=0; int slot=(n++)%7;
    float jx=float(slot-3)*(kGW+24.f);
    float jy=float(slot%3-1)*(kGH+18.f);
    circuit.add(type, wx+jx, wy+jy);
    if (onCircuitChanged) onCircuitChanged();
    redraw();
  }

  // ── RenderSurface ──────────────────────────────────────────────────────────
  void initialize(int w, int h) override {
    ls_gl::init();
    viewW_=w; viewH_=h;
    // Start camera centered on world origin
    camX_ = -w*.5f;
    camY_ = -h*.5f;
    buildShader();
    buildVAO();
  }
  void resize(int w, int h) override { viewW_=w; viewH_=h; }
  void update(double) override {}

  void render(const float* /*mvp*/) override {
    glViewport(0,0,viewW_,viewH_);
    glClearColor(0.055f,0.055f,0.08f,1.f);
    glClear(GL_COLOR_BUFFER_BIT);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    GL.useProgram(prog_);
    ls_gl::uniform2f(uViewSize_,float(viewW_),float(viewH_));
    GL.bindVertexArray(vao_);

    drawGrid();
    for (auto &g : circuit.gates) drawGate(g);

    GL.bindVertexArray(0);
    GL.useProgram(0);
    glDisable(GL_BLEND);
  }

  void destroy() override {
    if (prog_)  { GL.deleteProgram(prog_);       prog_=0; }
    if (vao_)   { GL.deleteVertexArrays(1,&vao_); vao_=0; }
    if (vbo_)   { GL.deleteBuffers(1,&vbo_);      vbo_=0; }
  }

  bool needsContinuousRedraw() const override { return false; }

  // ── Input ──────────────────────────────────────────────────────────────────
  void onMouseDown(float sx, float sy) override {
    lastSX_=sx; lastSY_=sy;
    for (auto &g : circuit.gates) g.selected=false;
    // convert screen → world
    float wx,wy; s2w(sx,sy,wx,wy);
    int hit=hitTest(wx,wy);
    if (hit>=0) {
      Gate *g=circuit.find(hit);
      if (g) {
        g->selected=true;
        dragId_=hit;
        dragOx_=wx-g->x; dragOy_=wy-g->y;
      }
    } else { dragId_=-1; }
    redraw();
  }
  void onMouseMove(float sx, float sy) override {
    if (mmb_) {
      // input is Y-up canvas space, both axes subtract directly
      float dsx=sx-lastSX_, dsy=sy-lastSY_;
      camX_ -= dsx/zoom_;
      camY_ -= dsy/zoom_;
      lastSX_=sx; lastSY_=sy;
      redraw(); return;
    }
    lastSX_=sx; lastSY_=sy;
    if (dragId_>=0) {
      float wx,wy; s2w(sx,sy,wx,wy);
      Gate *g=circuit.find(dragId_);
      if (g) { g->x=wx-dragOx_; g->y=wy-dragOy_; g->recomputePins(); redraw(); }
    }
  }
  void onMouseUp(float,float) override {
    mmb_=false;
    if (dragId_>=0) {
      dragId_=-1;
      if (onCircuitChanged) onCircuitChanged();
    }
  }
  void onRightMouseDown(float,float) override {}

  // Middle mouse
  void onMiddleDown(float sx,float sy) { mmb_=true; lastSX_=sx; lastSY_=sy; }
  void onMiddleUp()                    { mmb_=false; }

  // Scroll: dy>0 = wheel up = zoom in
  // cx,cy are Y-up canvas coords (same space as onMouseDown input)
  void onScroll(float cx,float cy,float dy,bool ctrl) {
    if (ctrl) {
      float wx0,wy0; s2w(cx,cy,wx0,wy0);
      float factor = (dy>0)?1.12f:1.f/1.12f;
      zoom_=std::clamp(zoom_*factor,0.05f,8.f);
      // keep world point under cursor fixed
      camX_=wx0-cx/zoom_;
      camY_=wy0-cy/zoom_;
      if (onZoomChanged) onZoomChanged(zoom_);
    } else {
      camY_ += dy*40.f/zoom_;
    }
    redraw();
  }

  void onKeyDown(int key) override {
    if (key==VK_DELETE||key==VK_BACK) {
      bool any=false;
      for (int i=int(circuit.gates.size())-1;i>=0;i--)
        if (circuit.gates[i].selected) {
          circuit.remove(circuit.gates[i].id); any=true;
        }
      if (any) {
        if (onCircuitChanged) onCircuitChanged();
        redraw();
      }
    }
    // Space+drag pan is handled at canvas level — not needed here
  }

private:
  int   viewW_=1, viewH_=1;
  float camX_=0, camY_=0, zoom_=1.f;  // camera: world coords at top-left

  GLuint prog_=0, vao_=0, vbo_=0;
  GLint  uViewSize_=-1, uColor_=-1;

  int   dragId_=-1;
  float dragOx_=0, dragOy_=0;
  float lastSX_=0, lastSY_=0;
  bool  mmb_=false;

  void redraw() { if (onRedrawNeeded) onRedrawNeeded(); }

  // Canvas coords (Y-up, from CanvasWidget::screenToCanvas) → screen pixels (Y-down)
  // CanvasWidget passes us coords already in Y-up canvas space, so:
  //   screen_x = (world_x - camX) * zoom
  //   screen_y = viewH - (world_y - camY) * zoom
  // But our input IS already Y-up world-like, so we just need:
  //   screen_x = (cx - camX) * zoom
  //   screen_y = viewH - (cy - camY) * zoom   [for rendering, Y-down GL pixels]
  void w2s(float wx,float wy,float &sx,float &sy) const {
    sx = (wx - camX_) * zoom_;
    sy = viewH_ - (wy - camY_) * zoom_;
  }
  // Input coords from onMouseDown/Move are Y-up canvas space → world directly
  // (CanvasWidget already did the Y flip via Viewport::screenToCanvas)
  void s2w(float cx,float cy,float &wx,float &wy) const {
    wx = camX_ + cx / zoom_;
    wy = camY_ + cy / zoom_;
  }

  void buildShader() {
    prog_=glutil::linkProgram(kGVert,kGFrag);
    assert(prog_);
    uViewSize_=GL.getUniformLocation(prog_,"uViewSize");
    uColor_   =GL.getUniformLocation(prog_,"uColor");
  }
  void buildVAO() {
    GL.genVertexArrays(1,&vao_);
    GL.genBuffers(1,&vbo_);
    GL.bindVertexArray(vao_);
    GL.bindBuffer(GL_ARRAY_BUFFER,vbo_);
    GL.enableVertexAttribArray(0);
    GL.vertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,2*sizeof(float),(void*)0);
    GL.bindVertexArray(0);
  }

  void upload(const std::vector<float> &v) {
    GL.bindBuffer(GL_ARRAY_BUFFER,vbo_);
    GL.bufferData(GL_ARRAY_BUFFER,
                  GLsizeiptr(v.size()*sizeof(float)),
                  v.data(), GL_DYNAMIC_DRAW);
  }
  void drawColored(const std::vector<float> &v,
                   float r,float g,float b,float a=1.f) {
    if (v.empty()) return;
    upload(v);
    ls_gl::uniform4f(uColor_,r,g,b,a);
    glDrawArrays(GL_TRIANGLES,0,(GLsizei)(v.size()/2));
  }
  void drawC(const std::vector<float> &v,const float c[4]) {
    drawColored(v,c[0],c[1],c[2],c[3]);
  }

  // ── Grid ───────────────────────────────────────────────────────────────────
  void drawGrid() {
    float gs=40.f, step=gs*zoom_;
    if (step<4.f) return;
    float vw=float(viewW_), vh=float(viewH_);

    std::vector<float> lines; lines.reserve(2048);
    float hw=0.5f;

    float startWX=std::floor(camX_/gs)*gs;
    for (float wx=startWX;;wx+=gs) {
      float sx=(wx-camX_)*zoom_; if (sx>vw+step) break;
      pushLine(lines,sx,0,sx,vh,hw);
    }
    float startWY=std::floor(camY_/gs)*gs;
    for (float wy=startWY;;wy+=gs) {
      float sy=vh-(wy-camY_)*zoom_; if (sy<-step) break;
      pushLine(lines,0,sy,vw,sy,hw);
    }
    drawColored(lines,0.12f,0.13f,0.19f,1.f);

    // origin cross
    float ox,oy; w2s(0,0,ox,oy);
    std::vector<float> cross;
    pushLine(cross,ox-10,oy,ox+10,oy,1.f);
    pushLine(cross,ox,oy-10,ox,oy+10,1.f);
    drawColored(cross,0.22f,0.23f,0.36f,1.f);
  }

  // ── Gate ───────────────────────────────────────────────────────────────────
  void drawGate(const Gate &g) {
    float sx,sy; w2s(g.x,g.y,sx,sy);
    float z=zoom_;
    float hw=kGW*z*.5f, hh=kGH*z*.5f;
    float L=sx-hw,R=sx+hw, yT=sy-hh,yB=sy+hh;
    float cr=kCorner*z, bw=max(1.f,z);

    GateColors col=gateColors(g.type,g.selected);

    // Shadow
    { std::vector<float> v;
      pushRoundRectFilled(v,L+3,yT+3,R+3,yB+3,cr);
      drawColored(v,0,0,0,0.45f); }

    // Body
    { std::vector<float> v;
      pushRoundRectFilled(v,L,yT,R,yB,cr);
      drawC(v,col.body); }

    // Border
    { std::vector<float> v;
      pushRoundRect(v,L,yT,R,yB,cr,bw*.5f+.5f);
      drawC(v,col.border); }

    // Selection glow
    if (g.selected) {
      std::vector<float> v;
      pushRoundRect(v,L+2,yT+2,R-2,yB-2,cr-2,bw*.5f);
      drawColored(v,col.border[0],col.border[1],col.border[2],0.3f);
    }

    // Top color band
    { float bh=max(3.f,hh*.22f);
      std::vector<float> v;
      pushQuad(v,L+cr,yT,R-cr,yT+bh);
      pushRoundRectFilled(v,L,yT,R,yT+bh*1.5f,cr);
      float bc[4]={col.border[0]*.4f,col.border[1]*.4f,
                   col.border[2]*.4f,0.8f};
      drawC(v,bc); }

    // Label (drawn as a simple colored rect placeholder — text via GDI
    // would need a texture; for now draw the name as a thin highlight bar)
    // A proper text label would require the GDI→texture path from RasterSurface.
    // Here we draw a subtle name-band at the center for visual ID.
    {
      float lh=max(1.5f,z*1.5f);
      float ly=sy-lh*.5f;
      float lw=hw*0.55f;
      std::vector<float> lv;
      pushQuad(lv,sx-lw,ly,sx+lw,ly+lh);
      drawColored(lv,
        col.border[0],col.border[1],col.border[2],0.55f);
    }

    // Input pin stubs + dots
    for (auto &pin : g.inPins) {
      float psx,psy; w2s(pin.cx,pin.cy,psx,psy);
      { std::vector<float> v;
        pushLine(v,L,psy,psx,psy,max(0.8f,z*.7f));
        drawColored(v,0.38f,0.42f,0.60f,1.f); }
      { std::vector<float> v;
        float pr=max(2.5f,kPinR*z);
        pushCircle(v,psx,psy,pr);
        float dc=pin.connected?1.f:0.f;
        float dg=pin.connected?.86f:.31f;
        float db=pin.connected?.47f:.51f;
        drawColored(v,dc,dg,db,1.f); }
    }

    // Output pin stubs + dots
    for (auto &pin : g.outPins) {
      float psx,psy; w2s(pin.cx,pin.cy,psx,psy);
      { std::vector<float> v;
        pushLine(v,R,psy,psx,psy,max(0.8f,z*.7f));
        drawColored(v,0.38f,0.42f,0.60f,1.f); }
      { std::vector<float> v;
        float pr=max(2.5f,kPinR*z);
        pushCircle(v,psx,psy,pr);
        float dc=pin.connected?1.f:.86f;
        float dg=pin.connected?.86f:.51f;
        float db=pin.connected?.47f:.16f;
        drawColored(v,dc,dg,db,1.f); }
    }

    // INPUT value badge
    if (g.type==GateType::INPUT) {
      float cr2=max(5.f,8.f*z);
      float bcx=R-cr2-2*z, bcy=yT+cr2+2*z;
      std::vector<float> v; pushCircle(v,bcx,bcy,cr2);
      if (g.inputVal) drawColored(v,0.31f,0.86f,0.47f,1.f);
      else            drawColored(v,0.86f,0.31f,0.31f,1.f);
    }

    // OUTPUT indicator
    if (g.type==GateType::OUTPUT) {
      float cr2=max(4.f,7.f*z);
      float bcx=R-cr2-2*z, bcy=yT+cr2+2*z;
      { std::vector<float> v; pushCircle(v,bcx,bcy,cr2);
        drawColored(v,0.86f,0.51f,0.16f,0.4f); }
      { std::vector<float> v; pushCircle(v,bcx,bcy,cr2*.6f);
        drawColored(v,0.86f,0.51f,0.16f,0.8f); }
    }
  }

  int hitTest(float wx,float wy) const {
    for (int i=int(circuit.gates.size())-1;i>=0;i--) {
      const Gate &g=circuit.gates[i];
      if (wx>=g.x-kGW*.5f&&wx<=g.x+kGW*.5f&&
          wy>=g.y-kGH*.5f&&wy<=g.y+kGH*.5f) return g.id;
    }
    return -1;
  }
};

// =============================================================================
// §6  CANVAS WIDGET SUBCLASS  — forwards MMB + scroll to CircuitSurface
//     We can't override window proc directly, so we use CanvasWidget's
//     existing mouse callbacks and rely on onMouseDown/Move/Up for MMB
//     by detecting WM_MBUTTONDOWN at the app level via subclassing.
//     Simpler: CircuitSurface handles everything through the standard
//     onMouseDown/Move/Up with a "pan mode" flag toggled by space-bar,
//     and scroll is forwarded via a public helper.
// =============================================================================

// =============================================================================
// §7  APP
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
      statusMsg("Click a gate type to place it  ·  Drag to move  ·  Del to remove",
                context),
      zoomPct(100.0,context) {}

  WidgetPtr build() override {
    int SW=GetSystemMetrics(SM_CXSCREEN);
    int SH=GetSystemMetrics(SM_CYSCREEN);
    int vW=SW-kSW, vH=SH-kTH-kHH;

    // ── Canvas — viewport disabled, surface owns pan/zoom ─────────────────
    auto cv = std::make_shared<CanvasWidget>()->setSize(vW,vH);
    cv->setCanvasSize(vW,vH);          // canvas == view size (no scroll needed)
    cv->setViewportEnabled(false);
    surface_ = cv->setSurface<CircuitSurface>();
    canvas_  = cv.get();

    surface_->onCircuitChanged = [this]() {
      gateCount.set(int(surface_->circuit.gates.size()));
    };
    surface_->onStatusMessage = [this](const char *m){ statusMsg.set(m); };
    surface_->onRedrawNeeded  = [this](){ if(canvas_) canvas_->redraw(); };
    surface_->onZoomChanged   = [this](float z){ notifyZoom(z); };

    zoomPct.listen([this](double p){ applyZoomFromSlider(p); });

    // ── Colors ────────────────────────────────────────────────────────────
    COLORREF kBg     = RGB(12,12,18);
    COLORREF kCard   = RGB(18,18,28);
    COLORREF kBord   = RGB(40,42,62);
    COLORREF kAccent = RGB(174,129,255);
    COLORREF kGreen  = RGB(148,226,213);
    COLORREF kDim    = RGB(80,84,110);
    COLORREF kText   = RGB(192,202,232);

    // ── Sidebar tiles ─────────────────────────────────────────────────────
    struct TileInfo { GateType type; COLORREF ac; const char *sub; };
    const std::vector<TileInfo> tiles = {
      {GateType::AND,  RGB(70,120,220), "A & B"},
      {GateType::OR,   RGB(70,190,110), "A | B"},
      {GateType::NOT,  RGB(170,70,220), "! A"},
      {GateType::NAND, RGB(220,70,70),  "!(A & B)"},
      {GateType::NOR,  RGB(200,70,170), "!(A | B)"},
      {GateType::XOR,  RGB(70,170,220), "A ^ B"},
      {GateType::XNOR, RGB(110,110,220),"!(A ^ B)"},
      {GateType::INPUT,RGB(70,210,110), "Toggle 0/1"},
      {GateType::OUTPUT,RGB(220,130,40),"LED output"},
    };

    auto makeTile = [&](const TileInfo &ti) -> WidgetPtr {
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
        if (!surface_) return;
        surface_->dropGate(t);
        statusMsg.set(std::string("Placed ")+kGateName[(int)t]+" gate");
      });
    };

    auto tileList=std::make_shared<ColumnWidget>(); tileList->setSpacing(5);
    for (auto &ti : tiles) tileList->addChild(makeTile(ti));

    auto sidebar=Container(
      Column(
        Text("GATES")->setFontSize(8)
          ->setFontWeight(FontWeight::Bold)->setTextColor(RGB(70,74,100)),
        SizedBox(0,10),
        tileList
      )->setSpacing(0)
    )->setWidth(kSW)->setBackgroundColor(kBg)->setPaddingAll(10,10,10,10);

    // ── Toolbar ───────────────────────────────────────────────────────────
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
          if (surface_) { surface_->fitToView(); notifyZoom(surface_->getZoom()); }
        }),
        mkBtn("1:1",kAccent,[this](){
          if (surface_) { surface_->resetZoom(); notifyZoom(1.f); }
        }),
        SizedBox(14,0),
        mkBtn("🗑 Clear",RGB(243,139,168),[this](){
          if (!surface_) return;
          surface_->circuit.clear();
          gateCount.set(0);
          if (canvas_) canvas_->redraw();
        }),
        SizedBox(14,0),
        Text(gateCount,[](int n){
          return std::to_string(n)+(n==1?" gate":" gates");
        })->setFontSize(10)->setTextColor(kDim)
      )->setSpacing(8)->setCrossAxisAlignment(CrossAxisAlignment::Center)
    )->setBackgroundColor(kBg)->setPaddingAll(12,8,12,8)->setHeight(kTH);

    // ── Hints bar ─────────────────────────────────────────────────────────
    auto hints=Container(
      Row(
        Text(statusMsg,[](const std::string &s){return s;})
          ->setFontSize(10)->setTextColor(kGreen),
        SizedBox(16,0),
        Text("Ctrl+Scroll=zoom  ·  Scroll=pan  ·  Drag=move  ·  Del=remove  ·  (wires in phase 2)")
          ->setFontSize(9)->setTextColor(RGB(48,52,72))
      )->setSpacing(0)
    )->setBackgroundColor(kBg)->setPaddingAll(10,3,10,3)->setHeight(kHH);

    // ── Root ──────────────────────────────────────────────────────────────
    auto canvasCol=Column(toolbar,hints,cv)->setSpacing(0);
    return Scaffold(Row(sidebar,canvasCol)->setSpacing(0));
  }
};

// =============================================================================
// §8  ENTRY POINT
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