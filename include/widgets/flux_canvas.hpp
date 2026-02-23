#ifndef FLUX_CANVAS_HPP
#define FLUX_CANVAS_HPP

// ============================================================================
// flux_canvas.hpp  —  HTML-Canvas-style 2-D drawing widget for Flux
//
// Drop next to flux_widget.hpp.  No extra dependencies beyond what
// flux_display.hpp already pulls in (OpenGL, GDI+, Win32).
//
// Mouse Events
// ------------
//   canvas->onMouseDown([](int x, int y) { ... });   // button pressed
//   canvas->onMouseMove([](int x, int y) { ... });   // cursor moved
//   canvas->onMouseUp  ([](int x, int y) { ... });   // button released
//   canvas->onMouseEnter([](int x, int y) { ... });  // cursor entered widget
//   canvas->onMouseLeave([](int x, int y) { ... });  // cursor left widget
//   canvas->onMouseWheel([](int x, int y, int delta) { ... }); // scroll
//
//   All coordinates are canvas-local pixels (top-left = 0, 0).
//   delta > 0  → scroll up / zoom in;  delta < 0  → scroll down / zoom out.
//
// ============================================================================

#include "flux_state.hpp"
#include "flux_widget.hpp"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <gl/GL.h>
#include <gl/GLU.h>
#include <windows.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <stack>
#include <string>
#include <vector>

#pragma comment(lib, "opengl32.lib")
#pragma comment(lib, "glu32.lib")

using namespace Gdiplus;

// ============================================================================
// COLOR PARSING
// ============================================================================

struct RGBA { float r, g, b, a; };

namespace detail {

inline RGBA parseHex(const std::string &s) {
    std::string h = s;
    if (!h.empty() && h[0] == '#') h = h.substr(1);
    auto hex2f = [](unsigned v) { return v / 255.0f; };
    unsigned n = (unsigned)std::stoul(h, nullptr, 16);
    if (h.size() == 3) {
        unsigned r=(n>>8)&0xF, g=(n>>4)&0xF, b=n&0xF;
        return {hex2f(r|(r<<4)), hex2f(g|(g<<4)), hex2f(b|(b<<4)), 1.f};
    }
    if (h.size() == 4) {
        unsigned r=(n>>12)&0xF, g=(n>>8)&0xF, b=(n>>4)&0xF, a=n&0xF;
        return {hex2f(r|(r<<4)), hex2f(g|(g<<4)), hex2f(b|(b<<4)), hex2f(a|(a<<4))};
    }
    if (h.size() == 6)
        return {hex2f((n>>16)&0xFF), hex2f((n>>8)&0xFF), hex2f(n&0xFF), 1.f};
    return {hex2f((n>>24)&0xFF), hex2f((n>>16)&0xFF), hex2f((n>>8)&0xFF), hex2f(n&0xFF)};
}

inline RGBA parseColor(const std::string &css) {
    if (css.empty()) return {1,1,1,1};
    if (css[0] == '#') return parseHex(css);
    if (css.substr(0,3) == "rgb") {
        auto a = css.find('('), b = css.find(')');
        if (a == std::string::npos) return {1,1,1,1};
        std::string inner = css.substr(a+1, b-a-1);
        float vals[4] = {0,0,0,1};
        int i = 0; size_t pos = 0;
        while (i < 4) {
            size_t comma = inner.find(',', pos);
            std::string tok = inner.substr(pos, comma==std::string::npos?std::string::npos:comma-pos);
            vals[i++] = std::stof(tok);
            if (comma == std::string::npos) break;
            pos = comma + 1;
        }
        return {vals[0]/255.f, vals[1]/255.f, vals[2]/255.f, vals[3]};
    }
    static const struct { const char *name; RGBA col; } kNamed[] = {
        {"black",{0,0,0,1}},{"white",{1,1,1,1}},{"red",{1,0,0,1}},
        {"green",{0,.5f,0,1}},{"blue",{0,0,1,1}},{"yellow",{1,1,0,1}},
        {"cyan",{0,1,1,1}},{"magenta",{1,0,1,1}},{"orange",{1,.647f,0,1}},
        {"purple",{.5f,0,.5f,1}},{"gray",{.5f,.5f,.5f,1}},{"grey",{.5f,.5f,.5f,1}},
        {"transparent",{0,0,0,0}},
    };
    std::string lo = css;
    for (auto &c : lo) c = (char)std::tolower((unsigned char)c);
    for (auto &e : kNamed) if (lo == e.name) return e.col;
    return {1,1,1,1};
}

} // namespace detail

