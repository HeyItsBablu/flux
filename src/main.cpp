// test_paint_viewport.cpp
#include "flux.hpp"
#include <commdlg.h>
#include <shlobj.h>
#pragma comment(lib, "comdlg32.lib")

// ── App-level tool enum ──────────────────────────────────────────────────────
enum class Tool {
  Brush  = kToolBrush,   // 0
  Eraser = kToolEraser,  // 1
  // Shape tools (values ≥ 2)
  Line        = 2,
  Rect        = 3,
  Fill        = 4,
  Ellipse     = 5,
  Triangle    = 6,
  RightTriangle = 7,
  Diamond     = 8,
  Pentagon    = 9,
  Hexagon     = 10,
  Star5       = 11,
  Star6       = 12,
  Arrow       = 13,
  ArrowDouble = 14,
  Parallelogram = 15,
  Trapezoid   = 16,
  RoundRect   = 17,  // approximated with many segments
  Cross       = 18,
  Heart       = 19,
};

static bool isShapeToolEnum(Tool t) {
  return t != Tool::Brush && t != Tool::Eraser && t != Tool::Fill;
}
static bool isDrawTool(Tool t) {
  return t == Tool::Brush || t == Tool::Eraser;
}

// ── Hex helpers ──────────────────────────────────────────────────────────────
static COLORREF hexToRef(const std::string &css) {
  RGBA c = parseHexColor(css);
  return RGB((BYTE)(c.r*255),(BYTE)(c.g*255),(BYTE)(c.b*255));
}
static std::string refToHex(COLORREF c) { return ColorToHex(c); }

// ── Save dialog ──────────────────────────────────────────────────────────────
static std::wstring promptSavePath(HWND owner) {
  wchar_t buf[MAX_PATH] = L"painting.png";
  OPENFILENAMEW ofn{};
  ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = owner;
  ofn.lpstrFilter = L"PNG Image\0*.png\0All Files\0*.*\0";
  ofn.lpstrFile = buf; ofn.nMaxFile = MAX_PATH;
  ofn.lpstrDefExt = L"png";
  ofn.Flags = OFN_OVERWRITEPROMPT|OFN_PATHMUSTEXIST;
  ofn.lpstrTitle = L"Save as PNG";
  return GetSaveFileNameW(&ofn) ? buf : L"";
}

// ============================================================================
// §1  GEOMETRY HELPERS
// ============================================================================

// Append a thick line (2 triangles = 6 verts) to out[], return new count
static int appendThickLine(float x0,float y0,float x1,float y1,float half,
                            float *out, int base) {
  float dx=x1-x0,dy=y1-y0,len=std::sqrt(dx*dx+dy*dy);
  if (len<0.001f){
    float v[12]={x0-half,y0-half, x0+half,y0-half, x0+half,y0+half,
                 x0+half,y0+half, x0-half,y0+half, x0-half,y0-half};
    memcpy(out+base*2,v,sizeof(v)); return base+6;
  }
  float nx=-dy/len*half, ny=dx/len*half;
  float v[12]={x0+nx,y0+ny, x0-nx,y0-ny, x1-nx,y1-ny,
               x1-nx,y1-ny, x1+nx,y1+ny, x0+nx,y0+ny};
  memcpy(out+base*2,v,sizeof(v)); return base+6;
}

// Build a filled convex polygon (triangle fan) from a list of perimeter points.
// out must hold at least (n+2)*2 floats.  Returns vertex count for GL_TRIANGLE_FAN.
static int buildFan(float cx,float cy,const std::vector<std::pair<float,float>> &pts,
                    float *out) {
  int n=(int)pts.size();
  out[0]=cx; out[1]=cy;
  for(int i=0;i<n;i++){ out[2+i*2]=pts[i].first; out[3+i*2]=pts[i].second; }
  out[2+n*2]=pts[0].first; out[3+n*2]=pts[0].second;
  return n+2;
}

// Regular n-gon (outline via thick lines), filled=false → outline verts as thick lines
static std::vector<float> nGonOutline(float cx,float cy,float rx,float ry,
                                       int n,float rot,float half) {
  std::vector<float> buf(6*n*2);
  int total=0;
  for(int i=0;i<n;i++){
    float a0=rot+float(i)/n*6.2831853f;
    float a1=rot+float(i+1)/n*6.2831853f;
    float x0=cx+cosf(a0)*rx, y0=cy+sinf(a0)*ry;
    float x1=cx+cosf(a1)*rx, y1=cy+sinf(a1)*ry;
    total=appendThickLine(x0,y0,x1,y1,half,buf.data(),total);
  }
  buf.resize(total*2); return buf;
}

// Filled regular n-gon
static std::vector<float> nGonFilled(float cx,float cy,float rx,float ry,
                                      int n,float rot) {
  std::vector<std::pair<float,float>> pts;
  for(int i=0;i<n;i++){
    float a=rot+float(i)/n*6.2831853f;
    pts.push_back({cx+cosf(a)*rx, cy+sinf(a)*ry});
  }
  std::vector<float> buf((n+2)*2);
  buildFan(cx,cy,pts,buf.data());
  return buf;
}

// 5-point star outline
static std::vector<float> star5Outline(float cx,float cy,float ro,float ri,
                                        float half) {
  std::vector<float> buf(10*6*2);
  int total=0;
  float off=-3.14159265f/2;
  for(int i=0;i<10;i++){
    float a0=off+float(i)/10*6.2831853f;
    float a1=off+float(i+1)/10*6.2831853f;
    float r0=(i%2==0)?ro:ri, r1=(i%2==1)?ro:ri;
    total=appendThickLine(cx+cosf(a0)*r0,cy+sinf(a0)*r0,
                          cx+cosf(a1)*r1,cy+sinf(a1)*r1,half,buf.data(),total);
  }
  buf.resize(total*2); return buf;
}

// 6-point star outline
static std::vector<float> star6Outline(float cx,float cy,float ro,float ri,
                                        float half) {
  std::vector<float> buf(12*6*2);
  int total=0;
  float off=0.f;
  for(int i=0;i<12;i++){
    float a0=off+float(i)/12*6.2831853f;
    float a1=off+float(i+1)/12*6.2831853f;
    float r0=(i%2==0)?ro:ri, r1=(i%2==1)?ro:ri;
    total=appendThickLine(cx+cosf(a0)*r0,cy+sinf(a0)*r0,
                          cx+cosf(a1)*r1,cy+sinf(a1)*r1,half,buf.data(),total);
  }
  buf.resize(total*2); return buf;
}

