// flux/platform/win32/flux_canvas_win32.hpp
#pragma once
#ifndef _WIN32
  #error "flux_canvas_win32.hpp must only be compiled on Win32"
#endif

// ============================================================================
// CanvasWidget — Win32 / WGL / pure-GL 
// ============================================================================

#include "../../flux_platform.hpp"
#include "../../flux_widget.hpp"
#include "../../flux_keys.hpp"
#include "../../flux_core.hpp"
#include "../../flux_canvas2d.hpp"
#include "../../flux_scrollbar.hpp"
#include "../../flux_render_surface.hpp"
#include "../../flux_canvas_types.hpp"
#include "../../flux_glutil.hpp"
#include "flux_wgl.hpp"

#include <glad/glad.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// ── Scrollbar GL shader ───────────────────────────────────────────────────────

static const char* kSBVert = R"GLSL(
#version 330 core
layout(location=0) in vec2 aPos;
uniform mat4 uMVP;
void main(){ gl_Position = uMVP * vec4(aPos, 0.0, 1.0); }
)GLSL";

static const char* kSBFrag = R"GLSL(
#version 330 core
out vec4 fragColor;
uniform vec4 uColor;
void main(){ fragColor = uColor; }
)GLSL";

class CanvasWidget : public Widget {
public:
    static constexpr float kSBThick = CustomScrollbar::kTrackThick;

    explicit CanvasWidget()
        : hBar_(CustomScrollbar::Axis::Horizontal),
          vBar_(CustomScrollbar::Axis::Vertical)
    {
        autoWidth = autoHeight = false;
        width  = 400;
        height = 300;
    }

    ~CanvasWidget() { destroyGL(); }

    // ── Configuration ─────────────────────────────────────────────────────────

    std::shared_ptr<CanvasWidget> setViewportEnabled(bool e) {
        viewportEnabled_ = e;
        return ptr();
    }
    std::shared_ptr<CanvasWidget> setScrollbarsEnabled(bool e) {
        scrollbarsEnabled_ = e;
        if (glHwnd_) { updateViewportSize(lastGLW_, lastGLH_); scheduleRepaint(); }
        return ptr();
    }
    bool scrollbarsEnabled() const { return scrollbarsEnabled_; }

    template<typename T, typename... A>
    std::shared_ptr<T> setSurface(A&&... a) {
        auto s = std::make_shared<T>(std::forward<A>(a)...);
        pendingSurface_ = s;
        return s;
    }

    RenderSurface*   getSurface()  const { return activeSurface_.get(); }
    const Viewport&  viewport()    const { return vp_; }
    Viewport&        viewport()          { return vp_; }

    std::shared_ptr<CanvasWidget> setSize(int w, int h) {
        width = w; height = h;
        autoWidth = autoHeight = false;
        markNeedsLayout();
        return ptr();
    }
    std::shared_ptr<CanvasWidget> setCanvasSize(int w, int h) {
        canvasW_ = w; canvasH_ = h;
        if (glHwnd_) {
            vp_.setCanvasSize(w, h);
            if (activeSurface_) {
                makeCurrent();
                activeSurface_->resize(w, h);
            }
        }
        return ptr();
    }
    std::shared_ptr<CanvasWidget> redraw() { markNeedsPaint(); return ptr(); }

    // ── Callbacks ─────────────────────────────────────────────────────────────
    std::function<void(float zoom)>  onViewportChanged;
    std::function<void(int w, int h)> onGLResize;

    // ── Widget overrides ──────────────────────────────────────────────────────

    void computeLayout(GraphicsContext& /*ctx*/,
                       const BoxConstraints& c, FontCache&) override {
        width  = c.clampWidth (autoWidth  ? c.maxWidth  : width);
        height = c.clampHeight(autoHeight ? c.maxHeight : height);
        needsLayout = false;
    }

    void render(GraphicsContext& ctx, FontCache&) override {
        ensureWindows(ctx);
        moveWindows();
        tickAndRender();
        needsPaint = false;
    }

    void onDetach() override {
        destroyGL();
        Widget::onDetach();
    }

    void markNeedsPaint() override {
        Widget::markNeedsPaint();
        if (glHwnd_ && !repaintPending_) {
            repaintPending_ = true;
            PostMessage(glHwnd_, WM_USER + 1, 0, 0);
        }
    }

private:
    // ── State ─────────────────────────────────────────────────────────────────
    bool viewportEnabled_  = true;
    bool scrollbarsEnabled_= true;