// ============================================================================
// DRAW STATE
// ============================================================================

struct CanvasState {
    RGBA fillColor   = {0,0,0,1};
    RGBA strokeColor = {0,0,0,1};
    float lineW  = 1.f;
    float alpha  = 1.f;
    int lineCap  = 0;   // 0=butt  1=round  2=square
    int lineJoin = 0;   // 0=miter 1=round  2=bevel
    float m[9]   = {1,0,0, 0,1,0, 0,0,1};
    float fontSize = 16.f;
    std::string fontFamily = "Segoe UI";
};

// ============================================================================
// PATH COMMAND
// ============================================================================

struct PathCmd {
    enum Type { MoveTo, LineTo, QuadTo, BezierTo, ArcTo, Arc, Close } type;
    float x0,y0,x1,y1,x2,y2,r,r2,start,end;
    bool  ccw;
};

// ============================================================================
// CANVAS CONTEXT
// ============================================================================

class CanvasContext {
public:
    int W, H;
    HDC gdiDC;

    CanvasContext(int w, int h, HDC dc) : W(w), H(h), gdiDC(dc) {
        st_.push({});
        setupGL();
    }

    // ── State ────────────────────────────────────────────────────────────────
    CanvasContext &save()    { st_.push(st_.top()); return *this; }
    CanvasContext &restore() { if (st_.size()>1) st_.pop(); applyTransform(); return *this; }

    // ── Styles ───────────────────────────────────────────────────────────────
    CanvasContext &fillStyle  (const std::string &css) { st_.top().fillColor   = detail::parseColor(css); return *this; }
    CanvasContext &strokeStyle(const std::string &css) { st_.top().strokeColor = detail::parseColor(css); return *this; }
    CanvasContext &lineWidth  (float w)                { st_.top().lineW  = w; return *this; }
    CanvasContext &globalAlpha(float a)                { st_.top().alpha  = a; return *this; }
    CanvasContext &lineCap (const std::string &v) { auto &s=st_.top(); s.lineCap =(v=="round")?1:(v=="square")?2:0; return *this; }
    CanvasContext &lineJoin(const std::string &v) { auto &s=st_.top(); s.lineJoin=(v=="round")?1:(v=="bevel" )?2:0; return *this; }
    CanvasContext &font(const std::string &sizeStr, const std::string &family="") {
        auto &s = st_.top();
        try { s.fontSize = std::stof(sizeStr); } catch(...) {}
        if (!family.empty()) s.fontFamily = family;
        return *this;
    }

    // ── Transforms ───────────────────────────────────────────────────────────
    CanvasContext &translate(float tx, float ty) {
        float *m = st_.top().m;
        m[6] += tx*m[0]+ty*m[3];
        m[7] += tx*m[1]+ty*m[4];
        applyTransform(); return *this;
    }
    CanvasContext &scale(float sx, float sy) {
        float *m = st_.top().m;
        m[0]*=sx; m[1]*=sx; m[3]*=sy; m[4]*=sy;
        applyTransform(); return *this;
    }
    CanvasContext &rotate(float rad) {
        float c=std::cos(rad), s=std::sin(rad);
        float *m=st_.top().m, a=m[0],b=m[1],d=m[3],e=m[4];
        m[0]=a*c+d*s; m[1]=b*c+e*s; m[3]=-a*s+d*c; m[4]=-b*s+e*c;
        applyTransform(); return *this;
    }
    CanvasContext &resetTransform() {
        float *m=st_.top().m;
        m[0]=1;m[1]=0;m[2]=0; m[3]=0;m[4]=1;m[5]=0; m[6]=0;m[7]=0;m[8]=1;
        applyTransform(); return *this;
    }

    // ── Path ─────────────────────────────────────────────────────────────────
    CanvasContext &beginPath() { path_.clear(); return *this; }
    CanvasContext &closePath() { path_.push_back({PathCmd::Close}); return *this; }
    CanvasContext &moveTo(float x, float y) { path_.push_back({PathCmd::MoveTo,x,y}); return *this; }
    CanvasContext &lineTo(float x, float y) { path_.push_back({PathCmd::LineTo,x,y}); return *this; }