// Arrow (right-pointing, from x0,y0 to x1,y1)
static std::vector<float> arrowVerts(float x0,float y0,float x1,float y1,float half) {
  float dx=x1-x0,dy=y1-y0,len=std::sqrt(dx*dx+dy*dy);
  if(len<1.f) return {};
  float ux=dx/len,uy=dy/len;
  float px=-uy,py=ux; // perp
  float hs=half*1.2f; // shaft half-width
  float headLen=min(len*0.4f,half*4.f);
  float headW=half*2.5f;
  float mx=x1-ux*headLen, my=y1-uy*headLen;

  // shaft rectangle (2 triangles)
  std::vector<float> v={
    x0+px*hs, y0+py*hs,  x0-px*hs, y0-py*hs,  mx-px*hs, my-py*hs,
    mx-px*hs, my-py*hs,  mx+px*hs, my+py*hs,   x0+px*hs, y0+py*hs,
    // arrowhead triangle
    x1,y1,  mx+px*headW,my+py*headW,  mx-px*headW,my-py*headW
  };
  return v;
}

// Double-headed arrow
static std::vector<float> doubleArrowVerts(float x0,float y0,float x1,float y1,float half){
  float dx=x1-x0,dy=y1-y0,len=std::sqrt(dx*dx+dy*dy);
  if(len<1.f) return {};
  float ux=dx/len,uy=dy/len;
  float px=-uy,py=ux;
  float hs=half*1.2f;
  float headLen=min(len*0.3f,half*4.f);
  float headW=half*2.5f;
  float m0x=x0+ux*headLen,m0y=y0+uy*headLen;
  float m1x=x1-ux*headLen,m1y=y1-uy*headLen;

  std::vector<float> v={
    m0x+px*hs,m0y+py*hs, m0x-px*hs,m0y-py*hs, m1x-px*hs,m1y-py*hs,
    m1x-px*hs,m1y-py*hs, m1x+px*hs,m1y+py*hs, m0x+px*hs,m0y+py*hs,
    x0,y0, m0x+px*headW,m0y+py*headW, m0x-px*headW,m0y-py*headW,
    x1,y1, m1x+px*headW,m1y+py*headW, m1x-px*headW,m1y-py*headW
  };
  return v;
}

// Ellipse outline (via thick line segments)
static std::vector<float> ellipseOutline(float cx,float cy,float rx,float ry,
                                          int segs,float half){
  std::vector<float> buf(segs*6*2);
  int total=0;
  for(int i=0;i<segs;i++){
    float a0=float(i)/segs*6.2831853f;
    float a1=float(i+1)/segs*6.2831853f;
    total=appendThickLine(cx+cosf(a0)*rx,cy+sinf(a0)*ry,
                          cx+cosf(a1)*rx,cy+sinf(a1)*ry,half,buf.data(),total);
  }
  buf.resize(total*2); return buf;
}

// Cross / plus sign
static std::vector<float> crossVerts(float cx,float cy,float rx,float ry,float arm){
  // 12-point polygon approximation
  float hx=arm,hy=arm;
  std::vector<float> buf={
    cx-hx,cy-ry, cx+hx,cy-ry, cx+hx,cy-hy, cx+rx,cy-hy,
    cx+rx,cy+hy, cx+hx,cy+hy, cx+hx,cy+ry, cx-hx,cy+ry,
    cx-hx,cy+hy, cx-rx,cy+hy, cx-rx,cy-hy, cx-hx,cy-hy
  };
  // fan from center
  std::vector<float> fan;
  fan.push_back(cx); fan.push_back(cy);
  for(int i=0;i<12;i++){fan.push_back(buf[i*2]);fan.push_back(buf[i*2+1]);}
  fan.push_back(buf[0]); fan.push_back(buf[1]);
  return fan;
}

// Simple heart (bezier approximated with line segments)
static std::vector<float> heartFan(float cx,float cy,float rx,float ry){
  const int segs=64;
  std::vector<std::pair<float,float>> pts;
  for(int i=0;i<segs;i++){
    float t=float(i)/segs*6.2831853f;
    // parametric heart
    float hx=rx*(16.f*sinf(t)*sinf(t)*sinf(t))/16.f;
    float hy=-ry*(13.f*cosf(t)-5.f*cosf(2*t)-2.f*cosf(3*t)-cosf(4*t))/16.f;
    pts.push_back({cx+hx,cy+hy});
  }
  std::vector<float> buf((segs+2)*2);
  buildFan(cx,cy,pts,buf.data());
  return buf;
}

// Parallelogram (outline via 4 thick lines)
static std::vector<float> parallelogramOutline(float x0,float y0,float x1,float y1,
                                                float shear,float half){
  float lx=min(x0,x1),rx=max(x0,x1);
  float by=min(y0,y1),ty=max(y0,y1);
  float sh=(rx-lx)*shear;
  float ax=lx+sh,bx=rx+sh,cx=rx,dx=lx;
  float ay=by,   bby=by,   ccy=ty,ddy=ty;
  std::vector<float> buf(4*6*2);
  int total=0;
  total=appendThickLine(ax,ay,bx,bby,half,buf.data(),total);
  total=appendThickLine(bx,bby,cx,ccy,half,buf.data(),total);
  total=appendThickLine(cx,ccy,dx,ddy,half,buf.data(),total);
  total=appendThickLine(dx,ddy,ax,ay, half,buf.data(),total);
  buf.resize(total*2); return buf;
}

// Trapezoid outline
static std::vector<float> trapezoidOutline(float x0,float y0,float x1,float y1,
                                            float taper,float half){
  float lx=min(x0,x1),rx=max(x0,x1);
  float by=min(y0,y1),ty=max(y0,y1);
  float w=rx-lx,t=w*taper;
  float ax=lx,bx=rx,cx=rx-t,dx=lx+t;
  std::vector<float> buf(4*6*2);
  int total=0;
  total=appendThickLine(ax,by,bx,by,half,buf.data(),total);
  total=appendThickLine(bx,by,cx,ty,half,buf.data(),total);
  total=appendThickLine(cx,ty,dx,ty,half,buf.data(),total);
  total=appendThickLine(dx,ty,ax,by,half,buf.data(),total);
  buf.resize(total*2); return buf;
}