    std::shared_ptr<CanvasWidget> ptr() {
        return std::static_pointer_cast<CanvasWidget>(shared_from_this());
    }

    std::shared_ptr<RenderSurface> activeSurface_, pendingSurface_;
    Viewport   vp_;
    int        canvasW_ = 512, canvasH_ = 512;

    CustomScrollbar hBar_, vBar_;

    using Clock = std::chrono::steady_clock;
    Clock::time_point lastTick_ = Clock::now();

    HWND  frameHwnd_  = nullptr;
    HWND  glHwnd_     = nullptr;
    HDC   glDC_       = nullptr;
    HGLRC glRC_       = nullptr;
    HWND  parentHwnd_ = nullptr;

    // ── Canvas2D GL context ───────────────────────────────────────────────────
    Canvas2DGL* canvasGL_ = nullptr;

    // ── Scrollbar GL program ──────────────────────────────────────────────────
    GLuint sbProg_  = 0;
    GLint  sbMVP_   = -1;
    GLint  sbColor_ = -1;
    GLuint sbVAO_   = 0;
    GLuint sbVBO_   = 0;

    bool repaintPending_ = false;
    bool trackingLeave_  = false;
    int  lastGLW_ = 0, lastGLH_ = 0;

    bool  panning_    = false;
    int   panStartSX_ = 0, panStartSY_ = 0;
    float panStartOX_ = 0, panStartOY_ = 0;

    // ── GL helpers ────────────────────────────────────────────────────────────

    void makeCurrent() {
        if (wglGetCurrentContext() != glRC_)
            wglMakeCurrent(glDC_, glRC_);
    }

    // ── Viewport / scrollbar geometry ─────────────────────────────────────────

    void viewportDims(int glW, int glH, int& vpW, int& vpH) const {
        if (!viewportEnabled_ || !scrollbarsEnabled_) { vpW=glW; vpH=glH; return; }
        ScrollbarInfo h = vp_.scrollbarH(), v = vp_.scrollbarV();
        vpW = glW - (v.visible ? (int)kSBThick : 0);
        vpH = glH - (h.visible ? (int)kSBThick : 0);
        if (vpW<1) vpW=1;
        if (vpH<1) vpH=1;
    }

    void updateViewportSize(int glW, int glH) {
        int vpW, vpH;
        viewportDims(glW, glH, vpW, vpH);
        vp_.setViewSize(vpW, vpH);
    }

    void updateSBGeometry(int glW, int glH) {
        ScrollbarInfo h = vp_.scrollbarH(), v = vp_.scrollbarV();
        float hLen = float(glW) - (v.visible ? kSBThick : 0.f);
        hBar_.setGeometry(0.f, float(glH)-kSBThick, hLen);
        hBar_.setThumb(h.thumbMin, h.thumbMax,
                       h.visible && scrollbarsEnabled_ && viewportEnabled_);
        float vLen = float(glH) - (h.visible ? kSBThick : 0.f);
        vBar_.setGeometry(float(glW)-kSBThick, 0.f, vLen);
        vBar_.setThumb(v.thumbMin, v.thumbMax,
                       v.visible && scrollbarsEnabled_ && viewportEnabled_);
    }

    // ── Pan ───────────────────────────────────────────────────────────────────

    void beginPan(int sx, int sy) {
        panning_ = true;
        panStartSX_=sx; panStartSY_=sy;
        panStartOX_=vp_.offsetX(); panStartOY_=vp_.offsetY();
    }
    void continuePan(int sx, int sy) {
        float dx=float(sx-panStartSX_), dy=float(sy-panStartSY_);
        vp_.setOffset(panStartOX_ - dx/vp_.zoom(),
                      panStartOY_ + dy/vp_.zoom());
        pokeScrollbars();
        scheduleRepaint();
    }
    void pokeScrollbars() { hBar_.poke(); vBar_.poke(); }

    void scheduleRepaint() {
        if (onViewportChanged) onViewportChanged(vp_.zoom());
        if (!repaintPending_) {
            repaintPending_ = true;
            PostMessage(glHwnd_, WM_USER+1, 0, 0);
        }
    }

    // ── Scrollbar callbacks ───────────────────────────────────────────────────