    CanvasContext &quadraticCurveTo(float cpx, float cpy, float x, float y) {
        PathCmd c{}; c.type=PathCmd::QuadTo; c.x0=cpx; c.y0=cpy; c.x1=x; c.y1=y;
        path_.push_back(c); return *this;
    }
    CanvasContext &bezierCurveTo(float cp1x,float cp1y,float cp2x,float cp2y,float x,float y) {
        PathCmd c{}; c.type=PathCmd::BezierTo;
        c.x0=cp1x; c.y0=cp1y; c.x1=cp2x; c.y1=cp2y; c.x2=x; c.y2=y;
        path_.push_back(c); return *this;
    }
    CanvasContext &arc(float x,float y,float r,float sa,float ea,bool ccw=false) {
        PathCmd c{}; c.type=PathCmd::Arc;
        c.x0=x; c.y0=y; c.r=r; c.start=sa; c.end=ea; c.ccw=ccw;
        path_.push_back(c); return *this;
    }
    CanvasContext &rect(float x,float y,float w,float h) {
        moveTo(x,y); lineTo(x+w,y); lineTo(x+w,y+h); lineTo(x,y+h); closePath();
        return *this;
    }
    CanvasContext &roundRect(float x,float y,float w,float h,float r) {
        r=min(r,min(w,h)*0.5f);
        moveTo(x+r,y); lineTo(x+w-r,y);
        arc(x+w-r,y+r,  r,-M_PI_2_F,0);
        lineTo(x+w,y+h-r);
        arc(x+w-r,y+h-r,r,0,M_PI_2_F);
        lineTo(x+r,y+h);
        arc(x+r,y+h-r,  r,M_PI_2_F,(float)M_PI);
        lineTo(x,y+r);
        arc(x+r,y+r,    r,(float)M_PI,-M_PI_2_F);
        closePath(); return *this;
    }
    CanvasContext &ellipse(float cx,float cy,float rx,float ry,float rotation,float sa,float ea) {
        int segs=64; float da=(ea-sa)/segs;
        float cosR=std::cos(rotation), sinR=std::sin(rotation); bool first=true;
        for (int i=0;i<=segs;i++) {
            float a=sa+i*da, ex=rx*std::cos(a), ey=ry*std::sin(a);
            float px=cx+ex*cosR-ey*sinR, py=cy+ex*sinR+ey*cosR;
            if (first){moveTo(px,py);first=false;} else lineTo(px,py);
        }
        return *this;
    }

    // ── Fill / Stroke ─────────────────────────────────────────────────────────
    CanvasContext &fill()   { setColor(st_.top().fillColor);  rasterisePath(true);  return *this; }
    CanvasContext &stroke() { glLineWidth(st_.top().lineW); setColor(st_.top().strokeColor); rasterisePath(false); return *this; }

    // ── Rect shortcuts ────────────────────────────────────────────────────────
    CanvasContext &fillRect(float x,float y,float w,float h) {
        setColor(st_.top().fillColor);
        glBegin(GL_QUADS); emitV(x,y);emitV(x+w,y);emitV(x+w,y+h);emitV(x,y+h); glEnd();
        return *this;
    }
    CanvasContext &strokeRect(float x,float y,float w,float h) {
        glLineWidth(st_.top().lineW); setColor(st_.top().strokeColor);
        glBegin(GL_LINE_LOOP); emitV(x,y);emitV(x+w,y);emitV(x+w,y+h);emitV(x,y+h); glEnd();
        return *this;
    }
    CanvasContext &clearRect(float x,float y,float w,float h) {
        glScissor((GLint)x,H-(GLint)(y+h),(GLsizei)w,(GLsizei)h);
        glEnable(GL_SCISSOR_TEST); glClear(GL_COLOR_BUFFER_BIT); glDisable(GL_SCISSOR_TEST);
        return *this;
    }

    // ── Text ──────────────────────────────────────────────────────────────────
    CanvasContext &fillText  (const std::string &t,float x,float y,float mw=-1.f){drawText(t,x,y,true);  return *this;}
    CanvasContext &strokeText(const std::string &t,float x,float y,float mw=-1.f){drawText(t,x,y,false); return *this;}
    float measureText(const std::string &text) {
        auto tt=makeTextTex(text,false); if(tt.id) glDeleteTextures(1,&tt.id); return (float)tt.w;
    }