// ============================================================================
// §2  FLOOD FILL
// ============================================================================
static void floodFill(uint8_t *pixels,int w,int h,int px,int py,
                      uint8_t tr,uint8_t tg,uint8_t tb,
                      uint8_t fr,uint8_t fg,uint8_t fb){
  if(px<0||py<0||px>=w||py>=h) return;
  if(tr==fr&&tg==fg&&tb==fb) return;
  auto match=[&](int x,int y)->bool{
    const uint8_t*p=pixels+(y*w+x)*4;
    return std::abs((int)p[0]-(int)tr)<=10&&
           std::abs((int)p[1]-(int)tg)<=10&&
           std::abs((int)p[2]-(int)tb)<=10;
  };
  if(!match(px,py)) return;
  std::vector<std::pair<int,int>> stack;
  stack.reserve(w*h/8);
  stack.push_back({px,py});
  while(!stack.empty()){
    auto[x,y]=stack.back(); stack.pop_back();
    if(x<0||x>=w||y<0||y>=h||!match(x,y)) continue;
    uint8_t*p=pixels+(y*w+x)*4;
    p[0]=fr;p[1]=fg;p[2]=fb;p[3]=255;
    stack.push_back({x+1,y});stack.push_back({x-1,y});
    stack.push_back({x,y+1});stack.push_back({x,y-1});
  }
}

// ============================================================================
// §3  PaintSurface
// ============================================================================
class PaintSurface : public RasterSurface {
public:
  const std::vector<std::pair<std::string,std::string>> kPalette = {
    {"#cba6f7","Purple"},{"#f38ba8","Pink"},{"#fab387","Peach"},
    {"#f9e2af","Yellow"},{"#a6e3a1","Green"},{"#89dceb","Sky"},
    {"#89b4fa","Blue"},  {"#ffffff","White"},{"#6c7086","Gray"},
    {"#1e1e2e","Black"},
  };

  std::string activeColor = "#cba6f7";
  float activeSize    = 4.f;
  float activeOpacity = 1.f;
  Tool  activeTool    = Tool::Brush;
  bool  filled        = false;  // fill interior of shapes

  std::function<void()>                          onStateChanged;
  std::function<void(const std::vector<RGBA>&)>  onHistoryChanged;
  std::function<void(const std::string&)>        onStatusMessage;
  std::function<void()>                          onRedrawNeeded;

  void setColor(const std::string& col){ activeColor=col; syncStyle(); }
  void setSize (float sz)              { activeSize=sz;   syncStyle(); }
  void setOpacity(float op)            { activeOpacity=op;syncStyle(); }
  void setFilled(bool f)               { filled=f; }

  void setActiveTool(Tool t){
    activeTool=t;
    setTool(isDrawTool(t) ? static_cast<ToolId>(t) : kToolBrush);
    syncStyle();
    shapeDrawing_=false;
    scratchClear();
  }

  void initialize(int w,int h) override {
    RasterSurface::initialize(w,h);
    syncStyle();
  }

  // ── Mouse ──────────────────────────────────────────────────────────────────
  void onMouseDown(float x,float y) override {
    if(activeTool==Tool::Fill){ doFill(x,y); return; }
    if(isShapeToolEnum(activeTool)){ shapeStart(x,y); return; }
    RasterSurface::onMouseDown(x,y);
  }
  void onMouseMove(float x,float y) override {
    if(isShapeToolEnum(activeTool)&&activeTool!=Tool::Fill){
      if(shapeDrawing_) shapePreview(x,y);
      return;
    }
    RasterSurface::onMouseMove(x,y);
  }
  void onMouseUp(float x,float y) override {
    if(activeTool==Tool::Fill) return;
    if(isShapeToolEnum(activeTool)){
      if(shapeDrawing_) shapeCommit(x,y);
      return;
    }
    RasterSurface::onMouseUp(x,y);
    if(onStateChanged)    onStateChanged();
    if(onHistoryChanged)  onHistoryChanged(colorHistory());
  }

  void onKeyDown(int key) override {
    RasterSurface::onKeyDown(key);
    switch(key){
      case 'B': setActiveTool(Tool::Brush);  break;
      case 'E': setActiveTool(Tool::Eraser); break;
      case 'L': setActiveTool(Tool::Line);   break;
      case 'R': setActiveTool(Tool::Rect);   break;
      case 'F': setActiveTool(Tool::Fill);   break;
    }
    if(onStateChanged) onStateChanged();
  }
  void onKeyUp(int) override {}

private:
  bool  shapeDrawing_=false;
  float shapeX0_=0,shapeY0_=0;

  // ── Shape lifecycle ─────────────────────────────────────────────────────────
  void shapeStart(float x,float y){
    shapeX0_=x; shapeY0_=y;
    shapeDrawing_=true;
    scratchClear();
  }

  void shapePreview(float x,float y){
    scratchClear();
    drawShapeInto(scratchFBOHandle(),shapeX0_,shapeY0_,x,y);
    if(onRedrawNeeded) onRedrawNeeded();
  }

  void shapeCommit(float x,float y){
    shapeDrawing_=false;
    scratchClear();
    pushUndoSnapshotPublic();
    drawShapeInto(committedFBOHandle(),shapeX0_,shapeY0_,x,y);
    if(onStateChanged)   onStateChanged();
    if(onRedrawNeeded)   onRedrawNeeded();
  }