    void applyHScrollFraction(float thumbMin) {
        float span  = vp_.scrollbarH().thumbMax - vp_.scrollbarH().thumbMin;
        float range = vp_.canvasW() - vp_.viewW()/vp_.zoom();
        if (range<=0.f) return;
        vp_.setOffsetX(thumbMin/(1.f-span)*range);
        pokeScrollbars(); scheduleRepaint();
    }
    void applyVScrollFraction(float thumbMin) {
        float span  = vp_.scrollbarV().thumbMax - vp_.scrollbarV().thumbMin;
        float range = vp_.canvasH() - vp_.viewH()/vp_.zoom();
        if (range<=0.f) return;
        float t = 1.f - thumbMin - span;
        vp_.setOffsetY(t/(1.f-span)*range);
        pokeScrollbars(); scheduleRepaint();
    }

    // ── GL scrollbar rendering ────────────────────────────────────────────────

    void renderScrollbarsGL(int glW, int glH, double dt) {
        bool hFade = hBar_.tick(dt);
        bool vFade = vBar_.tick(dt);

        if (!hBar_.needsRedraw() && !vBar_.needsRedraw()) {
            if ((hFade||vFade) && !repaintPending_) {
                repaintPending_ = true;
                SetTimer(glHwnd_, 1, 16, nullptr);
            }
            return;
        }

        // Enable alpha blending for scrollbars
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        hBar_.render(sbProg_, sbVAO_, sbVBO_, sbMVP_, sbColor_, glW, glH);
        vBar_.render(sbProg_, sbVAO_, sbVBO_, sbMVP_, sbColor_, glW, glH);

        // Corner fill when both scrollbars visible
        if (hBar_.isVisible() && vBar_.isVisible() && scrollbarsEnabled_) {
            float cx_ = float(glW)-kSBThick;
            float cy_ = float(glH)-kSBThick;
            float mvp[16];
            glutil::ortho(0.f, float(glW), float(glH), 0.f, mvp);
            glUseProgram(sbProg_);
            glUniformMatrix4fv(sbMVP_,1,GL_FALSE,mvp);
            glUniform4f(sbColor_, 0.12f,0.12f,0.12f,0.35f);
            float v[]={
                cx_,       cy_,
                cx_+kSBThick, cy_,
                cx_+kSBThick, cy_+kSBThick,
                cx_+kSBThick, cy_+kSBThick,
                cx_,       cy_+kSBThick,
                cx_,       cy_
            };
            glBindVertexArray(sbVAO_);
            glBindBuffer(GL_ARRAY_BUFFER,sbVBO_);
            glBufferData(GL_ARRAY_BUFFER,sizeof(v),v,GL_DYNAMIC_DRAW);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,sizeof(float)*2,nullptr);
            glDisableVertexAttribArray(1);
            glDrawArrays(GL_TRIANGLES,0,6);
            glBindVertexArray(0);
        }