    // ── Convenience ───────────────────────────────────────────────────────────
    CanvasContext &drawLine(float x1,float y1,float x2,float y2) {
        glLineWidth(st_.top().lineW); setColor(st_.top().strokeColor);
        glBegin(GL_LINES); emitV(x1,y1); emitV(x2,y2); glEnd(); return *this;
    }
    CanvasContext &fillCircle  (float cx,float cy,float r){beginPath();arc(cx,cy,r,0,(float)(2*M_PI));fill();  return *this;}
    CanvasContext &strokeCircle(float cx,float cy,float r){beginPath();arc(cx,cy,r,0,(float)(2*M_PI));stroke();return *this;}

private:
    std::stack<CanvasState> st_;
    std::vector<PathCmd>    path_;
    static constexpr double M_PI    = 3.14159265358979323846;
    static constexpr float  M_PI_2_F= 1.5707963f;

    void setupGL() {
        glMatrixMode(GL_PROJECTION); glLoadIdentity();
        glOrtho(0,W,H,0,-1,1);
        glMatrixMode(GL_MODELVIEW); glLoadIdentity();
        glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_LINE_SMOOTH); glHint(GL_LINE_SMOOTH_HINT,GL_NICEST);
        glEnable(GL_POINT_SMOOTH);
    }
    void applyTransform() {
        float *m=st_.top().m;
        GLfloat mat[16]={m[0],m[1],0,0, m[3],m[4],0,0, 0,0,1,0, m[6],m[7],0,1};
        glMatrixMode(GL_MODELVIEW); glLoadMatrixf(mat);
    }
    void setColor(const RGBA &c) { glColor4f(c.r,c.g,c.b,c.a*st_.top().alpha); }
    void emitV(float px,float py) { glVertex2f(px,py); }

    struct FlatVert { float x,y; };
    std::vector<FlatVert> tessPath() {
        std::vector<FlatVert> verts;
        float cx=0,cy=0,sx=0,sy=0;
        for (auto &cmd : path_) {
            switch(cmd.type) {
            case PathCmd::MoveTo:
                cx=cmd.x0; cy=cmd.y0; sx=cx; sy=cy;
                verts.push_back({-1e9f,-1e9f}); verts.push_back({cx,cy}); break;
            case PathCmd::LineTo:
                cx=cmd.x0; cy=cmd.y0; verts.push_back({cx,cy}); break;
            case PathCmd::QuadTo: {
                float x0=cx,y0=cy,x1=cmd.x0,y1=cmd.y0,x2=cmd.x1,y2=cmd.y1;
                const int N=16;
                for(int i=1;i<=N;i++){float t=(float)i/N,it=1-t;
                    verts.push_back({it*it*x0+2*it*t*x1+t*t*x2, it*it*y0+2*it*t*y1+t*t*y2});}
                cx=x2; cy=y2; break;
            }
            case PathCmd::BezierTo: {
                float x0=cx,y0=cy,x1=cmd.x0,y1=cmd.y0,x2=cmd.x1,y2=cmd.y1,x3=cmd.x2,y3=cmd.y2;
                const int N=32;
                for(int i=1;i<=N;i++){float t=(float)i/N,it=1-t;
                    verts.push_back({it*it*it*x0+3*it*it*t*x1+3*it*t*t*x2+t*t*t*x3,
                                     it*it*it*y0+3*it*it*t*y1+3*it*t*t*y2+t*t*t*y3});}
                cx=x3; cy=y3; break;
            }
            case PathCmd::Arc: {
                float as=cmd.start,ae=cmd.end;
                if(cmd.ccw){while(ae>as)ae-=(float)(2*M_PI);}
                else       {while(ae<as)ae+=(float)(2*M_PI);}
                float sw=ae-as; int N=max(8,(int)(std::abs(sw)*cmd.r/2)); N=min(N,256);
                for(int i=0;i<=N;i++){float a=as+sw*((float)i/N);
                    verts.push_back({cmd.x0+cmd.r*std::cos(a), cmd.y0+cmd.r*std::sin(a)});}
                cx=cmd.x0+cmd.r*std::cos(ae); cy=cmd.y0+cmd.r*std::sin(ae); break;
            }
            case PathCmd::Close: verts.push_back({sx,sy}); cx=sx; cy=sy; break;
            default: break;
            }
        }
        return verts;
    }

    void rasterisePath(bool doFill) {
        auto verts=tessPath(); if(verts.empty()) return;
        std::vector<std::vector<FlatVert>> subs;
        std::vector<FlatVert> cur;
        for(auto &v:verts){
            if(v.x<-1e8f){if(!cur.empty())subs.push_back(std::move(cur));cur.clear();}
            else cur.push_back(v);
        }
        if(!cur.empty()) subs.push_back(std::move(cur));
        if(doFill){
            for(auto &sp:subs){if(sp.size()<3)continue;glBegin(GL_TRIANGLE_FAN);for(auto &v:sp)glVertex2f(v.x,v.y);glEnd();}
        } else {
            for(auto &sp:subs){glBegin(GL_LINE_STRIP);for(auto &v:sp)glVertex2f(v.x,v.y);glEnd();}
        }
    }

    struct TextTex { GLuint id=0; int w=0,h=0; };
    TextTex makeTextTex(const std::string &text,bool filled) {
        if(text.empty()) return {};
        auto &s=st_.top();
        int wlen=MultiByteToWideChar(CP_UTF8,0,text.c_str(),-1,nullptr,0);
        std::wstring wt(wlen,0); MultiByteToWideChar(CP_UTF8,0,text.c_str(),-1,wt.data(),wlen);
        int wflen=MultiByteToWideChar(CP_UTF8,0,s.fontFamily.c_str(),-1,nullptr,0);
        std::wstring wf(wflen,0); MultiByteToWideChar(CP_UTF8,0,s.fontFamily.c_str(),-1,wf.data(),wflen);
        Gdiplus::Bitmap measure(1,1,PixelFormat32bppARGB);
        Gdiplus::Graphics gm(&measure);
        Gdiplus::Font font(wf.c_str(),s.fontSize,Gdiplus::FontStyleRegular,Gdiplus::UnitPixel);
        Gdiplus::RectF lay(0,0,4000,4000),bounds;
        gm.MeasureString(wt.c_str(),-1,&font,lay,&bounds);
        int tw=max(1,(int)std::ceil(bounds.Width)+2), th=max(1,(int)std::ceil(bounds.Height)+2);
        Gdiplus::Bitmap bmp(tw,th,PixelFormat32bppARGB);
        Gdiplus::Graphics g(&bmp);
        g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);
        g.Clear(Gdiplus::Color(0,0,0,0));
        const RGBA &col=filled?s.fillColor:s.strokeColor;
        BYTE cr=(BYTE)(col.r*255),cg=(BYTE)(col.g*255),cb=(BYTE)(col.b*255),ca=(BYTE)(col.a*s.alpha*255);
        Gdiplus::SolidBrush brush(Gdiplus::Color(ca,cr,cg,cb));
        g.DrawString(wt.c_str(),-1,&font,Gdiplus::PointF(1,1),&brush);
        Gdiplus::BitmapData bd; Gdiplus::Rect r(0,0,tw,th);
        bmp.LockBits(&r,Gdiplus::ImageLockModeRead,PixelFormat32bppARGB,&bd);
        std::vector<BYTE> rgba(tw*th*4);
        for(int row=0;row<th;row++){
            const BYTE *src=(const BYTE*)bd.Scan0+row*bd.Stride;
            BYTE *dst=rgba.data()+row*tw*4;
            for(int col=0;col<tw;col++,src+=4,dst+=4){dst[0]=src[2];dst[1]=src[1];dst[2]=src[0];dst[3]=src[3];}
        }
        bmp.UnlockBits(&bd);
        GLuint tex=0; glGenTextures(1,&tex); glBindTexture(GL_TEXTURE_2D,tex);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,tw,th,0,GL_RGBA,GL_UNSIGNED_BYTE,rgba.data());
        glBindTexture(GL_TEXTURE_2D,0);
        return {tex,tw,th};
    }

    void drawText(const std::string &text,float x,float y,bool filled) {
        auto tt=makeTextTex(text,filled); if(!tt.id) return;
        float x1=x,y1=y-tt.h,x2=x+tt.w,y2=y;
        glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();
        glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D,tt.id); glColor4f(1,1,1,1);
        glBegin(GL_QUADS);
        glTexCoord2f(0,0); glVertex2f(x1,y1); glTexCoord2f(1,0); glVertex2f(x2,y1);
        glTexCoord2f(1,1); glVertex2f(x2,y2); glTexCoord2f(0,1); glVertex2f(x1,y2);
        glEnd();
        glDisable(GL_TEXTURE_2D); glDeleteTextures(1,&tt.id); glPopMatrix();
    }
};