  void drawShapeInto(GLuint fbo,float x0,float y0,float x1,float y1){
    RGBA c=parseHexColor(activeColor);
    float r=c.r,g=c.g,b=c.b,a=activeOpacity;
    float half=max(0.5f,activeSize*0.5f);

    float cx=(x0+x1)*0.5f, cy=(y0+y1)*0.5f;
    float rx=std::abs(x1-x0)*0.5f, ry=std::abs(y1-y0)*0.5f;

    // Helper to emit outline or filled fan
    auto emit=[&](const std::vector<float>&v, GLenum mode){
      if(v.empty()) return;
      drawVertsToFBO(fbo,v.data(),(int)(v.size()/2),mode,r,g,b,a);
    };
    auto emitOutline=[&](const std::vector<float>&v){
      emit(v,GL_TRIANGLES);
    };
    auto emitFan=[&](const std::vector<float>&v){
      emit(v,GL_TRIANGLE_FAN);
    };

    // 4-side rect helper (outline via 4 thick lines)
    auto rectOutline=[&](float lx,float by,float rx2,float ty){
      float buf[4*12]; int total=0;
      total=appendThickLine(lx,by,rx2,by,half,buf,total);
      total=appendThickLine(rx2,by,rx2,ty,half,buf,total);
      total=appendThickLine(rx2,ty,lx,ty,half,buf,total);
      total=appendThickLine(lx,ty,lx,by,half,buf,total);
      drawVertsToFBO(fbo,buf,total,GL_TRIANGLES,r,g,b,a);
    };
    auto rectFilled=[&](float lx,float by,float rx2,float ty){
      float v[]={lx,by, rx2,by, rx2,ty, rx2,ty, lx,ty, lx,by};
      drawVertsToFBO(fbo,v,6,GL_TRIANGLES,r,g,b,a);
    };

    switch(activeTool){
      case Tool::Line:{
        float v[12]; int n=0;
        n=appendThickLine(x0,y0,x1,y1,half,v,0);
        drawVertsToFBO(fbo,v,n,GL_TRIANGLES,r,g,b,a);
        break;
      }
      case Tool::Rect:{
        float lx=min(x0,x1),bby=min(y0,y1);
        float rxx=max(x0,x1),ty=max(y0,y1);
        if(filled) rectFilled(lx,bby,rxx,ty);
        else       rectOutline(lx,bby,rxx,ty);
        break;
      }
      case Tool::Ellipse:{
        if(filled){
          auto v=nGonFilled(cx,cy,rx,ry,64,0);
          emitFan(v);
        } else {
          auto v=ellipseOutline(cx,cy,rx,ry,64,half);
          emitOutline(v);
        }
        break;
      }
      case Tool::Triangle:{
        // Equilateral-ish: apex top-center, base bottom
        std::vector<std::pair<float,float>> pts={
          {cx,    cy-ry},
          {cx+rx, cy+ry},
          {cx-rx, cy+ry}
        };
        if(filled){
          auto v=std::vector<float>((3+2)*2);
          buildFan(cx,cy+ry*0.3f,pts,v.data());
          emitFan(v);
        } else {
          float buf[3*12]; int total=0;
          for(int i=0;i<3;i++){
            int j=(i+1)%3;
            total=appendThickLine(pts[i].first,pts[i].second,
                                  pts[j].first,pts[j].second,half,buf,total);
          }
          drawVertsToFBO(fbo,buf,total,GL_TRIANGLES,r,g,b,a);
        }
        break;
      }
      case Tool::RightTriangle:{
        std::vector<std::pair<float,float>> pts={
          {x0,y1},{x1,y1},{x0,y0}
        };
        if(filled){
          float v[(3+2)*2];
          buildFan((x0+x1)/3.f,(y0+y1*2)/3.f,pts,v);
          emitFan(std::vector<float>(v,v+10));
        } else {
          float buf[3*12]; int total=0;
          for(int i=0;i<3;i++){
            int j=(i+1)%3;
            total=appendThickLine(pts[i].first,pts[i].second,
                                  pts[j].first,pts[j].second,half,buf,total);
          }
          drawVertsToFBO(fbo,buf,total,GL_TRIANGLES,r,g,b,a);
        }
        break;
      }
      case Tool::Diamond:{
        std::vector<std::pair<float,float>> pts={
          {cx,cy-ry},{cx+rx,cy},{cx,cy+ry},{cx-rx,cy}
        };
        if(filled){
          auto v=std::vector<float>((4+2)*2);
          buildFan(cx,cy,pts,v.data());
          emitFan(v);
        } else {
          float buf[4*12]; int total=0;
          for(int i=0;i<4;i++){
            int j=(i+1)%4;
            total=appendThickLine(pts[i].first,pts[i].second,
                                  pts[j].first,pts[j].second,half,buf,total);
          }
          drawVertsToFBO(fbo,buf,total,GL_TRIANGLES,r,g,b,a);
        }
        break;
      }
      case Tool::Pentagon:{
        if(filled){ auto v=nGonFilled(cx,cy,rx,ry,5,-1.5708f); emitFan(v); }
        else       { auto v=nGonOutline(cx,cy,rx,ry,5,-1.5708f,half); emitOutline(v); }
        break;
      }
      case Tool::Hexagon:{
        if(filled){ auto v=nGonFilled(cx,cy,rx,ry,6,0); emitFan(v); }
        else       { auto v=nGonOutline(cx,cy,rx,ry,6,0,half); emitOutline(v); }
        break;
      }
      case Tool::Star5:{
        float ro=min(rx,ry), ri=ro*0.4f;
        if(filled){
          // fan: alternate outer/inner points
          std::vector<std::pair<float,float>> pts;
          float off=-1.5708f;
          for(int i=0;i<10;i++){
            float aa=off+float(i)/10*6.2831853f;
            float rr=(i%2==0)?ro:ri;
            pts.push_back({cx+cosf(aa)*rr,cy+sinf(aa)*rr});
          }
          auto v=std::vector<float>((10+2)*2);
          buildFan(cx,cy,pts,v.data());
          emitFan(v);
        } else {
          auto v=star5Outline(cx,cy,ro,ri,half);
          emitOutline(v);
        }
        break;
      }
      case Tool::Star6:{
        float ro=min(rx,ry), ri=ro*0.5f;
        if(filled){
          std::vector<std::pair<float,float>> pts;
          for(int i=0;i<12;i++){
            float aa=float(i)/12*6.2831853f;
            float rr=(i%2==0)?ro:ri;
            pts.push_back({cx+cosf(aa)*rr,cy+sinf(aa)*rr});
          }
          auto v=std::vector<float>((12+2)*2);
          buildFan(cx,cy,pts,v.data());
          emitFan(v);
        } else {
          auto v=star6Outline(cx,cy,ro,ri,half);
          emitOutline(v);
        }
        break;
      }
      case Tool::Arrow:{
        auto v=arrowVerts(x0,y0,x1,y1,half);
        if(!v.empty()) drawVertsToFBO(fbo,v.data(),(int)(v.size()/2),GL_TRIANGLES,r,g,b,a);
        break;
      }
      case Tool::ArrowDouble:{
        auto v=doubleArrowVerts(x0,y0,x1,y1,half);
        if(!v.empty()) drawVertsToFBO(fbo,v.data(),(int)(v.size()/2),GL_TRIANGLES,r,g,b,a);
        break;
      }
      case Tool::Parallelogram:{
        auto v=parallelogramOutline(x0,y0,x1,y1,0.25f,half);
        emitOutline(v);
        break;
      }
      case Tool::Trapezoid:{
        auto v=trapezoidOutline(x0,y0,x1,y1,0.2f,half);
        emitOutline(v);
        break;
      }
      case Tool::RoundRect:{
        // approximate with 8-sided polygon per corner
        const int cornSegs=8;
        float lx=min(x0,x1),bby=min(y0,y1);
        float rxx=max(x0,x1),ty=max(y0,y1);
        float cr=min(min(rx,ry)*0.3f,12.f);
        std::vector<std::pair<float,float>> pts;
        auto addCorner=[&](float ox,float oy,float startA){
          for(int i=0;i<=cornSegs;i++){
            float aa=startA+float(i)/cornSegs*1.5708f;
            pts.push_back({ox+cosf(aa)*cr,oy+sinf(aa)*cr});
          }
        };
        addCorner(rxx-cr,ty-cr,0);
        addCorner(lx+cr, ty-cr,1.5708f);
        addCorner(lx+cr, bby+cr,3.1416f);
        addCorner(rxx-cr,bby+cr,4.7124f);
        if(filled){
          auto v=std::vector<float>((pts.size()+2)*2);
          buildFan(cx,cy,pts,v.data());
          emitFan(v);
        } else {
          int n=(int)pts.size();
          int total=0;
          std::vector<float> bufv(n*12);
          for(int i=0;i<n;i++){
            int j=(i+1)%n;
            total=appendThickLine(pts[i].first,pts[i].second,
                                  pts[j].first,pts[j].second,half,bufv.data(),total);
          }
          drawVertsToFBO(fbo,bufv.data(),total,GL_TRIANGLES,r,g,b,a);
        }
        break;
      }
      case Tool::Cross:{
        float arm=min(rx,ry)*0.35f;
        auto v=crossVerts(cx,cy,rx,ry,arm);
        emitFan(v);
        break;
      }
      case Tool::Heart:{
        auto v=heartFan(cx,cy,rx,ry);
        emitFan(v);
        break;
      }
      default: break;
    }
  }