        if ((hFade||vFade) && !repaintPending_) {
            repaintPending_ = true;
            SetTimer(glHwnd_,1,16,nullptr);
        }
    }

    // ── Window procedures ─────────────────────────────────────────────────────

    static LRESULT CALLBACK FrameProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        auto* self = reinterpret_cast<CanvasWidget*>(
            GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        switch (msg) {
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps; BeginPaint(hwnd,&ps); EndPaint(hwnd,&ps); return 0;
        }
        case WM_MOUSEWHEEL:
            if (self && self->viewportEnabled_ && self->glHwnd_)
                PostMessage(self->glHwnd_,msg,wp,lp);
            return 0;
        default:
            return DefWindowProc(hwnd,msg,wp,lp);
        }
    }

    static LRESULT CALLBACK GLProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        auto* self = reinterpret_cast<CanvasWidget*>(
            GetWindowLongPtrW(hwnd, GWLP_USERDATA));

        switch (msg) {

        case WM_USER+1:
            if (self) { self->repaintPending_=false; self->tickAndRender(); }
            return 0;

        case WM_ERASEBKGND: return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps; BeginPaint(hwnd,&ps); EndPaint(hwnd,&ps); return 0;
        }

        case WM_MBUTTONDOWN:
            if (!self||!self->viewportEnabled_) return 0;
            SetCapture(hwnd);
            self->beginPan(GET_X_LPARAM(lp),GET_Y_LPARAM(lp));
            return 0;
        case WM_MBUTTONUP:
            if (!self||!self->viewportEnabled_) return 0;
            ReleaseCapture(); self->panning_=false; return 0;

        case WM_LBUTTONDOWN: {
            SetCapture(hwnd); SetFocus(hwnd);
            if (!self) return 0;
            int sx=GET_X_LPARAM(lp), sy=GET_Y_LPARAM(lp);
            bool hC=self->hBar_.onMouseDown(sx,sy,
                [self](float t){ self->applyHScrollFraction(t); });
            bool vC=!hC&&self->vBar_.onMouseDown(sx,sy,
                [self](float t){ self->applyVScrollFraction(t); });
            if (!hC&&!vC) {
                if (self->viewportEnabled_&&(GetKeyState(Key::Space)&0x8000)) {
                    self->beginPan(sx,sy);
                } else if (self->activeSurface_) {
                    auto [cx,cy]=self->vp_.screenToCanvas(float(sx),float(sy));
                    self->activeSurface_->onMouseDown(cx,cy);
                }
                forwardToParent(hwnd,msg,wp,lp);
            }
            self->scheduleRepaint(); return 0;
        }
        case WM_LBUTTONUP: {
            ReleaseCapture();
            if (!self) return 0;
            int sx=GET_X_LPARAM(lp), sy=GET_Y_LPARAM(lp);
            bool hR=self->hBar_.onMouseUp(sx,sy);
            bool vR=self->vBar_.onMouseUp(sx,sy);
            if (!hR&&!vR) {
                if (self->panning_) { self->panning_=false; }
                else if (self->activeSurface_) {
                    auto [cx,cy]=self->vp_.screenToCanvas(float(sx),float(sy));
                    self->activeSurface_->onMouseUp(cx,cy);
                }
                forwardToParent(hwnd,msg,wp,lp);
            }
            self->scheduleRepaint(); return 0;
        }

        case WM_RBUTTONDOWN: {
            SetCapture(hwnd);
            if (self&&self->activeSurface_) {
                auto [cx,cy]=self->vp_.screenToCanvas(
                    float(GET_X_LPARAM(lp)),float(GET_Y_LPARAM(lp)));
                self->activeSurface_->onRightMouseDown(cx,cy);
            }
            forwardToParent(hwnd,msg,wp,lp); return 0;
        }
        case WM_RBUTTONUP: {
            ReleaseCapture();
            if (self&&self->activeSurface_) {
                auto [cx,cy]=self->vp_.screenToCanvas(
                    float(GET_X_LPARAM(lp)),float(GET_Y_LPARAM(lp)));
                self->activeSurface_->onMouseUp(cx,cy);
            }
            forwardToParent(hwnd,msg,wp,lp); return 0;
        }

        case WM_MOUSEMOVE: {
            if (!self) return 0;
            int sx=GET_X_LPARAM(lp), sy=GET_Y_LPARAM(lp);
            if (!self->trackingLeave_) {
                TRACKMOUSEEVENT tme{};
                tme.cbSize=sizeof(tme); tme.dwFlags=TME_LEAVE;
                tme.hwndTrack=hwnd; TrackMouseEvent(&tme);
                self->trackingLeave_=true;
            }
            bool hDrag=self->hBar_.onMouseMove(sx,sy);
            bool vDrag=self->vBar_.onMouseMove(sx,sy);
            self->scheduleRepaint();
            if (hDrag||vDrag) return 0;
            if (self->panning_) { self->continuePan(sx,sy); }
            else if (self->activeSurface_) {
                auto [cx,cy]=self->vp_.screenToCanvas(float(sx),float(sy));
                self->activeSurface_->onMouseMove(cx,cy);
            }
            forwardToParent(hwnd,msg,wp,lp); return 0;
        }
        case WM_MOUSELEAVE:
            if (self) {
                self->trackingLeave_=false;
                self->hBar_.onMouseLeave(); self->vBar_.onMouseLeave();
                self->scheduleRepaint();
            }
            return 0;

        case WM_MOUSEWHEEL: {
            if (!self||!self->viewportEnabled_) return 0;
            int  delta=GET_WHEEL_DELTA_WPARAM(wp);
            bool ctrl =(GET_KEYSTATE_WPARAM(wp)&MK_CONTROL)!=0;
            bool shift=(GET_KEYSTATE_WPARAM(wp)&MK_SHIFT)  !=0;
            POINT pt={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
            ScreenToClient(hwnd,&pt);
            if (ctrl) {
                float f=(delta>0)?1.1f:1.f/1.1f;
                self->vp_.zoomToward(float(pt.x),float(pt.y),f);
            } else if (shift) {
                self->vp_.panByScreen(float(delta)*0.5f,0.f);
            } else {
                self->vp_.panByScreen(0.f,-(float(delta)/WHEEL_DELTA)*40.f);
            }
            self->pokeScrollbars(); self->scheduleRepaint();
            forwardToParent(hwnd,msg,wp,lp); return 0;
        }

        case WM_KEYDOWN: {
            if (!self) return 0;
            bool ctrl=(GetKeyState(Key::Control)&0x8000)!=0;
            bool consumed=false;
            if (ctrl&&self->viewportEnabled_) {
                if (wp==Key::OemPlus||wp==Key::NumpadPlus)  {
                    self->vp_.zoomIn();  self->pokeScrollbars(); self->scheduleRepaint(); consumed=true;
                } else if (wp==Key::OemMinus||wp==Key::NumpadMinus) {
                    self->vp_.zoomOut(); self->pokeScrollbars(); self->scheduleRepaint(); consumed=true;
                } else if (wp=='0') {
                    self->vp_.resetZoom();self->pokeScrollbars(); self->scheduleRepaint(); consumed=true;
                }
            }
            if (!consumed&&self->activeSurface_)
                self->activeSurface_->onKeyDown(int(wp));
            return 0;
        }
        case WM_KEYUP:
            if (self&&self->activeSurface_) self->activeSurface_->onKeyUp(int(wp));
            return 0;

        case WM_TIMER:
            KillTimer(hwnd,1);
            if (self) { self->repaintPending_=false; self->tickAndRender(); }
            return 0;

        default:
            return DefWindowProc(hwnd,msg,wp,lp);
        }
    }

    static void forwardToParent(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
        HWND frame=GetParent(hwnd);
        HWND parent=frame?GetParent(frame):nullptr;
        if (!parent) return;
        POINT pt={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
        MapWindowPoints(hwnd,parent,&pt,1);
        PostMessage(parent,msg,wp,MAKELPARAM(pt.x,pt.y));
    }

    // ── Window class registration ─────────────────────────────────────────────

    static void registerClasses() {
        static bool done=false; if (done) return;
        HINSTANCE hi=GetModuleHandle(nullptr);
        WNDCLASSEXW wf{}; wf.cbSize=sizeof(wf); wf.lpfnWndProc=FrameProc;
        wf.hInstance=hi; wf.lpszClassName=L"FluxGLFrame9";
        wf.style=CS_HREDRAW|CS_VREDRAW; RegisterClassExW(&wf);
        WNDCLASSEXW wg{}; wg.cbSize=sizeof(wg); wg.lpfnWndProc=GLProc;
        wg.hInstance=hi; wg.lpszClassName=L"FluxGLCanvas9";
        wg.style=CS_OWNDC|CS_HREDRAW|CS_VREDRAW; RegisterClassExW(&wg);
        done=true;
    }

    // ── MSAA upgrade ──────────────────────────────────────────────────────────

    bool tryUpgradeToMSAA(int winW, int winH, HGLRC bootstrapCtx) {
        auto wglCPF=reinterpret_cast<PFNWGLCHOOSEPIXELFORMATARBPROC>(
            wglGetProcAddress("wglChoosePixelFormatARB"));
        if (!wglCPF) return false;
        for (int samples:{8,4,2}) {
            const int attribs[]={
                WGL_DRAW_TO_WINDOW_ARB,1, WGL_SUPPORT_OPENGL_ARB,1,
                WGL_DOUBLE_BUFFER_ARB,1,  WGL_PIXEL_TYPE_ARB,WGL_TYPE_RGBA_ARB,
                WGL_COLOR_BITS_ARB,32,    WGL_DEPTH_BITS_ARB,24,
                WGL_STENCIL_BITS_ARB,8,   WGL_SAMPLE_BUFFERS_ARB,1,
                WGL_SAMPLES_ARB,samples,  0
            };
            int fmt=0; UINT numFormats=0;
            if (!wglCPF(glDC_,attribs,nullptr,1,&fmt,&numFormats)||!numFormats) continue;
            wglMakeCurrent(nullptr,nullptr);
            wglDeleteContext(bootstrapCtx);
            ReleaseDC(glHwnd_,glDC_);
            DestroyWindow(glHwnd_);
            glHwnd_=CreateWindowExW(WS_EX_NOPARENTNOTIFY,L"FluxGLCanvas9",nullptr,
                WS_CHILD|WS_VISIBLE|WS_CLIPSIBLINGS|WS_CLIPCHILDREN,
                0,0,winW,winH, frameHwnd_,nullptr,GetModuleHandle(nullptr),nullptr);
            assert(glHwnd_);
            SetWindowLongPtrW(glHwnd_,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(this));
            glDC_=GetDC(glHwnd_);
            PIXELFORMATDESCRIPTOR pfd2{}; pfd2.nSize=sizeof(pfd2); pfd2.nVersion=1;
            SetPixelFormat(glDC_,fmt,&pfd2);
            HGLRC newBootstrap=wglCreateContext(glDC_);
            wglMakeCurrent(glDC_,newBootstrap);
            char dbg[64];
            _snprintf_s(dbg,sizeof(dbg),_TRUNCATE,"FluxCanvas: MSAA %dx enabled\n",samples);
            OutputDebugStringA(dbg);
            return true;
        }
        return false;
    }

    void setupPixelFormat(HDC dc) {
        PIXELFORMATDESCRIPTOR pfd{};
        pfd.nSize=sizeof(pfd); pfd.nVersion=1;
        pfd.dwFlags=PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER;
        pfd.iPixelType=PFD_TYPE_RGBA; pfd.cColorBits=32;
        pfd.cDepthBits=24; pfd.cStencilBits=8; pfd.iLayerType=PFD_MAIN_PLANE;
        SetPixelFormat(dc,ChoosePixelFormat(dc,&pfd),&pfd);
    }

    // ── GL context + Canvas2DGL init ──────────────────────────────────────────

    void ensureWindows(GraphicsContext& ctx) {
        (void)ctx;
        if (frameHwnd_) return;

        parentHwnd_=FluxUI::getCurrentInstance()->getWindow();
        if (!parentHwnd_) return;
        registerClasses();

        frameHwnd_=CreateWindowExW(WS_EX_NOPARENTNOTIFY,L"FluxGLFrame9",nullptr,
            WS_CHILD|WS_VISIBLE|WS_CLIPSIBLINGS|WS_CLIPCHILDREN,
            x,y,width,height, parentHwnd_,nullptr,GetModuleHandle(nullptr),nullptr);
        assert(frameHwnd_);
        SetWindowLongPtrW(frameHwnd_,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(this));

        glHwnd_=CreateWindowExW(WS_EX_NOPARENTNOTIFY,L"FluxGLCanvas9",nullptr,
            WS_CHILD|WS_VISIBLE|WS_CLIPSIBLINGS|WS_CLIPCHILDREN,
            0,0,width,height, frameHwnd_,nullptr,GetModuleHandle(nullptr),nullptr);
        assert(glHwnd_);
        SetWindowLongPtrW(glHwnd_,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(this));

        glDC_=GetDC(glHwnd_);
        setupPixelFormat(glDC_);
        HGLRC tmp=wglCreateContext(glDC_);
        wglMakeCurrent(glDC_,tmp);

        tryUpgradeToMSAA(width,height,tmp);
        HGLRC currentBootstrap=wglGetCurrentContext();

        auto wglCA=reinterpret_cast<PFNWGLCREATECONTEXTATTRIBSARBPROC>(
            wglGetProcAddress("wglCreateContextAttribsARB"));
        auto tryCtx=[&](int major,int minor)->HGLRC {
            if (!wglCA) return nullptr;
            const int att[]={
                WGL_CONTEXT_MAJOR_VERSION_ARB,major,
                WGL_CONTEXT_MINOR_VERSION_ARB,minor,
                WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,0
            };
            return wglCA(glDC_,nullptr,att);
        };
        HGLRC core=tryCtx(4,6);
        if (!core) core=tryCtx(4,5);
        if (!core) core=tryCtx(3,3);
        if (core) {
            wglMakeCurrent(nullptr,nullptr);
            wglDeleteContext(currentBootstrap);
            glRC_=core;
        } else {
            glRC_=currentBootstrap;
        }
        wglMakeCurrent(glDC_,glRC_);

        if (!gladLoadGL()) {
            OutputDebugStringA("GLAD: gladLoadGL() failed!\n");
            assert(false && "gladLoadGL failed");
        }

        {
            char info[256];
            _snprintf_s(info,sizeof(info),_TRUNCATE,
                "OpenGL %s  |  GLSL %s  |  %s\n",
                (const char*)glGetString(GL_VERSION),
                (const char*)glGetString(GL_SHADING_LANGUAGE_VERSION),
                (const char*)glGetString(GL_RENDERER));
            OutputDebugStringA(info);
        }

#ifndef NDEBUG
        glEnable(GL_DEBUG_OUTPUT);
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
        glDebugMessageCallback(
            [](GLenum source,GLenum type,GLuint id,GLenum severity,
               GLsizei,const GLchar* message,const void*) {
                if (severity==GL_DEBUG_SEVERITY_NOTIFICATION) return;
                const char* sev=
                    severity==GL_DEBUG_SEVERITY_HIGH  ?"HIGH"  :
                    severity==GL_DEBUG_SEVERITY_MEDIUM?"MEDIUM":
                    severity==GL_DEBUG_SEVERITY_LOW   ?"LOW"   :"NOTIFY";
                char buf[512];
                _snprintf_s(buf,sizeof(buf),_TRUNCATE,
                    "[GL %s] src=0x%04X type=0x%04X id=%u: %s\n",
                    sev,source,type,id,message);
                OutputDebugStringA(buf);
                if (type==GL_DEBUG_TYPE_ERROR) DebugBreak();
            },nullptr);
        glDebugMessageControl(GL_DONT_CARE,GL_DONT_CARE,
                              GL_DEBUG_SEVERITY_NOTIFICATION,0,nullptr,GL_FALSE);
#endif

        // ── Canvas2DGL shared context ─────────────────────────────────────────
        canvasGL_ = new Canvas2DGL();
        if (!canvasGL_->init()) {
            OutputDebugStringA("FluxCanvas: Canvas2DGL init failed!\n");
            assert(false && "Canvas2DGL init failed");
        }

        // Register fonts (system fonts on Windows)
        auto regFont=[this](const char* name, const char* path) {
            if (!Canvas2D::registerFont(canvasGL_, name, path)) {
                char warn[256];
                _snprintf_s(warn,sizeof(warn),_TRUNCATE,
                    "FluxCanvas: font '%s' not found at '%s'\n",name,path);
                OutputDebugStringA(warn);
            }
        };
        regFont("sans",             "C:\\Windows\\Fonts\\segoeui.ttf");
        regFont("sans-bold",        "C:\\Windows\\Fonts\\segoeuib.ttf");
        regFont("sans-italic",      "C:\\Windows\\Fonts\\segoeuii.ttf");
        regFont("sans-bold-italic", "C:\\Windows\\Fonts\\segoeuiz.ttf");
        regFont("mono",             "C:\\Windows\\Fonts\\consola.ttf");
        regFont("mono-bold",        "C:\\Windows\\Fonts\\consolab.ttf");

        // ── Scrollbar GL program ──────────────────────────────────────────────
        sbProg_  = glutil::linkProgram(kSBVert, kSBFrag);
        assert(sbProg_);
        sbMVP_   = glGetUniformLocation(sbProg_, "uMVP");
        sbColor_ = glGetUniformLocation(sbProg_, "uColor");

        glGenVertexArrays(1, &sbVAO_);
        glGenBuffers(1, &sbVBO_);

        // ── Viewport / surface init ───────────────────────────────────────────
        vp_.init(width, height, canvasW_, canvasH_);
        lastGLW_=width; lastGLH_=height;
        updateViewportSize(width,height);
        updateSBGeometry(width,height);
        vp_.fitToView();

        activatePendingSurface();
        lastTick_=Clock::now();

        if (onViewportChanged) onViewportChanged(vp_.zoom());
        if (onGLResize)        onGLResize(width,height);
    }

    // ── Surface management ────────────────────────────────────────────────────

    void activatePendingSurface() {
        if (!pendingSurface_) return;
        if (activeSurface_) { activeSurface_->destroy(); activeSurface_.reset(); }
        activeSurface_=pendingSurface_;
        pendingSurface_.reset();
        activeSurface_->initialize(canvasW_,canvasH_);
        vp_.setCanvasSize(canvasW_,canvasH_);
    }

    // ── Main render loop ──────────────────────────────────────────────────────

    void tickAndRender() {
        if (!glRC_||!glDC_||!canvasGL_) return;
        if (pendingSurface_) activatePendingSurface();
        if (!activeSurface_) return;

        makeCurrent();

        auto   now=Clock::now();
        double dt=std::chrono::duration<double>(now-lastTick_).count();
        lastTick_=now;

        activeSurface_->update(dt);

        int glW=lastGLW_<1?1:lastGLW_;
        int glH=lastGLH_<1?1:lastGLH_;

        // ── Clear ─────────────────────────────────────────────────────────
        glViewport(0,0,glW,glH);
        glClearColor(0.f,0.f,0.f,1.f);
        glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);
        glEnable(GL_MULTISAMPLE);

        // Enable blending globally
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);

        // ── Pre-render (raw GL, before Canvas2D) ──────────────────────────
        activeSurface_->preRender();

        // ── Restore viewport after preRender ──────────────────────────────
        glBindFramebuffer(GL_FRAMEBUFFER,0);
        glViewport(0,0,glW,glH);


        float z  = vp_.zoom();
        float ox = -vp_.offsetX() * z;
        float oy =  vp_.offsetY() * z;


        float ortho[16];
        glutil::ortho(0.f, float(glW), float(glH), 0.f, ortho);


        float vp[16]={
            z,  0,  0, 0,
            0,  z,  0, 0,
            0,  0,  1, 0,
            ox, oy, 0, 1
        };

        float mvp[16]={};
        for (int col=0;col<4;++col)
            for (int row=0;row<4;++row)
                for (int k=0;k<4;++k)
                    mvp[col*4+row]+=ortho[k*4+row]*vp[col*4+k];

        // ── Canvas2D render ───────────────────────────────────────────────
        {
            Canvas2D ctx(canvasGL_, canvasW_, canvasH_, mvp);
            activeSurface_->render(ctx);
        }

        // ── Scrollbars (screen space) ─────────────────────────────────────
        updateSBGeometry(glW,glH);
        renderScrollbarsGL(glW,glH,dt);

        SwapBuffers(glDC_);

        bool needsCont=activeSurface_->needsContinuousRedraw();
        if (needsCont&&!repaintPending_) {
            repaintPending_=true;
            SetTimer(glHwnd_,1,16,nullptr);
        }
    }

    // ── Window resize ─────────────────────────────────────────────────────────

    void moveWindows() {
        if (!frameHwnd_) return;
        SetWindowPos(frameHwnd_,nullptr,x,y,width,height,SWP_NOZORDER|SWP_NOACTIVATE);
        SetWindowPos(glHwnd_,nullptr,0,0,width,height,SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOMOVE);
        if (width!=lastGLW_||height!=lastGLH_) {
            makeCurrent();
            lastGLW_=width; lastGLH_=height;
            updateViewportSize(width,height);
            glViewport(0,0,width,height);
            if (onGLResize) onGLResize(width,height);
        }
    }

    // ── Teardown ──────────────────────────────────────────────────────────────

    void destroyGL() {
        if (glRC_) {
            wglMakeCurrent(glDC_,glRC_);

            if (activeSurface_) { activeSurface_->destroy(); activeSurface_.reset(); }
            pendingSurface_.reset();

            // Destroy Canvas2DGL
            if (canvasGL_) { canvasGL_->destroy(); delete canvasGL_; canvasGL_=nullptr; }

            // Destroy scrollbar GL resources
            if (sbProg_) { glDeleteProgram(sbProg_);       sbProg_=0; }
            if (sbVAO_)  { glDeleteVertexArrays(1,&sbVAO_);sbVAO_=0; }
            if (sbVBO_)  { glDeleteBuffers(1,&sbVBO_);     sbVBO_=0; }

            wglMakeCurrent(nullptr,nullptr);
            wglDeleteContext(glRC_); glRC_=nullptr;
        }
        if (glHwnd_&&glDC_) { ReleaseDC(glHwnd_,glDC_); glDC_=nullptr; }
        if (glHwnd_)   { DestroyWindow(glHwnd_);   glHwnd_=nullptr;   }
        if (frameHwnd_){ DestroyWindow(frameHwnd_);frameHwnd_=nullptr; }
    }
};

// ── Factory helpers ───────────────────────────────────────────────────────────

inline std::shared_ptr<CanvasWidget> Canvas() {
    return std::make_shared<CanvasWidget>();
}
inline std::shared_ptr<CanvasWidget> Canvas(int w, int h) {
    return std::make_shared<CanvasWidget>()->setSize(w,h);
}