// ============================================================================
// CANVAS WIDGET
// ============================================================================

class CanvasWidget : public Widget {
public:
    // Callback types
    using DrawFn    = std::function<void(CanvasContext &)>;
    using MouseFn   = std::function<void(int x, int y)>;
    using WheelFn   = std::function<void(int x, int y, int delta)>;

    CanvasWidget() { autoWidth=false; autoHeight=false; width=400; height=300; }
    ~CanvasWidget() { destroyGL(); }

    // ── Fluent draw / size ────────────────────────────────────────────────────
    std::shared_ptr<CanvasWidget> onDraw(DrawFn fn) {
        drawFn=std::move(fn); markNeedsPaint();
        return std::static_pointer_cast<CanvasWidget>(shared_from_this());
    }
    std::shared_ptr<CanvasWidget> setSize(int w,int h) {
        width=w; height=h; autoWidth=autoHeight=false; markNeedsLayout();
        return std::static_pointer_cast<CanvasWidget>(shared_from_this());
    }
    std::shared_ptr<CanvasWidget> redraw() {
        markNeedsPaint();
        return std::static_pointer_cast<CanvasWidget>(shared_from_this());
    }

    // ── Mouse event registration ──────────────────────────────────────────────
    //
    // onMouseDown  — fired when a mouse button is pressed inside the canvas.
    // onMouseMove  — fired whenever the cursor moves over the canvas.
    // onMouseUp    — fired when a mouse button is released (even if cursor left
    //                the canvas while the button was held, thanks to SetCapture).
    // onMouseEnter — fired the first time the cursor enters the canvas area.
    // onMouseLeave — fired when the cursor exits the canvas area.
    // onMouseWheel — fired on scroll wheel; delta>0 = up, delta<0 = down.
    //
    // All x/y values are canvas-local pixels (top-left = 0,0).