  // ── Flood fill ──────────────────────────────────────────────────────────────
  void doFill(float cx,float cy){
    int w=canvasWidth(),h=canvasHeight();
    int px=int(std::floor(cx)),py=int(std::floor(cy));
    if(px<0||py<0||px>=w||py>=h) return;
    std::vector<uint8_t> buf(size_t(w)*h*4);
    readCommitted(buf.data());
    uint8_t *seed=buf.data()+(py*w+px)*4;
    uint8_t tr=seed[0],tg=seed[1],tb=seed[2];
    RGBA fc=parseHexColor(activeColor);
    pushUndoSnapshotPublic();
    floodFill(buf.data(),w,h,px,py,tr,tg,tb,
              uint8_t(fc.r*255),uint8_t(fc.g*255),uint8_t(fc.b*255));
    uploadToCommitted(buf.data());
    if(onStateChanged)  onStateChanged();
    if(onRedrawNeeded)  onRedrawNeeded();
  }

  // ── Style sync ──────────────────────────────────────────────────────────────
  void syncStyle(){
    RGBA c=parseHexColor(activeColor);
    StrokeStyle s;
    s.r=c.r;s.g=c.g;s.b=c.b;s.a=1.f;
    s.radius=activeSize*0.5f;
    s.opacity=activeOpacity;
    s.tool=kToolBrush;
    setStrokeStyle(s);
    RasterSurface::setOpacity(activeOpacity);
  }
};

// ============================================================================
// §4  PaintApp
// ============================================================================
class PaintApp : public Component {
  State<std::string> activeColor;
  State<double>      activeSize;
  State<double>      activeOpacity;
  State<Tool>        activeTool;
  State<bool>        filledShapes;
  State<bool>        canUndo,canRedo;
  State<double>      zoomLevel;
  State<std::vector<RGBA>> colorHistory;
  State<std::string> statusMsg;

  std::shared_ptr<PaintSurface> surface_;
  CanvasWidget *canvasPtr_ = nullptr;
  HWND mainHwnd_ = nullptr;

  static constexpr double kZoomMin=6.25,kZoomMax=200.0;
  static constexpr int kSidebarWidth=268, kToolbarHeight=48;
  static constexpr int kHintsHeight=24,  kAppBarHeight=32;

  void refreshUndoRedo(){
    if(!surface_) return;
    canUndo.set(surface_->canUndo());
    canRedo.set(surface_->canRedo());
  }
  void syncZoomState(float z){
    double pct=z*100.0;
    if(std::abs(pct-zoomLevel.get())>0.5) zoomLevel.set(pct);
  }
  void applyZoomFromSlider(double pct){
    if(!canvasPtr_) return;
    Viewport&vp=canvasPtr_->viewport();
    float t=float(pct/100.0),cur=vp.zoom();
    if(std::abs(t-cur)<0.0001f) return;
    vp.zoomToward(vp.viewW()*.5f,vp.viewH()*.5f,t/cur);
    canvasPtr_->redraw();
  }
  void doSavePNG(){
    if(!surface_||!canvasPtr_) return;
    std::wstring path=promptSavePath(mainHwnd_);
    if(path.empty()) return;
    if(!surface_->savePNG(path))
      MessageBoxW(mainHwnd_,L"Failed to save PNG.",L"Error",MB_ICONERROR|MB_OK);
  }

public:
  PaintApp()
    : activeColor("#cba6f7",context),activeSize(4.0,context),
      activeOpacity(1.0,context),activeTool(Tool::Brush,context),
      filledShapes(false,context),
      canUndo(false,context),canRedo(false,context),
      zoomLevel(100.0,context),colorHistory({},context),
      statusMsg("",context) {}