    std::shared_ptr<CanvasWidget> onMouseDown (MouseFn fn) { mouseDownFn  = std::move(fn); return ptr(); }
    std::shared_ptr<CanvasWidget> onMouseMove (MouseFn fn) { mouseMoveFn  = std::move(fn); return ptr(); }
    std::shared_ptr<CanvasWidget> onMouseUp   (MouseFn fn) { mouseUpFn    = std::move(fn); return ptr(); }
    std::shared_ptr<CanvasWidget> onMouseEnter(MouseFn fn) { mouseEnterFn = std::move(fn); return ptr(); }
    std::shared_ptr<CanvasWidget> onMouseLeave(MouseFn fn) { mouseLeaveFn = std::move(fn); return ptr(); }
    std::shared_ptr<CanvasWidget> onMouseWheel(WheelFn fn) { mouseWheelFn = std::move(fn); return ptr(); }

    // ── Widget overrides ──────────────────────────────────────────────────────
    void computeLayout(HDC hdc, const BoxConstraints &constraints, FontCache &fontCache) override {
        width  = constraints.clampWidth (autoWidth  ? constraints.maxWidth  : width);
        height = constraints.clampHeight(autoHeight ? constraints.maxHeight : height);
        needsLayout = false;
    }
    void render(HDC hdc, FontCache &) override {
        ensureChildWindow(hdc);
        moveChildWindow();
        renderGL();
        needsPaint = false;
    }
    void onDetach() override { destroyGL(); Widget::onDetach(); }

private:
    std::shared_ptr<CanvasWidget> ptr() {
        return std::static_pointer_cast<CanvasWidget>(shared_from_this());
    }

    // callbacks
    DrawFn   drawFn;
    MouseFn  mouseDownFn;
    MouseFn  mouseMoveFn;
    MouseFn  mouseUpFn;
    MouseFn  mouseEnterFn;
    MouseFn  mouseLeaveFn;
    WheelFn  mouseWheelFn;

    // hover / leave tracking
    bool mouseInside_   = false;
    bool trackingLeave_ = false;

    HWND  childHwnd  = nullptr;
    HDC   glDC       = nullptr;
    HGLRC glRC       = nullptr;
    HWND  parentHwnd = nullptr;

    // ── Win32 child window ────────────────────────────────────────────────────
    //
    // We store `this` in GWLP_USERDATA so the static ChildProc can dispatch
    // mouse events to the correct CanvasWidget instance without any global map.
    //
    static LRESULT CALLBACK ChildProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        CanvasWidget *self =
            reinterpret_cast<CanvasWidget *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

        switch (msg) {

        case WM_ERASEBKGND: return 1;

        case WM_PAINT: {
            PAINTSTRUCT ps; BeginPaint(hwnd,&ps); EndPaint(hwnd,&ps); return 0;
        }

        // ── Button down ───────────────────────────────────────────────────────
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN:
            SetCapture(hwnd);   // keep receiving events even after cursor leaves
            if (self && self->mouseDownFn)
                self->mouseDownFn(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
            forwardToParent(hwnd, msg, wp, lp);
            return 0;

        // ── Button up ─────────────────────────────────────────────────────────
        case WM_LBUTTONUP:
        case WM_RBUTTONUP:
        case WM_MBUTTONUP:
            ReleaseCapture();
            if (self && self->mouseUpFn)
                self->mouseUpFn(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
            forwardToParent(hwnd, msg, wp, lp);
            return 0;

        // ── Mouse move ────────────────────────────────────────────────────────
        case WM_MOUSEMOVE: {
            int mx = GET_X_LPARAM(lp), my = GET_Y_LPARAM(lp);
            if (self) {
                // Subscribe to WM_MOUSELEAVE (must be renewed after each leave)
                if (!self->trackingLeave_) {
                    TRACKMOUSEEVENT tme{};
                    tme.cbSize    = sizeof(tme);
                    tme.dwFlags   = TME_LEAVE;
                    tme.hwndTrack = hwnd;
                    TrackMouseEvent(&tme);
                    self->trackingLeave_ = true;
                }
                if (!self->mouseInside_) {
                    self->mouseInside_ = true;
                    if (self->mouseEnterFn) self->mouseEnterFn(mx, my);
                }
                if (self->mouseMoveFn) self->mouseMoveFn(mx, my);
            }
            forwardToParent(hwnd, msg, wp, lp);
            return 0;
        }

        // ── Mouse leave ───────────────────────────────────────────────────────
        case WM_MOUSELEAVE:
            if (self) {
                self->trackingLeave_ = false;
                self->mouseInside_   = false;
                if (self->mouseLeaveFn) self->mouseLeaveFn(0, 0);
            }
            return 0;

        // ── Scroll wheel ──────────────────────────────────────────────────────
        case WM_MOUSEWHEEL: {
            int delta = GET_WHEEL_DELTA_WPARAM(wp);   // +120 per notch up, -120 per notch down
            // lp carries screen coords for WM_MOUSEWHEEL; convert to client space
            POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ScreenToClient(hwnd, &pt);
            if (self && self->mouseWheelFn)
                self->mouseWheelFn(pt.x, pt.y, delta);
            forwardToParent(hwnd, msg, wp, lp);
            return 0;
        }

        default:
            return DefWindowProc(hwnd, msg, wp, lp);
        }
    }

    static void forwardToParent(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        HWND parent = GetParent(hwnd);
        if (!parent) return;
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        MapWindowPoints(hwnd, parent, &pt, 1);
        PostMessage(parent, msg, wp, MAKELPARAM(pt.x, pt.y));
    }

    void registerChildClass() {
        static bool done = false;
        if (done) return;
        WNDCLASSEXW wc = {};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = ChildProc;
        wc.hInstance     = GetModuleHandle(nullptr);
        wc.lpszClassName = L"FluxGLCanvas";
        wc.style         = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
        RegisterClassExW(&wc);
        done = true;
    }

    void ensureChildWindow(HDC parentDC) {
        if (childHwnd) return;
        HWND owner = WindowFromDC(parentDC);
        if (!owner) owner = GetActiveWindow();
        parentHwnd = owner;
        registerChildClass();
        childHwnd = CreateWindowExW(
            WS_EX_NOPARENTNOTIFY, L"FluxGLCanvas", nullptr,
            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
            x, y, width, height,
            owner, nullptr, GetModuleHandle(nullptr), nullptr);
        if (!childHwnd) return;

        // Bind this CanvasWidget to its HWND so ChildProc can dispatch events.
        SetWindowLongPtrW(childHwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(this));

        glDC = GetDC(childHwnd);
        setupPixelFormat(glDC);
        glRC = wglCreateContext(glDC);
        wglMakeCurrent(glDC, glRC);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }

    void setupPixelFormat(HDC dc) {
        PIXELFORMATDESCRIPTOR pfd = {};
        pfd.nSize      = sizeof(pfd);
        pfd.nVersion   = 1;
        pfd.dwFlags    = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
        pfd.iPixelType = PFD_TYPE_RGBA;
        pfd.cColorBits = 32;
        pfd.cDepthBits = 24;
        pfd.iLayerType = PFD_MAIN_PLANE;
        int fmt = ChoosePixelFormat(dc, &pfd);
        SetPixelFormat(dc, fmt, &pfd);
    }

    void moveChildWindow() {
        if (!childHwnd) return;
        SetWindowPos(childHwnd, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
    }

    void renderGL() {
        if (!glRC || !glDC) return;
        if (wglGetCurrentContext() != glRC) wglMakeCurrent(glDC, glRC);
        glViewport(0, 0, width, height);
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        if (drawFn) { CanvasContext ctx(width, height, glDC); drawFn(ctx); }
        SwapBuffers(glDC);
    }

    void destroyGL() {
        if (glRC) { wglMakeCurrent(nullptr,nullptr); wglDeleteContext(glRC); glRC=nullptr; }
        if (childHwnd && glDC) { ReleaseDC(childHwnd,glDC); glDC=nullptr; }
        if (childHwnd) { DestroyWindow(childHwnd); childHwnd=nullptr; }
    }
};

// ============================================================================
// FACTORY
// ============================================================================

using CanvasPtr = std::shared_ptr<CanvasWidget>;

inline CanvasPtr Canvas()                                            { return std::make_shared<CanvasWidget>(); }
inline CanvasPtr Canvas(int w, int h)                                { return std::make_shared<CanvasWidget>()->setSize(w,h); }
inline CanvasPtr Canvas(int w, int h, CanvasWidget::DrawFn fn)       { return std::make_shared<CanvasWidget>()->setSize(w,h)->onDraw(std::move(fn)); }

#endif // FLUX_CANVAS_HPP

// ============================================================================
// USAGE EXAMPLES
// ============================================================================
/*

// ── Drag a dot (the originally commented-out demo, now working) ──────────────
State<int>  dotX(300, context), dotY(200, context);
State<bool> dragging(false, context);

auto canvas = Canvas(700, 480)
    ->onDraw([&](CanvasContext& ctx) {
        int dx = dotX.get(), dy = dotY.get();
        ctx.fillStyle("#0d1117").fillRect(0,0,700,480);

        // crosshairs
        ctx.strokeStyle("#30363d").lineWidth(1);
        ctx.beginPath().moveTo(dx,0).lineTo(dx,480).stroke();
        ctx.beginPath().moveTo(0,dy).lineTo(700,dy).stroke();

        // glow ring
        ctx.strokeStyle("rgba(88,166,255,0.25)").lineWidth(14).strokeCircle(dx,dy,18);
        // dot
        ctx.fillStyle("#58a6ff").fillCircle(dx,dy,10);
        ctx.strokeStyle("#cae8ff").lineWidth(2).strokeCircle(dx,dy,10);
    })
    ->onMouseDown([&](int mx, int my) {
        int dx=dotX.get(), dy=dotY.get();
        if (std::abs(mx-dx)<14 && std::abs(my-dy)<14)
            dragging.set(true);
    })
    ->onMouseMove([&](int mx, int my) {
        if (!dragging.get()) return;
        dotX.set(std::clamp(mx, 0, 700));
        dotY.set(std::clamp(my, 0, 480));
    })
    ->onMouseUp([&](int, int) {
        dragging.set(false);
    });


// ── Zoom with the scroll wheel ────────────────────────────────────────────────
State<float> zoom(1.f, context);

auto canvas = Canvas(600, 400)
    ->onDraw([&](CanvasContext& ctx) {
        float z = zoom.get();
        ctx.fillStyle("#111").fillRect(0,0,600,400);
        ctx.save()
           .translate(300,200).scale(z,z).translate(-300,-200)
           .fillStyle("#3fb950").fillRect(250,150,100,100)
           .restore();
    })
    ->onMouseWheel([&](int, int, int delta) {
        float z = zoom.get() * (delta > 0 ? 1.1f : 0.9f);
        zoom.set(std::clamp(z, 0.1f, 10.f));
    });


// ── Cursor change on hover ────────────────────────────────────────────────────
auto canvas = Canvas(400,300)
    ->onMouseEnter([](int,int){ SetCursor(LoadCursor(nullptr, IDC_HAND));  })
    ->onMouseLeave([](int,int){ SetCursor(LoadCursor(nullptr, IDC_ARROW)); });

*/