  WidgetPtr build() override {
    int screenW=GetSystemMetrics(SM_CXSCREEN);
    int screenH=GetSystemMetrics(SM_CYSCREEN);
    int viewW=screenW-kSidebarWidth;
    int viewH=screenH-kAppBarHeight-kToolbarHeight-kHintsHeight;
    static constexpr int kPaperW=1240,kPaperH=1754;

    auto canvas=RasterCanvas(viewW,viewH,kPaperW,kPaperH);
    surface_=canvas->setSurface<PaintSurface>();
    canvasPtr_=canvas.get();
    mainHwnd_=GetActiveWindow();

    surface_->onStateChanged  =[this](){ refreshUndoRedo(); };
    surface_->onHistoryChanged=[this](const std::vector<RGBA>&h){ colorHistory.set(h); };
    surface_->onStatusMessage =[this](const std::string&m){ statusMsg.set(m); };
    surface_->onRedrawNeeded  =[this](){ if(canvasPtr_) canvasPtr_->redraw(); };
    canvas->onViewportChanged =[this](float z){ syncZoomState(z); };

    activeColor.listen([this](const std::string&col){ if(surface_) surface_->setColor(col); });
    activeSize.listen([this](double sz)  { if(surface_) surface_->setSize(float(sz)); });
    activeOpacity.listen([this](double op){ if(surface_) surface_->setOpacity(float(op)); });
    activeTool.listen([this](Tool t)     { if(surface_) surface_->setActiveTool(t); });
    filledShapes.listen([this](bool f)   { if(surface_) surface_->setFilled(f); });
    zoomLevel.listen([this](double pct)  { applyZoomFromSlider(pct); });

    // ── Shared style helpers ─────────────────────────────────────────────────
    auto sectionLabel=[](const std::string&txt)->WidgetPtr{
      return Text(txt)->setFontSize(9)->setTextColor(RGB(100,105,125))->setFontWeight(FontWeight::Bold);
    };

    COLORREF kAccent=RGB(174,129,255);
    COLORREF kBg1   =RGB(17,17,27);
    COLORREF kBg2   =RGB(24,24,37);
    COLORREF kBorder=RGB(49,50,68);
    COLORREF kText  =RGB(205,214,244);

    auto cardWrap=[&](WidgetPtr child)->WidgetPtr{
      return Container(child)
        ->setBackgroundColor(kBg2)->setBorderRadius(8)
        ->setBorderWidth(1)->setBorderColor(kBorder)
        ->setPaddingAll(10,10,10,10);
    };

    // ── Tool button factory ───────────────────────────────────────────────────
    auto makeToolBtn=[&](Tool tv,const std::string&icon,
                         const std::string&lbl)->WidgetPtr{
      return GestureDetector(
        Container(
          Column(
            Text(icon)->setFontSize(14)->setTextColor(
              activeTool,[tv](const Tool&at){ return at==tv?RGB(203,166,247):RGB(140,140,160); }),
            SizedBox(0,1),
            Text(lbl)->setFontSize(7)->setTextColor(
              activeTool,[tv](const Tool&at){ return at==tv?RGB(220,200,255):RGB(100,100,120); })
          )->setSpacing(0)->setCrossAxisAlignment(CrossAxisAlignment::Center)
        )->setWidth(56)->setHeight(42)->setBorderRadius(6)
         ->setBackgroundColor(activeTool,[tv](const Tool&at){ return at==tv?RGB(40,35,58):RGB(24,24,37); })
         ->setBorderWidth(activeTool,[tv](const Tool&at){ return at==tv?2:1; })
         ->setBorderColor(activeTool,[tv](const Tool&at){ return at==tv?RGB(147,112,219):RGB(49,50,68); })
      )->setOnTap([this,tv](){ activeTool.set(tv); });
    };

    // ── §A  DRAW TOOLS section ────────────────────────────────────────────────
    auto drawRow=std::make_shared<RowWidget>(); drawRow->setSpacing(4);
    drawRow->addChild(makeToolBtn(Tool::Brush, "🖌","Brush"));
    drawRow->addChild(makeToolBtn(Tool::Eraser,"⬜","Eraser"));
    drawRow->addChild(makeToolBtn(Tool::Fill,  "🪣","Fill"));

    auto drawSection=cardWrap(
      Column(sectionLabel("DRAW"),SizedBox(0,8),drawRow)->setSpacing(0)
    );

    // ── §B  SHAPE TOOLS section ───────────────────────────────────────────────
    struct SD{ Tool t; std::string icon; std::string label; };
    const std::vector<SD> basicShapes={
      {Tool::Line,       "╱",  "Line"},
      {Tool::Rect,       "▭",  "Rect"},
      {Tool::RoundRect,  "▢",  "RndRect"},
      {Tool::Ellipse,    "⬭",  "Ellipse"},
    };
    const std::vector<SD> polyShapes={
      {Tool::Triangle,      "△",  "Triangle"},
      {Tool::RightTriangle, "◺",  "R.Tri"},
      {Tool::Diamond,       "◇",  "Diamond"},
      {Tool::Pentagon,      "⬠",  "Pentagon"},
    };
    const std::vector<SD> starShapes={
      {Tool::Hexagon,    "⬡",  "Hexagon"},
      {Tool::Star5,      "★",  "Star 5"},
      {Tool::Star6,      "✡",  "Star 6"},
      {Tool::Cross,      "✛",  "Cross"},
    };
    const std::vector<SD> specialShapes={
      {Tool::Arrow,       "→",  "Arrow"},
      {Tool::ArrowDouble, "↔",  "Dbl Arrow"},
      {Tool::Parallelogram,"▱", "Parallel."},
      {Tool::Trapezoid,  "⏢",  "Trapezoid"},
    };
    const std::vector<SD> extraShapes={
      {Tool::Heart,      "♥",  "Heart"},
    };

    auto makeShapeRow=[&](const std::vector<SD>&shapes)->std::shared_ptr<RowWidget>{
      auto row=std::make_shared<RowWidget>(); row->setSpacing(4);
      for(auto&s:shapes) row->addChild(makeToolBtn(s.t,s.icon,s.label));
      return row;
    };

    // Fill toggle
    auto fillToggle=GestureDetector(
      Container(
        Row(
          Container(nullptr)->setWidth(14)->setHeight(14)->setBorderRadius(3)
           ->setBackgroundColor(filledShapes,[](const bool&f){ return f?RGB(174,129,255):RGB(17,17,27); })
           ->setBorderWidth(1)->setBorderColor(RGB(147,112,219)),
          SizedBox(6,0),
          Text("Fill shapes")->setFontSize(10)->setTextColor(RGB(160,165,190))
        )->setSpacing(0)->setCrossAxisAlignment(CrossAxisAlignment::Center)
      )->setPaddingAll(6,4,6,4)->setBorderRadius(5)
       ->setBackgroundColor(RGB(24,24,37))->setBorderWidth(1)->setBorderColor(kBorder)
    )->setOnTap([this](){ filledShapes.set(!filledShapes.get()); });

    auto shapeSection=cardWrap(
      Column(
        sectionLabel("SHAPES"),SizedBox(0,8),
        makeShapeRow(basicShapes),   SizedBox(0,4),
        makeShapeRow(polyShapes),    SizedBox(0,4),
        makeShapeRow(starShapes),    SizedBox(0,4),
        makeShapeRow(specialShapes), SizedBox(0,4),
        makeShapeRow(extraShapes),   SizedBox(0,8),
        fillToggle
      )->setSpacing(0)
    );

    // ── §C  Color ─────────────────────────────────────────────────────────────
    auto selectedSwatch=cardWrap(
      Row(
        Container(nullptr)->setWidth(32)->setHeight(32)->setBorderRadius(6)
         ->setBackgroundColor(activeColor,[](const std::string&c){ return hexToRef(c); })
         ->setBorderWidth(1)->setBorderColor(RGB(69,71,90)),
        SizedBox(8,0),
        Column(
          Text("Selected")->setFontSize(9)->setTextColor(RGB(100,105,125)),
          SizedBox(0,2),
          Text(activeColor,[](const std::string&c){ return c; })
           ->setFontSize(11)->setTextColor(kText)
        )->setSpacing(0)
      )->setSpacing(0)->setCrossAxisAlignment(CrossAxisAlignment::Center)
    );

    auto picker=ColorPicker(hexToRef(activeColor.get()))->setShowAlpha(false)
      ->setOnColorChanged([this](COLORREF c){ activeColor.set(refToHex(c)); });
    auto pickerSection=cardWrap(picker);

    // Color history
    auto historyList=ListView(colorHistory)->itemBuilder([this](int,const RGBA&c)->WidgetPtr{
      char hex[8]; _snprintf_s(hex,sizeof(hex),_TRUNCATE,"#%02x%02x%02x",
        int(c.r*255),int(c.g*255),int(c.b*255));
      std::string col=hex;
      return GestureDetector(
        Container(nullptr)->setWidth(20)->setHeight(20)->setBorderRadius(10)
         ->setBackgroundColor(hexToRef(col))->setBorderWidth(1)->setBorderColor(RGB(80,80,100))
      )->setOnTap([this,col](){ activeColor.set(col); });
    })->setHorizontal(true)->setSpacing(4)->setScrollbarSize(3)
      ->setScrollbarColor(RGB(60,62,80))->setScrollbarHoverColor(RGB(100,102,120));

    auto historySection=cardWrap(
      Column(
        sectionLabel("RECENT COLORS"),SizedBox(0,8),
        Conditional(colorHistory,[](const std::vector<RGBA>&h){ return !h.empty(); })
          ->Then([&]{ return SizedBox(224,22,historyList); })
          ->Else([&]{ return Text("Paint a stroke to record colors")
              ->setFontSize(9)->setTextColor(RGB(70,72,90)); })
      )->setSpacing(0)
    );

    // Palette
    auto pc1=std::make_shared<RowWidget>(); pc1->setSpacing(4);
    auto pc2=std::make_shared<RowWidget>(); pc2->setSpacing(4);
    for(int i=0;i<(int)surface_->kPalette.size();i++){
      const auto&[col,lbl]=surface_->kPalette[i];
      std::string c=col;
      auto sw=GestureDetector(
        Container(nullptr)->setWidth(20)->setHeight(20)->setBorderRadius(10)
         ->setBackgroundColor(hexToRef(col))
         ->setBorderWidth(activeColor,[c](const std::string&a){ return a==c?2:0; })
         ->setBorderColor(activeColor,[c](const std::string&a){ return a==c?RGB(255,255,255):RGB(0,0,0); })
      )->setOnTap([this,c](){ activeColor.set(c); });
      if(i%2==0) pc1->addChild(sw); else pc2->addChild(sw);
    }
    auto paletteSection=cardWrap(
      Column(sectionLabel("PRESETS"),SizedBox(0,8),
        Column(pc1,SizedBox(0,4),pc2)->setSpacing(0)
      )->setSpacing(0)
    );

    // Size & Opacity
    auto sizeSection=cardWrap(
      Column(
        Row(sectionLabel("SIZE"),SizedBox(6,0),
          Text(activeSize,[](double v){ return std::to_string(int(std::round(v)))+"px"; })
           ->setFontSize(9)->setTextColor(RGB(160,160,180))
        )->setSpacing(0)->setCrossAxisAlignment(CrossAxisAlignment::Center),
        SizedBox(0,8),
        Slider(1.0,40.0,0.5)->setValue(activeSize)->setTrackFillColor(kAccent)->setWidth(224)
      )->setSpacing(0)
    );
    auto opacitySection=cardWrap(
      Column(
        Row(sectionLabel("OPACITY"),SizedBox(6,0),
          Text(activeOpacity,[](double op){ return std::to_string(int(std::round(op*100)))+"%"; })
           ->setFontSize(9)->setTextColor(RGB(160,160,180))
        )->setSpacing(0)->setCrossAxisAlignment(CrossAxisAlignment::Center),
        SizedBox(0,8),
        Slider(0.0,1.0,0.01)->setValue(activeOpacity)->setTrackFillColor(kAccent)->setWidth(224)
      )->setSpacing(0)
    );

    // ── §D  Sidebar assembly ──────────────────────────────────────────────────
    auto sidebar=Container(
      Column(
        drawSection,   SizedBox(0,6),
        shapeSection,  SizedBox(0,6),
        selectedSwatch,SizedBox(0,6),
        pickerSection, SizedBox(0,6),
        historySection,SizedBox(0,6),
        paletteSection,SizedBox(0,6),
        sizeSection,   SizedBox(0,6),
        opacitySection
      )->setSpacing(0)
    )->setWidth(kSidebarWidth)->setBackgroundColor(kBg1)->setPaddingAll(10,10,10,10);

    // ── §E  Toolbar ───────────────────────────────────────────────────────────
    auto makeBtn=[&](const std::string&lbl,COLORREF txt,std::function<void()>fn)->WidgetPtr{
      return Button(lbl,fn)->setBackgroundColor(RGB(30,30,46))->setTextColor(txt)
        ->setBorderRadius(5)->setHeight(26)->setPadding(4);
    };
    auto toolbar=Container(
      Row(
        Text("Zoom")->setFontSize(11)->setTextColor(RGB(140,140,160)),
        Slider(kZoomMin,kZoomMax,0.25)->setValue(zoomLevel)->setTrackFillColor(kAccent)->setWidth(110),
        Text(zoomLevel,[](double v){ return std::to_string(int(std::round(v)))+"%"; })
         ->setFontSize(11)->setTextColor(RGB(180,180,200))->setMinWidth(38),
        Button("1:1",[this]{ if(canvasPtr_){ canvasPtr_->viewport().resetZoom(); canvasPtr_->redraw(); syncZoomState(1.f); } })
          ->setBackgroundColor(RGB(24,24,37))->setTextColor(kAccent)->setBorderRadius(5)->setWidth(30)->setHeight(26)->setPadding(4),
        Button("Fit",[this]{ if(canvasPtr_){ canvasPtr_->viewport().fitToView(); canvasPtr_->redraw(); syncZoomState(canvasPtr_->viewport().zoom()); } })
          ->setBackgroundColor(RGB(24,24,37))->setTextColor(kAccent)->setBorderRadius(5)->setWidth(30)->setHeight(26)->setPadding(4),
        SizedBox(12,0),
        makeBtn("↩ Undo",RGB(137,180,250),[this]{ if(surface_){ surface_->undo(); refreshUndoRedo(); if(canvasPtr_) canvasPtr_->redraw(); } }),
        makeBtn("Redo ↪",RGB(166,226,46), [this]{ if(surface_){ surface_->redo(); refreshUndoRedo(); if(canvasPtr_) canvasPtr_->redraw(); } }),
        makeBtn("✕ Clear",RGB(243,139,168),[this]{ if(surface_){ surface_->clear(); refreshUndoRedo(); if(canvasPtr_) canvasPtr_->redraw(); } }),
        makeBtn("💾 Save",RGB(148,226,213),[this]{ doSavePNG(); })
      )->setSpacing(8)->setCrossAxisAlignment(CrossAxisAlignment::Center)
    )->setBackgroundColor(kBg1)->setPaddingAll(10,7,10,7)->setHeight(kToolbarHeight);

    // ── §F  Context menu ──────────────────────────────────────────────────────
    auto canvasWithMenu=ContextMenu(canvas,{
      {"Brush (B)",       [this]{ activeTool.set(Tool::Brush);   }},
      {"Eraser (E)",      [this]{ activeTool.set(Tool::Eraser);  }},
      {"Fill (F)",        [this]{ activeTool.set(Tool::Fill);    }},
      ContextMenuItem::Separator(),
      {"Line (L)",        [this]{ activeTool.set(Tool::Line);    }},
      {"Rectangle (R)",   [this]{ activeTool.set(Tool::Rect);    }},
      {"Ellipse",         [this]{ activeTool.set(Tool::Ellipse); }},
      {"Triangle",        [this]{ activeTool.set(Tool::Triangle);}},
      {"Diamond",         [this]{ activeTool.set(Tool::Diamond); }},
      {"Star",            [this]{ activeTool.set(Tool::Star5);   }},
      {"Arrow",           [this]{ activeTool.set(Tool::Arrow);   }},
      ContextMenuItem::Separator(),
      {"Undo",            [this]{ if(surface_){ surface_->undo(); refreshUndoRedo(); if(canvasPtr_) canvasPtr_->redraw(); } }},
      {"Redo",            [this]{ if(surface_){ surface_->redo(); refreshUndoRedo(); if(canvasPtr_) canvasPtr_->redraw(); } }},
      ContextMenuItem::Separator(),
      {"Zoom 1:1",        [this]{ if(canvasPtr_){ canvasPtr_->viewport().resetZoom(); canvasPtr_->redraw(); syncZoomState(1.f); } }},
      {"Fit to view",     [this]{ if(canvasPtr_){ canvasPtr_->viewport().fitToView(); canvasPtr_->redraw(); syncZoomState(canvasPtr_->viewport().zoom()); } }},
      ContextMenuItem::Separator(),
      {"Save as PNG",     [this]{ doSavePNG(); }},
      {"Clear",           [this]{ if(surface_){ surface_->clear(); refreshUndoRedo(); if(canvasPtr_) canvasPtr_->redraw(); } }},
    });

    // ── §G  Hints strip ───────────────────────────────────────────────────────
    auto hints=Container(
      Conditional(statusMsg,[](const std::string&s){ return !s.empty(); })
        ->Then([&]{ return Text(statusMsg,[](const std::string&s){ return s; })
            ->setFontSize(10)->setTextColor(RGB(148,226,213)); })
        ->Else([&]{ return Row(
            Text("MMB/Space: Pan")->setFontSize(10)->setTextColor(RGB(75,75,95)),SizedBox(10,0),
            Text("Ctrl+Scroll: Zoom")->setFontSize(10)->setTextColor(RGB(75,75,95)),SizedBox(10,0),
            Text("Ctrl+Z/Y: Undo/Redo")->setFontSize(10)->setTextColor(RGB(75,75,95)),SizedBox(10,0),
            Text("B E L R F: Tools")->setFontSize(10)->setTextColor(RGB(75,75,95))
          )->setSpacing(0); })
    )->setBackgroundColor(kBg1)->setPaddingAll(10,3,10,3)->setHeight(kHintsHeight);

    auto canvasColumn=Column(toolbar,hints,canvasWithMenu)->setSpacing(0);
    auto root=Row(sidebar,canvasColumn)->setSpacing(0);
    return Scaffold(root);
  }
};

// ── Entry point ───────────────────────────────────────────────────────────────
int WINAPI WinMain(HINSTANCE hInstance,HINSTANCE,LPSTR,int){
  FluxUI app(hInstance);
  app.build([&](){ return FluxApp("Paint",BuildComponent<PaintApp>(),AppTheme::dark()); });
  RECT wa; SystemParametersInfo(SPI_GETWORKAREA,0,&wa,0);
  app.createWindow("FluxUI - Paint",wa.right-wa.left,wa.bottom-wa.top);
  return app.run();
}