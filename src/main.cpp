// =============================================================================
// logic_sim.hpp  —  Logic Gate Simulator  v10  (IEEE/ANSI symbols + CLOCK gate)
// =============================================================================

#pragma once
#include "flux.hpp"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <windowsx.h>

#include <algorithm>
#include <unordered_map>
#include <cmath>
#include <functional>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#pragma comment(lib,"comdlg32.lib")

// ---------------------------------------------------------------------------
// Extra GL procs
// ---------------------------------------------------------------------------
using PFNGLUNIFORM4FPROC_LS = void(APIENTRY*)(GLint,GLfloat,GLfloat,GLfloat,GLfloat);
using PFNGLUNIFORM2FPROC_LS = void(APIENTRY*)(GLint,GLfloat,GLfloat);
namespace ls_gl {
    inline PFNGLUNIFORM4FPROC_LS uniform4f = nullptr;
    inline PFNGLUNIFORM2FPROC_LS uniform2f = nullptr;
    inline void init() {
        if (uniform4f) return;
        HMODULE gl32 = GetModuleHandleA("opengl32.dll");
        auto load = [&](const char* n) -> void* {
            void* p = (void*)wglGetProcAddress(n);
            if (!p||p==(void*)1||p==(void*)2||p==(void*)3||p==(void*)-1)
                p = (void*)GetProcAddress(gl32,n);
            return p;
        };
        uniform4f = (PFNGLUNIFORM4FPROC_LS)load("glUniform4f");
        uniform2f = (PFNGLUNIFORM2FPROC_LS)load("glUniform2f");
        assert(uniform4f && uniform2f);
    }
}

// =============================================================================
// §0  MINIMAL JSON
// =============================================================================
namespace js {
    static std::string esc(const std::string& s){
        std::string r;r.reserve(s.size()+2);r+='"';
        for(char c:s){
            if(c=='"') r+="\\\"";
            else if(c=='\\') r+="\\\\";
            else if(c=='\n') r+="\\n";
            else r+=c;
        }
        r+='"';return r;
    }
    static std::string kv(const std::string& k,const std::string& v){ return esc(k)+":"+v; }
    static std::string kv(const std::string& k,int   v){ return esc(k)+":"+std::to_string(v); }
    static std::string kv(const std::string& k,float v){ char b[32];snprintf(b,32,"%.4g",v);return esc(k)+":"+b; }
    static std::string kv(const std::string& k,bool  v){ return esc(k)+":"+(v?"true":"false"); }
    static std::string obj(std::initializer_list<std::string> fs){
        std::string r="{";bool f=true;
        for(auto& x:fs){if(!f)r+=",";r+=x;f=false;}
        return r+"}";
    }
    static std::string arr(const std::vector<std::string>& items){
        std::string r="[";bool f=true;
        for(auto& x:items){if(!f)r+=",";r+=x;f=false;}
        return r+"]";
    }
    static void skipWS(const std::string& s,size_t& p){
        while(p<s.size()&&(s[p]==' '||s[p]=='\t'||s[p]=='\r'||s[p]=='\n'))++p;
    }
    static std::string readStr(const std::string& s,size_t& p){
        if(p>=s.size()||s[p]!='"')return{};
        ++p;std::string r;
        while(p<s.size()&&s[p]!='"'){
            if(s[p]=='\\'&&p+1<s.size()){++p;if(s[p]=='n')r+='\n';else r+=s[p];}
            else r+=s[p];
            ++p;
        }
        if(p<s.size())++p;return r;
    }
    static std::string grabVal(const std::string& s,size_t& p){
        skipWS(s,p);if(p>=s.size())return{};
        char o=s[p];
        if(o=='{'||o=='['){
            char c=(o=='{')? '}':']';int d=0;size_t st=p;
            while(p<s.size()){
                if(s[p]==o)d++;
                else if(s[p]==c){d--;if(d==0){++p;break;}}
                else if(s[p]=='"'){++p;while(p<s.size()&&s[p]!='"'){if(s[p]=='\\')++p;++p;}}
                ++p;
            }
            return s.substr(st,p-st);
        }
        if(o=='"'){size_t st=p;readStr(s,p);return s.substr(st,p-st);}
        size_t st=p;
        while(p<s.size()&&s[p]!=','&&s[p]!='}'&&s[p]!=']')++p;
        return s.substr(st,p-st);
    }
    using KVList = std::vector<std::pair<std::string,std::string>>;
    static KVList parseObj(const std::string& s,size_t& p){
        KVList out;skipWS(s,p);if(p>=s.size()||s[p]!='{')return out;++p;
        while(p<s.size()&&s[p]!='}'){
            skipWS(s,p);if(s[p]=='}')break;
            auto key=readStr(s,p);
            skipWS(s,p);if(p<s.size()&&s[p]==':')++p;
            auto val=grabVal(s,p);
            out.push_back({key,val});
            skipWS(s,p);if(p<s.size()&&s[p]==',')++p;
        }
        if(p<s.size()&&s[p]=='}')++p;return out;
    }
    static std::vector<std::string> parseArr(const std::string& s){
        std::vector<std::string> out;size_t p=0;
        skipWS(s,p);if(p>=s.size()||s[p]!='[')return out;++p;
        while(p<s.size()&&s[p]!=']'){
            skipWS(s,p);if(s[p]==']')break;
            out.push_back(grabVal(s,p));
            skipWS(s,p);if(p<s.size()&&s[p]==',')++p;
        }
        return out;
    }
    static int   asInt  (const std::string& v){try{return std::stoi(v);}catch(...){return 0;}}
    static float asFloat(const std::string& v){try{return std::stof(v);}catch(...){return 0.f;}}
    static bool  asBool (const std::string& v){return v=="true"||v=="1";}
    static std::string asStr(const std::string& v){
        if(v.size()>=2&&v.front()=='"'&&v.back()=='"'){
            size_t p=0;return readStr(v,p);
        }
        return v;
    }
}

// =============================================================================
// §1  DATA MODEL
// =============================================================================

// CLOCK is appended after OUTPUT — all existing indices are preserved
enum class GateType { AND=0,OR,NOT,NAND,NOR,XOR,XNOR,INPUT,OUTPUT,CLOCK,COUNT };
static constexpr int kGTC=(int)GateType::COUNT;
//                                         AND OR NOT NAND NOR XOR XNOR IN  OUT CLK
static constexpr int kInPins [kGTC]={       2, 2,  1,  2,  2,  2,   2,  0,  1,  0};
static constexpr int kOutPins[kGTC]={       1, 1,  1,  1,  1,  1,   1,  1,  0,  1};
static const char*   kGateName[kGTC]={"AND","OR","NOT","NAND","NOR","XOR","XNOR","INPUT","OUTPUT","CLOCK"};

// Clock frequency presets (Hz) – cycled with double-click or via label
static constexpr float kClockFreqPresets[] = {0.5f, 1.f, 2.f, 4.f, 8.f};
static constexpr int   kClockFreqCount = 5;

static constexpr float kGW=88.f,kGH=60.f,kPinR=5.f,kPinLen=16.f,kCorner=8.f;
static constexpr float kGrid=20.f;

static float snapGrid(float v){ return std::round(v/kGrid)*kGrid; }

struct Pin { float cx=0,cy=0; bool connected=false; };

struct Gate {
    int      id=0;
    GateType type=GateType::AND;
    float    x=0,y=0;
    bool     selected=false;
    bool     inputVal=false;    // current logical value (CLOCK toggles this)
    bool     simVal=false;
    std::string label;
    // CLOCK-specific: frequency in Hz (stored so it survives save/load)
    float    clockHz=1.f;
    std::vector<Pin> inPins,outPins;

    void recomputePins(){
        int ni=kInPins[(int)type],no=kOutPins[(int)type];
        inPins.resize(ni);outPins.resize(no);
        float L=x-kGW*.5f,R=x+kGW*.5f,B=y-kGH*.5f;
        for(int i=0;i<ni;i++){float t=(ni==1)?.5f:float(i)/(ni-1);inPins[i]={L-kPinLen,B+t*kGH};}
        for(int i=0;i<no;i++){float t=(no==1)?.5f:float(i)/(no-1);outPins[i]={R+kPinLen,B+t*kGH};}
    }
    std::string toJson() const {
        return js::obj({js::kv("id",id),js::kv("type",(int)type),
                        js::kv("x",x),js::kv("y",y),
                        js::kv("inputVal",inputVal),js::kv("label",label),
                        js::kv("clockHz",clockHz)});
    }
    void fromKV(const js::KVList& m){
        for(auto&[k,v]:m){
            if(k=="id")       id=js::asInt(v);
            else if(k=="type")type=(GateType)js::asInt(v);
            else if(k=="x")   x=js::asFloat(v);
            else if(k=="y")   y=js::asFloat(v);
            else if(k=="inputVal")inputVal=js::asBool(v);
            else if(k=="label")   label=js::asStr(v);
            else if(k=="clockHz") clockHz=js::asFloat(v);
        }
        if(clockHz<=0.f)clockHz=1.f;
        recomputePins();
    }
};

struct WireEndpoint{int gateId=-1;int pinIdx=0;bool isOutput=false;};

struct Wire {
    int          id=0;
    WireEndpoint src,dst;
    bool         value=false;
    bool         selected=false;
    std::string toJson() const {
        return js::obj({js::kv("id",id),
                        js::kv("srcId",src.gateId),js::kv("srcPin",src.pinIdx),
                        js::kv("dstId",dst.gateId),js::kv("dstPin",dst.pinIdx)});
    }
    void fromKV(const js::KVList& m){
        for(auto&[k,v]:m){
            if     (k=="id")    id=js::asInt(v);
            else if(k=="srcId") src.gateId=js::asInt(v);
            else if(k=="srcPin")src.pinIdx=js::asInt(v);
            else if(k=="dstId") dst.gateId=js::asInt(v);
            else if(k=="dstPin")dst.pinIdx=js::asInt(v);
        }
        src.isOutput=true;dst.isOutput=false;
    }
};

struct PinRef{int gateId;int pinIdx;bool isOutput;float cx,cy;};
struct TruthRow{std::vector<bool>inputs,outputs;};
enum class SimMode{Edit,Running,Paused};

// ── Circuit ───────────────────────────────────────────────────────────────────
struct Circuit {
    std::vector<Gate> gates;
    std::vector<Wire> wires;
    int nextId=1,nextWireId=1;

    int add(GateType t,float x,float y,const std::string& lbl=""){
        Gate g;g.id=nextId++;g.type=t;g.x=x;g.y=y;g.label=lbl;
        if(t==GateType::CLOCK)g.clockHz=1.f;
        g.recomputePins();gates.push_back(g);return g.id;
    }
    Gate* find(int id){for(auto&g:gates)if(g.id==id)return&g;return nullptr;}
    const Gate* find(int id)const{for(auto&g:gates)if(g.id==id)return&g;return nullptr;}
    void remove(int id){
        gates.erase(std::remove_if(gates.begin(),gates.end(),
            [id](const Gate&g){return g.id==id;}),gates.end());
    }
    int connect(int sId,int sPin,int dId,int dPin){
        Wire w;w.id=nextWireId++;
        w.src={sId,sPin,true};w.dst={dId,dPin,false};
        wires.push_back(w);
        markPin(sId,sPin,true,true);markPin(dId,dPin,false,true);
        return w.id;
    }
    void disconnect(int wireId){
        for(int i=int(wires.size())-1;i>=0;--i){
            if(wires[i].id!=wireId)continue;
            auto&w=wires[i];
            recomputePin(w.src.gateId,w.src.pinIdx,true, wireId);
            recomputePin(w.dst.gateId,w.dst.pinIdx,false,wireId);
            wires.erase(wires.begin()+i);return;
        }
    }
    void removeGateWires(int gateId){
        std::vector<int>ids;
        for(auto&w:wires)if(w.src.gateId==gateId||w.dst.gateId==gateId)ids.push_back(w.id);
        for(int id:ids)disconnect(id);
    }
    void clearAll(){gates.clear();wires.clear();nextId=1;nextWireId=1;}

    void rebuildConnected(){
        for(auto&g:gates){for(auto&p:g.inPins)p.connected=false;for(auto&p:g.outPins)p.connected=false;}
        for(auto&w:wires){markPin(w.src.gateId,w.src.pinIdx,true,true);markPin(w.dst.gateId,w.dst.pinIdx,false,true);}
    }

    void evaluate(){
        for(int idx:topoSort()){
            Gate&g=gates[idx];
            g.simVal=computeGate(g);
            for(auto&w:wires)if(w.src.gateId==g.id)w.value=g.simVal;
        }
    }

    std::vector<TruthRow> buildTruthTable(std::vector<int>&inIds,std::vector<int>&outIds)const{
        inIds.clear();outIds.clear();
        std::vector<int>inIdx,outIdx;
        for(int i=0;i<(int)gates.size();i++){
            // CLOCK behaves like INPUT for truth table purposes
            if(gates[i].type==GateType::INPUT||gates[i].type==GateType::CLOCK)
               {inIdx.push_back(i); inIds.push_back(gates[i].id);}
            if(gates[i].type==GateType::OUTPUT){outIdx.push_back(i);outIds.push_back(gates[i].id);}
        }
        if(inIdx.empty()||outIdx.empty())return{};
        int ni=(int)inIdx.size(),combos=1<<min(ni,6);
        std::vector<Gate>gs=gates;std::vector<Wire>ws=wires;

        auto topo=[&]()->std::vector<int>{
            int sz=(int)gs.size();std::vector<int>ind(sz,0);
            for(auto&w:ws)for(int i=0;i<sz;i++)if(gs[i].id==w.dst.gateId){ind[i]++;break;}
            std::vector<int>q,r;
            for(int i=0;i<sz;i++)if(ind[i]==0)q.push_back(i);
            while(!q.empty()){int c=q.back();q.pop_back();r.push_back(c);
                for(auto&w:ws){if(w.src.gateId!=gs[c].id)continue;
                    for(int i=0;i<sz;i++)if(gs[i].id==w.dst.gateId&&--ind[i]==0){q.push_back(i);break;}}}
            if((int)r.size()<sz){std::vector<bool>vis(sz,false);for(int i:r)vis[i]=true;
                for(int i=0;i<sz;i++)if(!vis[i])r.push_back(i);}
            return r;
        };
        auto eval=[&](){
            for(int idx:topo()){
                Gate&g=gs[idx];
                if(g.type==GateType::INPUT||g.type==GateType::CLOCK){g.simVal=g.inputVal;}
                else{bool ins[2]={};
                    for(auto&w:ws)if(w.dst.gateId==g.id&&w.dst.pinIdx<2)ins[w.dst.pinIdx]=w.value;
                    bool a=ins[0],b=ins[1];
                    switch(g.type){
                        case GateType::AND:  g.simVal=a&&b;break;
                        case GateType::OR:   g.simVal=a||b;break;
                        case GateType::NOT:  g.simVal=!a;break;
                        case GateType::NAND: g.simVal=!(a&&b);break;
                        case GateType::NOR:  g.simVal=!(a||b);break;
                        case GateType::XOR:  g.simVal=a^b;break;
                        case GateType::XNOR: g.simVal=!(a^b);break;
                        default:             g.simVal=false;break;
                    }
                }
                for(auto&w:ws)if(w.src.gateId==g.id)w.value=g.simVal;
            }
        };
        std::vector<TruthRow>table;
        for(int mask=0;mask<combos;mask++){
            for(int b=0;b<ni;b++)gs[inIdx[b]].inputVal=(mask>>b)&1;
            eval();
            TruthRow row;
            for(int i:inIdx) row.inputs.push_back(gs[i].inputVal);
            for(int i:outIdx)row.outputs.push_back(gs[i].simVal);
            table.push_back(row);
        }
        return table;
    }

    std::string toJson()const{
        std::vector<std::string>ga,wa;
        for(auto&g:gates)ga.push_back(g.toJson());
        for(auto&w:wires)wa.push_back(w.toJson());
        return js::obj({js::kv("version",1),js::kv("nextId",nextId),
                        js::kv("nextWireId",nextWireId),
                        "\"gates\":"+js::arr(ga),"\"wires\":"+js::arr(wa)});
    }
    bool fromJson(const std::string&src){
        clearAll();size_t p=0;
        for(auto&[k,v]:js::parseObj(src,p)){
            if(k=="nextId")     nextId=js::asInt(v);
            else if(k=="nextWireId")nextWireId=js::asInt(v);
            else if(k=="gates"){
                for(auto&gj:js::parseArr(v)){
                    size_t gp=0;Gate g;g.fromKV(js::parseObj(gj,gp));gates.push_back(g);}
            }
            else if(k=="wires"){
                for(auto&wj:js::parseArr(v)){
                    size_t wp=0;Wire w;w.fromKV(js::parseObj(wj,wp));wires.push_back(w);}
            }
        }
        rebuildConnected();return !gates.empty();
    }

private:
    std::vector<int> topoSort()const{
        int sz=(int)gates.size();std::vector<int>ind(sz,0);
        for(auto&w:wires)for(int i=0;i<sz;i++)if(gates[i].id==w.dst.gateId){ind[i]++;break;}
        std::vector<int>q,r;
        for(int i=0;i<sz;i++)if(ind[i]==0)q.push_back(i);
        while(!q.empty()){int c=q.back();q.pop_back();r.push_back(c);
            for(auto&w:wires){if(w.src.gateId!=gates[c].id)continue;
                for(int i=0;i<sz;i++)if(gates[i].id==w.dst.gateId&&--ind[i]==0){q.push_back(i);break;}}}
        if((int)r.size()<sz){std::vector<bool>vis(sz,false);for(int i:r)vis[i]=true;
            for(int i=0;i<sz;i++)if(!vis[i])r.push_back(i);}
        return r;
    }
    bool computeGate(const Gate&g)const{
        if(g.type==GateType::INPUT||g.type==GateType::CLOCK)return g.inputVal;
        bool ins[2]={};
        for(auto&w:wires)if(w.dst.gateId==g.id&&w.dst.pinIdx<2)ins[w.dst.pinIdx]=w.value;
        bool a=ins[0],b=ins[1];
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
    void markPin(int gateId,int pinIdx,bool isOut,bool val){
        Gate*g=find(gateId);if(!g)return;
        if( isOut&&pinIdx<(int)g->outPins.size())g->outPins[pinIdx].connected=val;
        if(!isOut&&pinIdx<(int)g->inPins.size()) g->inPins[pinIdx].connected=val;
    }
    void recomputePin(int gateId,int pinIdx,bool isOut,int skipId){
        bool still=false;
        for(auto&w:wires){
            if(w.id==skipId)continue;
            if( isOut&&w.src.gateId==gateId&&w.src.pinIdx==pinIdx){still=true;break;}
            if(!isOut&&w.dst.gateId==gateId&&w.dst.pinIdx==pinIdx){still=true;break;}
        }
        markPin(gateId,pinIdx,isOut,still);
    }
};

// =============================================================================
// §2  VERTEX HELPERS
// =============================================================================

static void pushQuad(std::vector<float>&v,float x0,float y0,float x1,float y1){
    v.insert(v.end(),{x0,y0,x1,y0,x1,y1,x1,y1,x0,y1,x0,y0});
}
static void pushLine(std::vector<float>&v,float x0,float y0,float x1,float y1,float hw){
    float dx=x1-x0,dy=y1-y0,len=sqrtf(dx*dx+dy*dy);
    if(len<0.001f){pushQuad(v,x0-hw,y0-hw,x0+hw,y0+hw);return;}
    float nx=-dy/len*hw,ny=dx/len*hw;
    v.insert(v.end(),{x0+nx,y0+ny,x0-nx,y0-ny,x1-nx,y1-ny,x1-nx,y1-ny,x1+nx,y1+ny,x0+nx,y0+ny});
}
static void pushCircle(std::vector<float>&v,float cx,float cy,float r,int s=16){
    for(int i=0;i<s;i++){
        float a0=float(i)/s*6.2831853f,a1=float(i+1)/s*6.2831853f;
        v.insert(v.end(),{cx,cy,cx+cosf(a0)*r,cy+sinf(a0)*r,cx+cosf(a1)*r,cy+sinf(a1)*r});
    }
}
static void pushRRFill(std::vector<float>&v,float L,float B,float R2,float T,float cr){
    cr=min(cr,min((R2-L)*.45f,(T-B)*.45f));
    pushQuad(v,L+cr,B,R2-cr,T);pushQuad(v,L,B+cr,L+cr,T-cr);pushQuad(v,R2-cr,B+cr,R2,T-cr);
    float ccx[4]={R2-cr,L+cr,L+cr,R2-cr},ccy[4]={T-cr,T-cr,B+cr,B+cr},sa[4]={0,1.5708f,3.1416f,4.7124f};
    for(int c=0;c<4;c++)for(int i=0;i<8;i++){
        float a0=sa[c]+float(i)/8*1.5708f,a1=sa[c]+float(i+1)/8*1.5708f;
        v.insert(v.end(),{ccx[c],ccy[c],ccx[c]+cosf(a0)*cr,ccy[c]+sinf(a0)*cr,ccx[c]+cosf(a1)*cr,ccy[c]+sinf(a1)*cr});
    }
}
static void pushRR(std::vector<float>&v,float L,float B,float R2,float T,float cr,float hw,int cs=6){
    cr=min(cr,min((R2-L)*.4f,(T-B)*.4f));
    pushLine(v,L+cr,B,R2-cr,B,hw);pushLine(v,R2,B+cr,R2,T-cr,hw);
    pushLine(v,R2-cr,T,L+cr,T,hw);pushLine(v,L,T-cr,L,B+cr,hw);
    float ox[4]={R2-cr,L+cr,L+cr,R2-cr},oy[4]={T-cr,T-cr,B+cr,B+cr},sa[4]={0,1.5708f,3.1416f,4.7124f};
    for(int c=0;c<4;c++)for(int i=0;i<cs;i++){
        float a0=sa[c]+float(i)/cs*1.5708f,a1=sa[c]+float(i+1)/cs*1.5708f;
        pushLine(v,ox[c]+cosf(a0)*cr,oy[c]+sinf(a0)*cr,ox[c]+cosf(a1)*cr,oy[c]+sinf(a1)*cr,hw);
    }
}
static void bezierPt(float x0,float y0,float x1,float y1,float x2,float y2,float x3,float y3,
                     float t,float&ox,float&oy){
    float u=1-t,u2=u*u,u3=u2*u,t2=t*t,t3=t2*t;
    ox=u3*x0+3*u2*t*x1+3*u*t2*x2+t3*x3;oy=u3*y0+3*u2*t*y1+3*u*t2*y2+t3*y3;
}
static float bezSpread(float x0,float x3){return fabsf(x3-x0)*0.52f+20.f;}
static void pushBez(std::vector<float>&v,float x0,float y0,float x3,float y3,float hw,int segs=24){
    float sp=bezSpread(x0,x3),x1=x0+sp,y1=y0,x2=x3-sp,y2=y3,px=0,py=0;
    for(int i=0;i<=segs;i++){
        float t=float(i)/segs,qx,qy;bezierPt(x0,y0,x1,y1,x2,y2,x3,y3,t,qx,qy);
        if(i>0)pushLine(v,px,py,qx,qy,hw);px=qx;py=qy;
    }
}
static bool bezHit(float x0,float y0,float x3,float y3,float px,float py,float thr=8.f,int segs=24){
    float sp=bezSpread(x0,x3),x1=x0+sp,y1=y0,x2=x3-sp,y2=y3;
    float t2=thr*thr,lx=x0,ly=y0;
    for(int i=1;i<=segs;i++){
        float t=float(i)/segs,qx,qy;bezierPt(x0,y0,x1,y1,x2,y2,x3,y3,t,qx,qy);
        float dx=qx-lx,dy=qy-ly,len2=dx*dx+dy*dy;
        float s=(len2>0)?((px-lx)*dx+(py-ly)*dy)/len2:0;s=max(0.f,min(1.f,s));
        float cx2=lx+dx*s-px,cy2=ly+dy*s-py;if(cx2*cx2+cy2*cy2<=t2)return true;
        lx=qx;ly=qy;
    }
    return false;
}
static std::vector<PinRef> collectPins(const Circuit&c){
    std::vector<PinRef>out;
    for(auto&g:c.gates){
        for(int i=0;i<(int)g.outPins.size();i++)out.push_back({g.id,i,true, g.outPins[i].cx,g.outPins[i].cy});
        for(int i=0;i<(int)g.inPins.size(); i++)out.push_back({g.id,i,false,g.inPins[i].cx, g.inPins[i].cy});
    }
    return out;
}
static bool nearestPin(const std::vector<PinRef>&pins,float wx,float wy,float radius,
                        bool mustOut,bool mustIn,PinRef&out){
    float best=radius*radius;bool found=false;
    for(auto&p:pins){
        if(mustOut&&!p.isOutput)continue;if(mustIn&&p.isOutput)continue;
        float dx=p.cx-wx,dy=p.cy-wy,d2=dx*dx+dy*dy;
        if(d2<best){best=d2;out=p;found=true;}
    }
    return found;
}

// =============================================================================
// §2b  IEEE/ANSI SHAPE BUILDERS
// =============================================================================

static void pushCubic(std::vector<float>&v,
                       float x0,float y0,float x1,float y1,
                       float x2,float y2,float x3,float y3,
                       float hw,int segs=22){
    float px=x0,py=y0;
    for(int i=1;i<=segs;++i){
        float t=float(i)/float(segs),qx,qy;
        bezierPt(x0,y0,x1,y1,x2,y2,x3,y3,t,qx,qy);
        pushLine(v,px,py,qx,qy,hw);
        px=qx;py=qy;
    }
}

static void pushArc(std::vector<float>&v,
                    float cx,float cy,float r,
                    float a0,float a1,float hw,int segs=24){
    float px=cx+cosf(a0)*r, py=cy+sinf(a0)*r;
    for(int i=1;i<=segs;++i){
        float a=a0+(a1-a0)*float(i)/float(segs);
        float qx=cx+cosf(a)*r, qy=cy+sinf(a)*r;
        pushLine(v,px,py,qx,qy,hw);
        px=qx;py=qy;
    }
}

static void pushRing(std::vector<float>&v,float cx,float cy,float r,float hw){
    pushArc(v,cx,cy,r, 0.f, 6.2831853f, hw, 20);
}

static void pushTriFill(std::vector<float>&v,
                         float L,float yT,float R,float yB){
    float MY=(yT+yB)*0.5f;
    v.insert(v.end(),{L,yT, R,MY, L,yB});
}

static void pushANDFill(std::vector<float>&v,
                         float L,float yT,float R,float yB){
    float H=yB-yT, MY=(yT+yB)*0.5f;
    float flatX = L + (R-L)*0.44f;
    float arcR  = H*0.5f;
    pushQuad(v, L, yT, flatX, yB);
    const int S=24;
    for(int i=0;i<S;++i){
        float a0 = -1.5707963f + float(i)  /S*3.1415926f;
        float a1 = -1.5707963f + float(i+1)/S*3.1415926f;
        v.insert(v.end(),{
            flatX, MY,
            flatX + cosf(a0)*arcR,  MY + sinf(a0)*arcR,
            flatX + cosf(a1)*arcR,  MY + sinf(a1)*arcR
        });
    }
}

static void pushANDOutline(std::vector<float>&v,
                            float L,float yT,float R,float yB,
                            float hw){
    float H=yB-yT, MY=(yT+yB)*0.5f;
    float flatX = L + (R-L)*0.44f;
    float arcR  = H*0.5f;
    pushLine(v, L, yT, L, yB, hw);
    pushLine(v, L, yT, flatX, yT, hw);
    pushLine(v, L, yB, flatX, yB, hw);
    pushArc (v, flatX, MY, arcR, -1.5707963f, 1.5707963f, hw, 28);
}

static void pushORFill(std::vector<float>&v,
                        float L,float yT,float R,float yB){
    float H=yB-yT, W=R-L, MY=(yT+yB)*0.5f;
    float bulgX=W*0.30f;
    const int ROWS=32;
    for(int i=0;i<ROWS;++i){
        float t0=float(i)  /ROWS;
        float t1=float(i+1)/ROWS;
        float y0=yT+t0*H, y1=yT+t1*H;

        auto backX=[&](float t)->float{
            float u=1.f-t;
            return u*u*u*L + 3*u*u*t*(L+bulgX) + 3*u*t*t*(L+bulgX) + t*t*t*L;
        };
        auto frontX=[&](float t)->float{
            float tp=(t<0.5f)?(1.f-t*2.f):((t-0.5f)*2.f);
            float u=1.f-tp;
            return u*u*u*L + 3*u*u*tp*(L+W*0.55f) + 3*u*tp*tp*(R-W*0.12f) + tp*tp*tp*R;
        };

        float lx=backX(t0),  rx=frontX(t0);
        float lx1=backX(t1), rx1=frontX(t1);
        v.insert(v.end(),{lx,y0, rx,y0, rx1,y1});
        v.insert(v.end(),{lx,y0, rx1,y1, lx1,y1});
        (void)lx1;
    }
}

static void pushOROutline(std::vector<float>&v,
                           float L,float yT,float R,float yB,
                           float hw){
    float W=R-L, MY=(yT+yB)*0.5f;
    float bulgX=W*0.30f;

    pushCubic(v, L,yT, L+bulgX,yT, L+bulgX,yB, L,yB, hw);

    pushCubic(v, L,yT,
                 L+W*0.55f, yT,
                 R-W*0.12f, MY - (yB-yT)*0.10f,
                 R, MY,  hw);
    pushCubic(v, L,yB,
                 L+W*0.55f, yB,
                 R-W*0.12f, MY + (yB-yT)*0.10f,
                 R, MY,  hw);
}

static void pushXORBackArc(std::vector<float>&v,
                             float L,float yT,float R,float yB,
                             float hw){
    float W=R-L, bulgX=W*0.30f, offset=W*0.11f;
    pushCubic(v, L-offset,yT, L-offset+bulgX,yT, L-offset+bulgX,yB, L-offset,yB, hw);
}

static void pushTRIOutline(std::vector<float>&v,
                            float L,float yT,float R,float yB,
                            float hw, bool negate){
    float MY=(yT+yB)*0.5f;
    float H=yB-yT;
    float tipX = negate ? (R - H*0.20f) : R;
    pushLine(v, L, yT, L, yB, hw);
    pushLine(v, L, yT, tipX, MY, hw);
    pushLine(v, tipX, MY, L, yB, hw);
}

// ─────────────────────────────────────────────────────────────────────────────
// §2c  CLOCK SQUARE-WAVE GLYPH
//   Draws a small square wave icon centered at (cx,cy) within a given width/height.
//   Used inside the clock gate body to make it immediately recognisable.
// ─────────────────────────────────────────────────────────────────────────────
static void pushSquareWave(std::vector<float>&v,
                            float cx,float cy,
                            float gW,float gH,   // total glyph width/height
                            float hw){
    // Draw 1.5 cycles of a square wave
    float x0=cx-gW*0.5f;
    float y_lo=cy+gH*0.35f;
    float y_hi=cy-gH*0.35f;
    float pw=gW/3.f;  // one pulse width

    // Segment sequence: _|‾|_|‾|_  (lo-rise-hi-fall-lo-rise-hi-fall-lo)
    //  x:  x0 → x0+pw/2 → x0+pw/2 → x0+pw → x0+3pw/2 → x0+3pw/2 → x0+2pw → x0+5pw/2 → x0+5pw/2 → x0+3pw
    float xs[10]={
        x0,
        x0+pw*0.5f, x0+pw*0.5f,
        x0+pw,
        x0+pw*1.5f, x0+pw*1.5f,
        x0+pw*2.f,
        x0+pw*2.5f, x0+pw*2.5f,
        x0+pw*3.f
    };
    float ys[10]={y_lo, y_lo, y_hi, y_hi, y_lo, y_lo, y_hi, y_hi, y_lo, y_lo};
    for(int i=0;i<9;++i)
        pushLine(v,xs[i],ys[i],xs[i+1],ys[i+1],hw);
}

// =============================================================================
// §3  SHADERS
// =============================================================================

static const char* kGVert=R"GLSL(
#version 330 core
layout(location=0) in vec2 aPos;
uniform vec2 uViewSize;
void main(){
    vec2 ndc=aPos/uViewSize*2.0-1.0;ndc.y=-ndc.y;
    gl_Position=vec4(ndc,0.0,1.0);
}
)GLSL";
static const char* kGFrag=R"GLSL(
#version 330 core
out vec4 fragColor;
uniform vec4 uColor;
void main(){fragColor=uColor;}
)GLSL";

// =============================================================================
// §4  GATE COLORS
// =============================================================================

struct GateColors{float body[4],border[4],label[4];};
static GateColors gateColors(GateType t,bool sel){
    //                          AND           OR            NOT           NAND          NOR           XOR           XNOR          INPUT         OUTPUT        CLOCK
    static const float bodies[kGTC][4]={
        {0.08f,0.15f,0.30f,1},{0.08f,0.25f,0.15f,1},{0.22f,0.10f,0.28f,1},
        {0.26f,0.08f,0.10f,1},{0.20f,0.08f,0.22f,1},{0.08f,0.20f,0.30f,1},
        {0.12f,0.12f,0.28f,1},{0.08f,0.22f,0.13f,1},{0.24f,0.14f,0.05f,1},
        {0.05f,0.15f,0.28f,1},  // CLOCK – dark teal-blue
    };
    static const float borders[kGTC][4]={
        {0.27f,0.47f,0.86f,1},{0.27f,0.75f,0.43f,1},{0.66f,0.27f,0.86f,1},
        {0.86f,0.27f,0.27f,1},{0.78f,0.27f,0.67f,1},{0.27f,0.67f,0.86f,1},
        {0.43f,0.43f,0.86f,1},{0.27f,0.82f,0.43f,1},{0.86f,0.51f,0.16f,1},
        {0.18f,0.82f,0.90f,1},  // CLOCK – bright cyan
    };
    int i=(int)t;GateColors c;
    memcpy(c.body,bodies[i],16);memcpy(c.label,borders[i],16);
    if(sel){c.border[0]=0.80f;c.border[1]=0.65f;c.border[2]=0.97f;c.border[3]=1.f;}
    else memcpy(c.border,borders[i],16);
    return c;
}

// =============================================================================
// §5  CIRCUIT SURFACE
// =============================================================================

// Timer IDs for per-clock-gate WM_TIMER messages
// We use CLOCK_TIMER_BASE + gateId as the timer ID so each clock is independent.
static constexpr UINT_PTR kClockTimerBase = 0x4C4B0000; // 'LK' prefix

class CircuitSurface : public RenderSurface {
public:
    Circuit circuit;
    SimMode simMode=SimMode::Edit;
    bool    snapEnabled=true;

    std::function<void()>            onCircuitChanged;
    std::function<void(const char*)> onStatusMessage;
    std::function<void()>            onRedrawNeeded;
    std::function<void(float)>       onZoomChanged;
    std::function<void(SimMode)>     onSimModeChanged;
    std::function<void()>            onSimUpdated;
    std::function<void(int,const std::string&)> onLabelEdit;
    std::function<void()>            onSaveRequest;
    std::function<void()>            onLoadRequest;

    void pushUndo(){
        if(undoPos_<(int)undoStack_.size())
            undoStack_.erase(undoStack_.begin()+undoPos_,undoStack_.end());
        undoStack_.push_back(circuit.toJson());
        if((int)undoStack_.size()>kMaxUndo) undoStack_.erase(undoStack_.begin());
        undoPos_=(int)undoStack_.size();
    }
    void undo(){
        if(undoPos_<=0)return;
        if(undoPos_==(int)undoStack_.size()) undoStack_.push_back(circuit.toJson());
        --undoPos_;circuit.fromJson(undoStack_[undoPos_]);
        afterEdit("Undo");
    }
    void redo(){
        if(undoPos_>=(int)undoStack_.size()-1)return;
        ++undoPos_;circuit.fromJson(undoStack_[undoPos_]);
        afterEdit("Redo");
    }

    // ── Clock timer management ────────────────────────────────────────────
    void startClockTimers(){
        if(!hwnd_)return;
        for(auto&g:circuit.gates){
            if(g.type==GateType::CLOCK){
                float hz=g.clockHz;if(hz<=0.f)hz=1.f;
                // Timer fires at half-period (toggle each half-period = full cycle)
                UINT ms=max(16u,(UINT)(500.f/hz));
                SetTimer(hwnd_,kClockTimerBase+(UINT_PTR)g.id,ms,nullptr);
            }
        }
    }
    void stopClockTimers(){
        if(!hwnd_)return;
        for(auto&g:circuit.gates)
            if(g.type==GateType::CLOCK)
                KillTimer(hwnd_,kClockTimerBase+(UINT_PTR)g.id);
    }
    void restartClockTimer(int gateId){
        Gate*g=circuit.find(gateId);if(!g||!hwnd_)return;
        KillTimer(hwnd_,kClockTimerBase+(UINT_PTR)gateId);
        if(simMode==SimMode::Running){
            float hz=g->clockHz;if(hz<=0.f)hz=1.f;
            UINT ms=max(16u,(UINT)(500.f/hz));
            SetTimer(hwnd_,kClockTimerBase+(UINT_PTR)gateId,ms,nullptr);
        }
    }

    // ── Cycle frequency preset on click ──────────────────────────────────
    void cycleClockFreq(int gateId){
        Gate*g=circuit.find(gateId);if(!g||g->type!=GateType::CLOCK)return;
        pushUndo();
        // Find current preset index and advance
        int best=0;float bdiff=1e9f;
        for(int i=0;i<kClockFreqCount;++i){
            float d=fabsf(g->clockHz-kClockFreqPresets[i]);
            if(d<bdiff){bdiff=d;best=i;}
        }
        int next=(best+1)%kClockFreqCount;
        g->clockHz=kClockFreqPresets[next];
        restartClockTimer(gateId);
        char buf[64];snprintf(buf,sizeof(buf),"Clock %.1f Hz",g->clockHz);
        if(onStatusMessage)onStatusMessage(buf);
        if(onCircuitChanged)onCircuitChanged();
        redraw();
    }

    void simPlay(){
        if(circuit.gates.empty())return;
        simMode=SimMode::Running;circuit.evaluate();notifyMode();
        startClockTimers();
        if(onSimUpdated)onSimUpdated();
        if(onStatusMessage)onStatusMessage("▶ Running  ·  Click INPUT/CLOCK to interact  ·  Space = pause");
        redraw();
    }
    void simPause(){
        if(simMode==SimMode::Edit)return;
        simMode=(simMode==SimMode::Running)?SimMode::Paused:SimMode::Running;
        if(simMode==SimMode::Running) startClockTimers();
        else                          stopClockTimers();
        notifyMode();
        if(onStatusMessage)onStatusMessage(simMode==SimMode::Paused
            ?"⏸ Paused  ·  Space or ▶ to resume"
            :"▶ Running  ·  Click INPUT/CLOCK to interact  ·  Space = pause");
        redraw();
    }
    void simStop(){
        stopClockTimers();
        simMode=SimMode::Edit;
        for(auto&w:circuit.wires)w.value=false;
        for(auto&g:circuit.gates){g.simVal=false;if(g.type==GateType::CLOCK)g.inputVal=false;}
        notifyMode();if(onSimUpdated)onSimUpdated();
        if(onStatusMessage)onStatusMessage("■ Stopped  ·  Edit circuit then press ▶ Play");
        redraw();
    }
    void simStep(){
        if(simMode==SimMode::Edit)simMode=SimMode::Paused;
        // Advance clocks one half-period manually
        for(auto&g:circuit.gates)
            if(g.type==GateType::CLOCK)g.inputVal=!g.inputVal;
        circuit.evaluate();notifyMode();if(onSimUpdated)onSimUpdated();
        if(onStatusMessage)onStatusMessage("⏭ Stepped");redraw();
    }
    bool isSimActive() const{return simMode!=SimMode::Edit;}
    bool isSimRunning()const{return simMode==SimMode::Running;}

    void applyLabel(int gateId,const std::string&lbl){
        Gate*g=circuit.find(gateId);if(!g)return;
        pushUndo();g->label=lbl;
        if(onCircuitChanged)onCircuitChanged();
        if(onSimUpdated)onSimUpdated();
        redraw();
    }

    bool saveToFile(const std::string&path){
        std::ofstream f(path);if(!f)return false;
        f<<circuit.toJson();return f.good();
    }
    bool loadFromFile(const std::string&path){
        std::ifstream f(path);if(!f)return false;
        std::string src((std::istreambuf_iterator<char>(f)),std::istreambuf_iterator<char>());
        if(!circuit.fromJson(src))return false;
        undoStack_.clear();undoPos_=0;pushUndo();
        afterEdit("Loaded");return true;
    }

    void resetZoom(){zoom_=1.f;camX_=0.f;camY_=0.f;redraw();}
    void fitToView(){
        if(circuit.gates.empty()){resetZoom();return;}
        float mnX=1e9f,mnY=1e9f,mxX=-1e9f,mxY=-1e9f;
        for(auto&g:circuit.gates){mnX=min(mnX,g.x-kGW);mxX=max(mxX,g.x+kGW);mnY=min(mnY,g.y-kGH);mxY=max(mxY,g.y+kGH);}
        float ww=mxX-mnX,wh=mxY-mnY;if(ww<1||wh<1){resetZoom();return;}
        float pad=60.f,zx=(viewW_-pad*2)/ww,zy=(viewH_-pad*2)/wh;
        zoom_=std::clamp(min(zx,zy),0.05f,4.f);
        camX_=(mnX+mxX)*.5f-viewW_*.5f/zoom_;camY_=(mnY+mxY)*.5f-viewH_*.5f/zoom_;
        redraw();
    }
    void setZoomLevel(float z){zoom_=std::clamp(z,0.05f,8.f);redraw();if(onZoomChanged)onZoomChanged(zoom_);}
    float getZoom()const{return zoom_;}

    void dropGate(GateType type){
        if(isSimActive())return;
        float wx=camX_+viewW_*.5f/zoom_,wy=camY_+viewH_*.5f/zoom_;
        static int n=0;int slot=(n++)%7;
        wx+=float(slot-3)*(kGW+24.f);wy+=float(slot%3-1)*(kGH+18.f);
        if(snapEnabled){wx=snapGrid(wx);wy=snapGrid(wy);}
        pushUndo();circuit.add(type,wx,wy);
        if(onCircuitChanged)onCircuitChanged();redraw();
    }

    void initialize(int w,int h)override{
        ls_gl::init();viewW_=w;viewH_=h;
        camX_=-w*.5f;camY_=-h*.5f;
        buildShader();buildVAO();
        HWND hwnd=WindowFromDC(wglGetCurrentDC());
        if(hwnd) hookWindow(hwnd);
    }
    void resize(int w,int h)override{viewW_=w;viewH_=h;}
    void update(double)override{}

    void render(const float*)override{
        glViewport(0,0,viewW_,viewH_);
        glClearColor(0.055f,0.055f,0.08f,1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
        GL.useProgram(prog_);
        ls_gl::uniform2f(uViewSize_,float(viewW_),float(viewH_));
        GL.bindVertexArray(vao_);
        drawGrid();drawAllWires();drawDragPreview();
        for(auto&g:circuit.gates)drawGate(g);
        drawPinHovers();
        if(isSimActive())drawSimBorder();
        GL.bindVertexArray(0);GL.useProgram(0);glDisable(GL_BLEND);
    }
    void destroy()override{
        stopClockTimers();
        if(prog_){GL.deleteProgram(prog_);prog_=0;}
        if(vao_) {GL.deleteVertexArrays(1,&vao_);vao_=0;}
        if(vbo_) {GL.deleteBuffers(1,&vbo_);vbo_=0;}
    }
    bool needsContinuousRedraw()const override{return false;}

    void onMouseDown(float sx,float sy)override{
        lastSX_=sx;lastSY_=sy;
        float wx,wy;s2w(sx,sy,wx,wy);
        if(isSimActive()){
            int hit=hitGate(wx,wy);
            if(hit>=0){Gate*g=circuit.find(hit);
                if(g&&g->type==GateType::INPUT){
                    g->inputVal=!g->inputVal;circuit.evaluate();
                    if(onSimUpdated)onSimUpdated();if(onCircuitChanged)onCircuitChanged();redraw();}
                // CLOCK: single-click cycles frequency in sim mode
                else if(g&&g->type==GateType::CLOCK){
                    cycleClockFreq(hit);
                }}
            return;
        }
        {auto pins=collectPins(circuit);PinRef src{};
         if(nearestPin(pins,wx,wy,16.f/zoom_,true,false,src)){
             dragWire_=true;dragSrc_=src;dragCurWX_=wx;dragCurWY_=wy;hasDstSnap_=false;
             for(auto&g:circuit.gates)g.selected=false;for(auto&w:circuit.wires)w.selected=false;
             if(onStatusMessage)onStatusMessage("Drag to an input pin  ·  Esc to cancel");
             redraw();return;}}
        for(auto&w:circuit.wires)w.selected=false;
        for(auto&w:circuit.wires){
            auto[sx0,sy0,sx1,sy1]=wireSC(w);
            if(bezHit(sx0,sy0,sx1,sy1,sx,sy,8.f)){
                w.selected=true;for(auto&g:circuit.gates)g.selected=false;
                if(onStatusMessage)onStatusMessage("Wire selected  ·  Del to remove  ·  Right-click to delete");
                redraw();return;}}
        for(auto&g:circuit.gates)g.selected=false;
        int hit=hitGate(wx,wy);
        if(hit>=0){Gate*g=circuit.find(hit);if(g){g->selected=true;dragId_=hit;dragOx_=wx-g->x;dragOy_=wy-g->y;}}
        else dragId_=-1;
        redraw();
    }

    void onMouseMove(float sx,float sy)override{
        float wx,wy;s2w(sx,sy,wx,wy);
        if(mmb_){camX_-=(sx-lastSX_)/zoom_;camY_-=(sy-lastSY_)/zoom_;lastSX_=sx;lastSY_=sy;redraw();return;}
        lastSX_=sx;lastSY_=sy;
        if(isSimActive())return;
        if(dragWire_){
            dragCurWX_=wx;dragCurWY_=wy;
            auto pins=collectPins(circuit);PinRef dst{};
            hasDstSnap_=nearestPin(pins,wx,wy,20.f/zoom_,false,true,dst);
            if(hasDstSnap_&&dst.gateId==dragSrc_.gateId)hasDstSnap_=false;
            if(hasDstSnap_)dstSnap_=dst;redraw();return;
        }
        {auto pins=collectPins(circuit);PinRef h{};
         bool found=nearestPin(pins,wx,wy,16.f/zoom_,false,false,h);
         bool changed=(found!=hovPinValid_);
         if(!changed&&found)changed=(h.gateId!=hovPin_.gateId||h.pinIdx!=hovPin_.pinIdx||h.isOutput!=hovPin_.isOutput);
         hovPinValid_=found;if(found)hovPin_=h;if(changed)redraw();}
        if(dragId_>=0){
            Gate*g=circuit.find(dragId_);
            if(g){float nx=wx-dragOx_,ny=wy-dragOy_;
                if(snapEnabled){nx=snapGrid(nx);ny=snapGrid(ny);}
                g->x=nx;g->y=ny;g->recomputePins();redraw();}
        }
    }

    void onMouseUp(float sx,float sy)override{
        mmb_=false;
        if(isSimActive())return;
        if(dragWire_){
            dragWire_=false;
            if(hasDstSnap_){
                bool occ=false;
                for(auto&w:circuit.wires)if(w.dst.gateId==dstSnap_.gateId&&w.dst.pinIdx==dstSnap_.pinIdx){occ=true;break;}
                if(!occ){
                    pushUndo();
                    circuit.connect(dragSrc_.gateId,dragSrc_.pinIdx,dstSnap_.gateId,dstSnap_.pinIdx);
                    circuit.evaluate();if(onCircuitChanged)onCircuitChanged();
                    if(onStatusMessage)onStatusMessage("Connected  ·  Press ▶ Play to simulate");
                }else if(onStatusMessage)onStatusMessage("Input already connected — remove existing wire first");
            }
            hasDstSnap_=false;redraw();return;
        }
        if(dragId_>=0){pushUndo();dragId_=-1;if(onCircuitChanged)onCircuitChanged();}
    }

    void onRightMouseDown(float sx,float sy)override{
        if(isSimActive())return;
        for(int i=int(circuit.wires.size())-1;i>=0;--i){
            auto[sx0,sy0,sx1,sy1]=wireSC(circuit.wires[i]);
            if(bezHit(sx0,sy0,sx1,sy1,sx,sy,10.f)){
                pushUndo();circuit.disconnect(circuit.wires[i].id);circuit.evaluate();
                if(onCircuitChanged)onCircuitChanged();
                if(onStatusMessage)onStatusMessage("Wire removed");redraw();return;}}
    }

    void onDoubleClick(float sx,float sy){
        float wx,wy;s2w(sx,sy,wx,wy);
        if(isSimActive()){
            int hit=hitGate(wx,wy);
            if(hit>=0){Gate*g=circuit.find(hit);
                if(g&&g->type==GateType::INPUT){g->inputVal=!g->inputVal;circuit.evaluate();if(onSimUpdated)onSimUpdated();redraw();}
                else if(g&&g->type==GateType::CLOCK){cycleClockFreq(hit);}
            }
            return;
        }
        int hit=hitGate(wx,wy);
        if(hit>=0){Gate*g=circuit.find(hit);
            // CLOCK: double-click in edit mode cycles frequency
            if(g&&g->type==GateType::CLOCK){cycleClockFreq(hit);return;}
            if(g&&onLabelEdit)onLabelEdit(hit,g->label);return;}
        for(int i=int(circuit.wires.size())-1;i>=0;--i){
            auto[sx0,sy0,sx1,sy1]=wireSC(circuit.wires[i]);
            if(bezHit(sx0,sy0,sx1,sy1,sx,sy,10.f)){
                pushUndo();circuit.disconnect(circuit.wires[i].id);circuit.evaluate();
                if(onCircuitChanged)onCircuitChanged();
                if(onStatusMessage)onStatusMessage("Wire removed");redraw();return;}}
    }

    void hookWindow(HWND hwnd){
        if(hwnd_) return;
        hwnd_=hwnd;
        sMap()[hwnd]=this;
        origProc_=(WNDPROC)SetWindowLongPtrW(hwnd,GWLP_WNDPROC,(LONG_PTR)WndProcHook);
    }

private:
    HWND    hwnd_=nullptr;
    WNDPROC origProc_=nullptr;

    static std::unordered_map<HWND,CircuitSurface*>& sMap(){
        static std::unordered_map<HWND,CircuitSurface*> m;
        return m;
    }

    static LRESULT CALLBACK WndProcHook(HWND h,UINT msg,WPARAM wp,LPARAM lp){
        auto it=sMap().find(h);
        if(it==sMap().end()) return DefWindowProcW(h,msg,wp,lp);
        CircuitSurface* s=it->second;
        WNDPROC orig=s->origProc_;
        switch(msg){
            case WM_MBUTTONDOWN:{
                SetCapture(h);s->mmb_=true;
                s->lastSX_=float(GET_X_LPARAM(lp));s->lastSY_=float(GET_Y_LPARAM(lp));
                return 0;
            }
            case WM_MBUTTONUP:{ReleaseCapture();s->mmb_=false;return 0;}
            case WM_MOUSEWHEEL:{
                POINT pt={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};ScreenToClient(h,&pt);
                float cx=float(pt.x),cy=float(pt.y);
                float dy=float((short)HIWORD(wp))/float(WHEEL_DELTA);
                float wx0,wy0;s->s2w(cx,cy,wx0,wy0);
                float f=(dy>0)?1.12f:1.f/1.12f;
                s->zoom_=std::clamp(s->zoom_*f,0.05f,8.f);
                s->camX_=wx0-cx/s->zoom_;s->camY_=wy0-cy/s->zoom_;
                if(s->onZoomChanged)s->onZoomChanged(s->zoom_);
                s->redraw();return 0;
            }
            // ── Clock timer tick ──────────────────────────────────────────
            case WM_TIMER:{
                UINT_PTR tid=(UINT_PTR)wp;
                if(tid>=kClockTimerBase && s->simMode==SimMode::Running){
                    int gateId=(int)(tid-kClockTimerBase);
                    Gate* g=s->circuit.find(gateId);
                    if(g&&g->type==GateType::CLOCK){
                        g->inputVal=!g->inputVal;
                        s->circuit.evaluate();
                        if(s->onSimUpdated)s->onSimUpdated();
                        if(s->onCircuitChanged)s->onCircuitChanged();
                        s->redraw();
                    }
                    return 0;
                }
                break;
            }
            case WM_NCDESTROY:{
                s->stopClockTimers();
                sMap().erase(h);
                SetWindowLongPtrW(h,GWLP_WNDPROC,(LONG_PTR)orig);
                return CallWindowProcW(orig,h,msg,wp,lp);
            }
        }
        return CallWindowProcW(orig,h,msg,wp,lp);
    }
public:

    void onKeyDown(int key)override{
        bool ctrl=(GetKeyState(VK_CONTROL)&0x8000)!=0;
        if(ctrl&&key=='Z'){undo();return;}
        if(ctrl&&key=='Y'){redo();return;}
        if(ctrl&&key=='S'){if(onSaveRequest)onSaveRequest();return;}
        if(ctrl&&key=='O'){if(onLoadRequest)onLoadRequest();return;}
        if(key=='G'&&!ctrl){
            snapEnabled=!snapEnabled;
            if(onStatusMessage)onStatusMessage(snapEnabled?"⊞ Snap to grid ON  (G to toggle)":"⊟ Snap to grid OFF  (G to toggle)");
            redraw();return;
        }
        if(key==VK_ESCAPE&&dragWire_){dragWire_=false;hasDstSnap_=false;redraw();return;}
        if(key==VK_SPACE){
            if(isSimActive()){simPause();return;}
            for(auto&g:circuit.gates)
                if(g.selected&&g.type==GateType::INPUT){
                    g.inputVal=!g.inputVal;circuit.evaluate();if(onCircuitChanged)onCircuitChanged();redraw();}
        }
        if(!isSimActive()&&(key==VK_DELETE||key==VK_BACK)){
            bool anyW=false;
            for(int i=int(circuit.wires.size())-1;i>=0;--i)
                if(circuit.wires[i].selected){circuit.disconnect(circuit.wires[i].id);anyW=true;}
            if(anyW){pushUndo();circuit.evaluate();if(onCircuitChanged)onCircuitChanged();redraw();return;}
            bool anyG=false;
            for(int i=int(circuit.gates.size())-1;i>=0;--i)
                if(circuit.gates[i].selected){
                    // stop its timer if it's a clock
                    if(circuit.gates[i].type==GateType::CLOCK&&hwnd_)
                        KillTimer(hwnd_,kClockTimerBase+(UINT_PTR)circuit.gates[i].id);
                    circuit.removeGateWires(circuit.gates[i].id);
                    circuit.remove(circuit.gates[i].id);anyG=true;}
            if(anyG){pushUndo();circuit.evaluate();if(onCircuitChanged)onCircuitChanged();redraw();}
        }
    }

private:
    static constexpr int kMaxUndo=50;
    std::vector<std::string> undoStack_;
    int undoPos_=0;

    int   viewW_=1,viewH_=1;
    float camX_=0,camY_=0,zoom_=1.f;
    GLuint prog_=0,vao_=0,vbo_=0;
    GLint  uViewSize_=-1,uColor_=-1;
    int   dragId_=-1;
    float dragOx_=0,dragOy_=0,lastSX_=0,lastSY_=0;
    bool  mmb_=false,dragWire_=false;
    PinRef dragSrc_{},dstSnap_{};
    float  dragCurWX_=0,dragCurWY_=0;
    bool   hasDstSnap_=false,hovPinValid_=false;
    PinRef hovPin_{};

    void redraw(){if(onRedrawNeeded)onRedrawNeeded();}
    void notifyMode(){if(onSimModeChanged)onSimModeChanged(simMode);}
    void afterEdit(const char*msg){
        if(onCircuitChanged)onCircuitChanged();if(onSimUpdated)onSimUpdated();
        if(onStatusMessage)onStatusMessage(msg);redraw();
    }
    void w2s(float wx,float wy,float&sx,float&sy)const{sx=(wx-camX_)*zoom_;sy=viewH_-(wy-camY_)*zoom_;}
    void s2w(float sx,float sy,float&wx,float&wy)const{wx=camX_+sx/zoom_;wy=camY_+sy/zoom_;}
    std::tuple<float,float,float,float> wireSC(const Wire&w)const{
        float sx0=0,sy0=0,sx1=0,sy1=0;
        if(auto*g=circuit.find(w.src.gateId))if(w.src.pinIdx<(int)g->outPins.size())w2s(g->outPins[w.src.pinIdx].cx,g->outPins[w.src.pinIdx].cy,sx0,sy0);
        if(auto*g=circuit.find(w.dst.gateId))if(w.dst.pinIdx<(int)g->inPins.size()) w2s(g->inPins[w.dst.pinIdx].cx, g->inPins[w.dst.pinIdx].cy, sx1,sy1);
        return{sx0,sy0,sx1,sy1};
    }
    int hitGate(float wx,float wy)const{
        for(int i=int(circuit.gates.size())-1;i>=0;--i){const Gate&g=circuit.gates[i];
            if(wx>=g.x-kGW*.5f&&wx<=g.x+kGW*.5f&&wy>=g.y-kGH*.5f&&wy<=g.y+kGH*.5f)return g.id;}
        return -1;
    }
    void buildShader(){prog_=glutil::linkProgram(kGVert,kGFrag);assert(prog_);
        uViewSize_=GL.getUniformLocation(prog_,"uViewSize");uColor_=GL.getUniformLocation(prog_,"uColor");}
    void buildVAO(){
        GL.genVertexArrays(1,&vao_);GL.genBuffers(1,&vbo_);
        GL.bindVertexArray(vao_);GL.bindBuffer(GL_ARRAY_BUFFER,vbo_);
        GL.enableVertexAttribArray(0);GL.vertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,2*sizeof(float),(void*)0);
        GL.bindVertexArray(0);
    }
    void upload(const std::vector<float>&v){
        GL.bindBuffer(GL_ARRAY_BUFFER,vbo_);
        GL.bufferData(GL_ARRAY_BUFFER,(GLsizeiptr)(v.size()*sizeof(float)),v.data(),GL_DYNAMIC_DRAW);
    }
    void dc(const std::vector<float>&v,float r,float g,float b,float a=1.f){
        if(v.empty())return;upload(v);ls_gl::uniform4f(uColor_,r,g,b,a);
        glDrawArrays(GL_TRIANGLES,0,(GLsizei)(v.size()/2));
    }

    // ── Grid ──────────────────────────────────────────────────────────────
    void drawGrid(){
        float gs=40.f,step=gs*zoom_;if(step<4.f)return;
        float vw=float(viewW_),vh=float(viewH_);
        std::vector<float>lines;lines.reserve(2048);
        for(float wx=std::floor(camX_/gs)*gs;;wx+=gs){float sx=(wx-camX_)*zoom_;if(sx>vw+step)break;pushLine(lines,sx,0,sx,vh,0.5f);}
        for(float wy=std::floor(camY_/gs)*gs;;wy+=gs){float sy=vh-(wy-camY_)*zoom_;if(sy<-step)break;pushLine(lines,0,sy,vw,sy,0.5f);}
        dc(lines,0.12f,0.13f,0.19f);
        if(snapEnabled&&zoom_>0.35f){
            float gsnap=kGrid,sdot=max(1.f,zoom_*1.1f);
            std::vector<float>dots;
            for(float wx=std::floor(camX_/gsnap)*gsnap;wx<camX_+viewW_/zoom_;wx+=gsnap){
                float sx=(wx-camX_)*zoom_;
                for(float wy=std::floor(camY_/gsnap)*gsnap;wy<camY_+viewH_/zoom_;wy+=gsnap){
                    float sy=vh-(wy-camY_)*zoom_;pushCircle(dots,sx,sy,sdot,6);
                }
            }
            dc(dots,0.22f,0.24f,0.38f,0.65f);
        }
        float ox,oy;w2s(0,0,ox,oy);
        std::vector<float>cross;pushLine(cross,ox-10,oy,ox+10,oy,1.f);pushLine(cross,ox,oy-10,ox,oy+10,1.f);
        dc(cross,0.22f,0.23f,0.36f);
    }

    // ── Wires ─────────────────────────────────────────────────────────────
    void drawAllWires(){
        bool sim=isSimActive();float hw=max(1.0f,zoom_*1.4f);
        for(auto&w:circuit.wires){
            auto[sx0,sy0,sx1,sy1]=wireSC(w);
            if(!sim&&w.selected){std::vector<float>halo;pushBez(halo,sx0,sy0,sx1,sy1,hw+4.f);dc(halo,0.9f,0.78f,0.2f,0.25f);}
            if(w.value){std::vector<float>v;pushBez(v,sx0,sy0,sx1,sy1,hw+0.5f);dc(v,0.28f,0.92f,0.48f);}
            else{std::vector<float>v;pushBez(v,sx0,sy0,sx1,sy1,hw);
                if(!sim&&w.selected)dc(v,0.88f,0.75f,0.18f);else dc(v,0.22f,0.25f,0.42f);}
            {std::vector<float>ep;float dr=max(2.8f,hw*1.4f);pushCircle(ep,sx0,sy0,dr);pushCircle(ep,sx1,sy1,dr);
             if(w.value)dc(ep,0.28f,0.92f,0.48f);else if(!sim&&w.selected)dc(ep,0.88f,0.75f,0.18f);else dc(ep,0.28f,0.32f,0.55f);}
        }
    }

    // ── Drag preview ──────────────────────────────────────────────────────
    void drawDragPreview(){
        if(!dragWire_)return;
        float sx0,sy0;
        {auto*g=circuit.find(dragSrc_.gateId);if(!g||dragSrc_.pinIdx>=(int)g->outPins.size())return;
         w2s(g->outPins[dragSrc_.pinIdx].cx,g->outPins[dragSrc_.pinIdx].cy,sx0,sy0);}
        float sx1,sy1;
        if(hasDstSnap_)w2s(dstSnap_.cx,dstSnap_.cy,sx1,sy1);
        else w2s(dragCurWX_,dragCurWY_,sx1,sy1);
        float hw=max(1.f,zoom_*1.3f);bool sn=hasDstSnap_;
        {std::vector<float>g;pushBez(g,sx0,sy0,sx1,sy1,hw+5.f);dc(g,sn?.25f:.50f,sn?.90f:.45f,sn?.45f:.95f,0.18f);}
        {std::vector<float>l;pushBez(l,sx0,sy0,sx1,sy1,hw);dc(l,sn?.28f:.52f,sn?1.f:.50f,sn?.52f:.98f,0.85f);}
        {std::vector<float>dot;pushCircle(dot,sx1,sy1,max(4.f,hw*2.f));dc(dot,sn?.28f:.52f,sn?1.f:.50f,sn?.52f:.98f);}
    }

    // ── Pin hovers ────────────────────────────────────────────────────────
    void drawPinHovers(){
        if(dragWire_&&hasDstSnap_){float sx,sy;w2s(dstSnap_.cx,dstSnap_.cy,sx,sy);
            std::vector<float>r;pushCircle(r,sx,sy,max(6.f,kPinR*zoom_*2.f),24);dc(r,0.28f,1.f,0.5f,0.45f);}
        if(!dragWire_&&hovPinValid_&&!isSimActive()){float sx,sy;w2s(hovPin_.cx,hovPin_.cy,sx,sy);
            std::vector<float>r;pushCircle(r,sx,sy,max(6.f,kPinR*zoom_*2.f),24);
            if(hovPin_.isOutput)dc(r,1.f,0.72f,0.28f,0.40f);else dc(r,0.4f,0.6f,1.f,0.40f);}
    }

    // ── Sim border ────────────────────────────────────────────────────────
    void drawSimBorder(){
        float vw=float(viewW_),vh=float(viewH_),bw=3.f;
        std::vector<float>b;
        pushLine(b,0,bw,vw,bw,bw);pushLine(b,0,vh-bw,vw,vh-bw,bw);
        pushLine(b,bw,0,bw,vh,bw);pushLine(b,vw-bw,0,vw-bw,vh,bw);
        if(simMode==SimMode::Running)dc(b,0.28f,0.86f,0.47f,0.55f);
        else                         dc(b,0.86f,0.78f,0.28f,0.55f);
    }

    // =========================================================================
    // ── Gate  (IEEE/ANSI symbols) ─────────────────────────────────────────────
    // =========================================================================
    void drawGate(const Gate& g){
        float sx,sy; w2s(g.x,g.y,sx,sy);
        float z=zoom_;
        float hw=kGW*z*0.5f, hh=kGH*z*0.5f;
        float L=sx-hw, R=sx+hw;
        float yT=sy-hh, yB=sy+hh;
        float lw=max(1.0f, z);
        float cr=kCorner*z;
        bool  sim=isSimActive();
        GateColors col=gateColors(g.type,g.selected);

        auto DC=[&](const std::vector<float>&v,float r,float gg,float b,float a=1.f){
            dc(v,r,gg,b,a);
        };
        auto DCb=[&](const std::vector<float>&v,float a=1.f){
            DC(v,col.border[0],col.border[1],col.border[2],a);
        };
        auto DCbd=[&](const std::vector<float>&v,float a=1.f){
            DC(v,col.body[0],col.body[1],col.body[2],a);
        };

        // ── INPUT ─────────────────────────────────────────────────────────
        if(g.type==GateType::INPUT){
            {std::vector<float>v;pushRRFill(v,L+3,yT+3,R+3,yB+3,cr);DC(v,0,0,0,0.45f);}
            {std::vector<float>v;pushRRFill(v,L,yT,R,yB,cr);DCbd(v);}
            {std::vector<float>v;pushRR(v,L,yT,R,yB,cr,lw*0.5f+0.5f);DCb(v);}
            if(g.selected){std::vector<float>v;pushRR(v,L+2,yT+2,R-2,yB-2,cr-2,lw*0.5f);DCb(v,0.3f);}
            bool on=g.inputVal;
            float cr2=max(5.f,8.f*z),bcx=R-cr2-2*z,bcy=yT+cr2+2*z;
            if(sim&&on){std::vector<float>ring;pushRR(ring,L-1,yT-1,R+1,yB+1,cr+1,max(1.5f,z));DC(ring,0.28f,0.86f,0.47f,0.55f);}
            if(on){std::vector<float>gv;pushCircle(gv,bcx,bcy,cr2*1.8f,20);DC(gv,0.28f,0.86f,0.47f,0.20f);}
            {std::vector<float>v;pushCircle(v,bcx,bcy,cr2);DC(v,on?.28f:.55f,on?.86f:.18f,on?.47f:.18f);}
            {float lw2=hw*.3f,lh=max(1.5f,z*1.5f);std::vector<float>v;pushQuad(v,sx-lw2,sy-lh*.5f,sx+lw2,sy+lh*.5f);DC(v,on?.28f:.55f,on?.86f:.18f,on?.47f:.18f,0.7f);}
            for(auto&pin:g.outPins){
                float psx,psy;w2s(pin.cx,pin.cy,psx,psy);
                {std::vector<float>v;pushLine(v,R,psy,psx,psy,max(0.8f,z*.7f));DC(v,0.38f,0.42f,0.60f);}
                {std::vector<float>v;pushCircle(v,psx,psy,max(2.5f,kPinR*z));DC(v,pin.connected?1.f:.86f,pin.connected?.86f:.51f,pin.connected?.47f:.16f);}
            }
            return;
        }

        // ── OUTPUT ────────────────────────────────────────────────────────
        if(g.type==GateType::OUTPUT){
            {std::vector<float>v;pushRRFill(v,L+3,yT+3,R+3,yB+3,cr);DC(v,0,0,0,0.45f);}
            {std::vector<float>v;pushRRFill(v,L,yT,R,yB,cr);DCbd(v);}
            {std::vector<float>v;pushRR(v,L,yT,R,yB,cr,lw*0.5f+0.5f);DCb(v);}
            if(g.selected){std::vector<float>v;pushRR(v,L+2,yT+2,R-2,yB-2,cr-2,lw*0.5f);DCb(v,0.3f);}
            bool live=false;for(auto&w:circuit.wires)if(w.dst.gateId==g.id){live=w.value;break;}
            float cr2=max(4.f,7.f*z),bcx=R-cr2-2*z,bcy=yT+cr2+2*z;
            if(live){
                std::vector<float>ring;pushRR(ring,L-1,yT-1,R+1,yB+1,cr+1,max(1.5f,z));DC(ring,0.28f,0.86f,0.47f,0.60f);
                std::vector<float>tint;pushRRFill(tint,L,yT,R,yB,cr);DC(tint,0.05f,0.32f,0.10f,0.22f);
                std::vector<float>gv;pushCircle(gv,bcx,bcy,cr2*2.2f,24);DC(gv,0.28f,0.86f,0.47f,0.22f);
            }
            {std::vector<float>v;pushCircle(v,bcx,bcy,cr2);DC(v,live?.28f:.55f,live?.86f:.22f,live?.47f:.10f,live?1.f:.5f);}
            {std::vector<float>v;pushCircle(v,bcx,bcy,cr2*.5f);DC(v,live?.5f:.4f,live?1.f:.28f,live?.65f:.12f,live?1.f:.35f);}
            for(auto&pin:g.inPins){
                float psx,psy;w2s(pin.cx,pin.cy,psx,psy);
                {std::vector<float>v;pushLine(v,L,psy,psx,psy,max(0.8f,z*.7f));DC(v,0.38f,0.42f,0.60f);}
                {std::vector<float>v;pushCircle(v,psx,psy,max(2.5f,kPinR*z));DC(v,pin.connected?1.f:0.f,pin.connected?.86f:.31f,pin.connected?.47f:.51f);}
            }
            return;
        }

        // ── CLOCK ─────────────────────────────────────────────────────────
        if(g.type==GateType::CLOCK){
            bool on=g.inputVal;

            // Shadow
            {std::vector<float>v;pushRRFill(v,L+3,yT+3,R+3,yB+3,cr);DC(v,0,0,0,0.45f);}
            // Body fill
            {std::vector<float>v;pushRRFill(v,L,yT,R,yB,cr);DCbd(v);}
            // Border
            {std::vector<float>v;pushRR(v,L,yT,R,yB,cr,lw*0.5f+0.5f);DCb(v);}
            // Selection inner ring
            if(g.selected){std::vector<float>v;pushRR(v,L+2,yT+2,R-2,yB-2,cr-2,lw*0.5f);DCb(v,0.3f);}

            // Running glow when HIGH
            if(sim&&on){
                std::vector<float>ring;pushRR(ring,L-1,yT-1,R+1,yB+1,cr+1,max(1.5f,z));
                DC(ring,0.18f,0.82f,0.90f,0.65f);
                // body tint
                std::vector<float>tint;pushRRFill(tint,L,yT,R,yB,cr);
                DC(tint,0.02f,0.22f,0.30f,0.22f);
            }

            // Square-wave glyph drawn in the body centre
            {
                float glyphW=(R-L)*0.68f;
                float glyphH=(yB-yT)*0.38f;
                float gcx=sx-(R-L)*0.05f; // slight left bias to leave room for freq badge
                float gcy=(yT+yB)*0.5f;
                float strokeW=max(1.2f,z*1.1f);
                std::vector<float>wv;
                pushSquareWave(wv,gcx,gcy,glyphW,glyphH,strokeW);
                DC(wv,
                   on?0.18f:0.30f,
                   on?0.90f:0.65f,
                   on?0.95f:0.80f,
                   on?1.f:0.65f);
            }

            // Frequency badge: small filled circle in top-right corner
            {
                float cr2=max(4.5f,6.5f*z);
                float bcx=R-cr2-2*z,bcy=yT+cr2+2*z;
                // outer glow when running
                if(sim){std::vector<float>gv;pushCircle(gv,bcx,bcy,cr2*1.9f,20);DC(gv,0.18f,0.82f,0.90f,on?0.22f:0.08f);}
                {std::vector<float>v;pushCircle(v,bcx,bcy,cr2);
                 DC(v,on?0.18f:0.25f,on?0.82f:0.45f,on?0.90f:0.55f);}
                // inner dot (pulse indicator)
                {std::vector<float>v;pushCircle(v,bcx,bcy,cr2*0.45f);
                 DC(v,0.7f,0.95f,1.f,on?1.f:0.4f);}
            }

            // Output pin stub
            for(auto&pin:g.outPins){
                float psx,psy;w2s(pin.cx,pin.cy,psx,psy);
                {std::vector<float>v;pushLine(v,R,psy,psx,psy,max(0.8f,z*.7f));DC(v,0.38f,0.42f,0.60f);}
                {std::vector<float>v;pushCircle(v,psx,psy,max(2.5f,kPinR*z));
                 DC(v,pin.connected?1.f:.86f,pin.connected?.86f:.51f,pin.connected?.47f:.16f);}
            }
            return;
        }

        // =====================================================================
        // IEEE / ANSI LOGIC GATES (AND/OR/NOT/NAND/NOR/XOR/XNOR)
        // =====================================================================

        bool isNeg = (g.type==GateType::NAND || g.type==GateType::NOR  ||
                      g.type==GateType::XNOR || g.type==GateType::NOT);
        bool isOR  = (g.type==GateType::OR  || g.type==GateType::NOR  ||
                      g.type==GateType::XOR || g.type==GateType::XNOR);
        bool isAND = (g.type==GateType::AND || g.type==GateType::NAND);
        bool isTRI = (g.type==GateType::NOT);

        float H      = yB - yT;
        float bubR   = H * 0.115f;
        float tipX   = isTRI ? (R - H*0.20f) : R;
        float bubCX  = isAND ? (L+(R-L)*0.44f + H*0.5f + bubR)
                      : isTRI ? (tipX + bubR)
                      : (R + bubR);
        float MY     = (yT+yB)*0.5f;

        // 1. Drop shadow
        {
            std::vector<float> sv;float s=3.f;
            if(isAND) pushANDFill(sv,L+s,yT+s,R+s,yB+s);
            else if(isOR) pushORFill(sv,L+s,yT+s,R+s,yB+s);
            else          pushTriFill(sv,L+s,yT+s,R+s,yB+s);
            DC(sv, 0,0,0, 0.38f);
        }
        // 2. Body fill
        {
            std::vector<float> fv;
            if(isAND) pushANDFill(fv,L,yT,R,yB);
            else if(isOR) pushORFill(fv,L,yT,R,yB);
            else          pushTriFill(fv,L,yT,R,yB);
            DCbd(fv);
        }
        // 3. Selection fill tint
        if(g.selected){
            std::vector<float> sv;
            if(isAND) pushANDFill(sv,L,yT,R,yB);
            else if(isOR) pushORFill(sv,L,yT,R,yB);
            else          pushTriFill(sv,L,yT,R,yB);
            DC(sv, 0.80f,0.65f,0.97f, 0.18f);
        }
        // 4. Sim HIGH glow
        if(sim && g.simVal){
            std::vector<float> gv;float e=lw*0.8f;
            if(isAND) pushANDOutline(gv,L-e,yT-e,R+e,yB+e,lw*1.8f);
            else if(isOR){pushOROutline(gv,L-e,yT-e,R+e,yB+e,lw*1.8f);
                          if(g.type==GateType::XOR||g.type==GateType::XNOR)
                              pushXORBackArc(gv,L-e,yT-e,R+e,yB+e,lw*1.8f);}
            else          pushTRIOutline(gv,L-e,yT-e,R+e,yB+e,lw*1.8f,isNeg);
            if(isNeg) pushRing(gv, bubCX, MY, bubR+e, lw*1.8f);
            DC(gv, 0.28f,0.86f,0.47f, 0.38f);
        }
        // 5. Body outline
        {
            std::vector<float> ov;
            if(isAND)     pushANDOutline(ov,L,yT,R,yB,lw);
            else if(isOR){ pushOROutline(ov,L,yT,R,yB,lw);
                if(g.type==GateType::XOR||g.type==GateType::XNOR)
                    pushXORBackArc(ov,L,yT,R,yB,lw);}
            else           pushTRIOutline(ov,L,yT,R,yB,lw,isNeg);
            DCb(ov);
        }
        // 6. Inversion bubble
        if(isNeg){
            {std::vector<float>v;pushCircle(v,bubCX,MY,bubR,18);DCbd(v);}
            {std::vector<float>v;pushRing(v,bubCX,MY,bubR,lw);DCb(v);}
        }
        // 7. Selection inner ring
        if(g.selected){
            std::vector<float> rv;float i2=lw*0.45f;
            if(isAND) pushANDOutline(rv,L+2,yT+2,R-2,yB-2,i2);
            else if(isOR){pushOROutline(rv,L+2,yT+2,R-2,yB-2,i2);
                          if(g.type==GateType::XOR||g.type==GateType::XNOR)
                              pushXORBackArc(rv,L+2,yT+2,R-2,yB-2,i2);}
            else pushTRIOutline(rv,L+2,yT+2,R-2,yB-2,i2,isNeg);
            DCb(rv,0.35f);
        }
        // 8. Input pins
        for(auto&pin:g.inPins){
            float psx,psy; w2s(pin.cx,pin.cy,psx,psy);
            float stubL = psx;
            float arcOff = (g.type==GateType::XOR||g.type==GateType::XNOR)
                           ? (R-L)*0.11f : 0.f;
            {std::vector<float>v;pushLine(v,stubL,psy, L+arcOff,psy, max(0.8f,z*.7f));
             DC(v,0.38f,0.42f,0.60f);}
            {std::vector<float>v;pushCircle(v,psx,psy,max(2.5f,kPinR*z));
             DC(v, pin.connected?1.f:0.f, pin.connected?.86f:.31f, pin.connected?.47f:.51f);}
        }
        // 9. Output pin
        for(auto&pin:g.outPins){
            float psx,psy; w2s(pin.cx,pin.cy,psx,psy);
            float stubStart = isNeg ? (bubCX + bubR) : R;
            {std::vector<float>v;pushLine(v,stubStart,psy, psx,psy, max(0.8f,z*.7f));
             DC(v,0.38f,0.42f,0.60f);}
            {std::vector<float>v;pushCircle(v,psx,psy,max(2.5f,kPinR*z));
             DC(v, pin.connected?1.f:.86f, pin.connected?.86f:.51f, pin.connected?.47f:.16f);}
        }
    }
};

// =============================================================================
// §6  WIN32 FILE + LABEL DIALOGS
// =============================================================================

static std::string dlgSave(HWND hwnd){
    char buf[MAX_PATH]="circuit.lsim";
    OPENFILENAMEA ofn={};ofn.lStructSize=sizeof(ofn);ofn.hwndOwner=hwnd;
    ofn.lpstrFilter="Logic Sim\0*.lsim\0JSON\0*.json\0All Files\0*.*\0\0";
    ofn.lpstrFile=buf;ofn.nMaxFile=MAX_PATH;ofn.lpstrDefExt="lsim";
    ofn.Flags=OFN_OVERWRITEPROMPT|OFN_PATHMUSTEXIST;
    return GetSaveFileNameA(&ofn)?std::string(buf):"";
}
static std::string dlgOpen(HWND hwnd){
    char buf[MAX_PATH]="";
    OPENFILENAMEA ofn={};ofn.lStructSize=sizeof(ofn);ofn.hwndOwner=hwnd;
    ofn.lpstrFilter="Logic Sim\0*.lsim\0JSON\0*.json\0All Files\0*.*\0\0";
    ofn.lpstrFile=buf;ofn.nMaxFile=MAX_PATH;
    ofn.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST;
    return GetOpenFileNameA(&ofn)?std::string(buf):"";
}

static std::string dlgLabel(HWND hwnd,const std::string&initial){
    struct Ctx{char buf[128];};
    static Ctx ctx;
    strncpy_s(ctx.buf,sizeof(ctx.buf),initial.c_str(),_TRUNCATE);

    struct P{static INT_PTR CALLBACK Proc(HWND h,UINT m,WPARAM w,LPARAM l){
        static Ctx*c=nullptr;
        if(m==WM_INITDIALOG){c=(Ctx*)l;HWND ed=GetDlgItem(h,100);
            SetWindowTextA(ed,c->buf);SendMessageA(ed,EM_SETSEL,0,-1);SetFocus(ed);return FALSE;}
        if(m==WM_COMMAND){
            if(LOWORD(w)==IDOK){GetDlgItemTextA(h,100,c->buf,sizeof(c->buf));EndDialog(h,IDOK);}
            else if(LOWORD(w)==IDCANCEL)EndDialog(h,IDCANCEL);}
        return FALSE;
    }};

    std::vector<WORD>d;
    auto p32=[&](DWORD v){d.push_back(v&0xFFFF);d.push_back(v>>16);};
    auto p16=[&](WORD  v){d.push_back(v);};
    auto pws=[&](const wchar_t*s){while(*s)d.push_back((WORD)*s++);d.push_back(0);};
    auto al4=[&](){while(d.size()%2)d.push_back(0);};
    p32(DS_SETFONT|DS_MODALFRAME|DS_CENTER|WS_POPUP|WS_CAPTION|WS_SYSMENU);
    p32(0);p16(3);p16(0);p16(0);p16(200);p16(64);
    p16(0);p16(0);pws(L"Gate Label");p16(9);pws(L"Segoe UI");
    auto ctrl=[&](DWORD style,short x,short y,short cx,short cy,WORD id,WORD cls,const wchar_t*txt){
        al4();p32(WS_CHILD|WS_VISIBLE|style);p32(0);
        p16(x);p16(y);p16(cx);p16(cy);p16(id);p16(0xFFFF);p16(cls);pws(txt);p16(0);
    };
    ctrl(WS_BORDER|ES_AUTOHSCROLL, 7, 7,186,14,100,0x0081,L"");
    ctrl(BS_DEFPUSHBUTTON,         60,44, 36,14,IDOK,    0x0080,L"OK");
    ctrl(0,                       102,44, 50,14,IDCANCEL,0x0080,L"Cancel");

    INT_PTR r=DialogBoxIndirectParamA(GetModuleHandleA(nullptr),(LPCDLGTEMPLATE)d.data(),hwnd,P::Proc,(LPARAM)&ctx);
    return(r==IDOK)?std::string(ctx.buf):"__cancelled__";
}

// =============================================================================
// §7  APP
// =============================================================================

class LogicSimApp : public Component {
    State<int>         gateCount;
    State<std::string> statusMsg;
    State<double>      zoomPct;
    State<bool>        simRunning;
    State<bool>        simActive;
    State<std::string> ttText;
    State<bool>        snapOn;
    State<std::string> currentFile;

    std::shared_ptr<CircuitSurface> surface_;
    CanvasWidget* canvas_=nullptr;

    static constexpr int kSW=190,kTH=80,kHH=26;

    void notifyZoom(float z){double p=double(z)*100.0;if(std::abs(p-zoomPct.get())>0.4)zoomPct.set(p);}
    void applyZoom(double p){if(!surface_)return;surface_->setZoomLevel(float(p/100.0));if(canvas_)canvas_->redraw();}

    void rebuildTruthTable(){
        if(!surface_){ttText.set("No circuit");return;}
        std::vector<int>inIds,outIds;
        auto table=surface_->circuit.buildTruthTable(inIds,outIds);
        if(table.empty()||inIds.empty()||outIds.empty()){
            ttText.set("Connect INPUT → gates → OUTPUT\nto see truth table");return;}
        std::ostringstream ss;
        for(int i=0;i<(int)inIds.size();i++){
            auto*g=surface_->circuit.find(inIds[i]);
            ss<<((g&&!g->label.empty())?g->label:("I"+std::to_string(i+1)))<<" ";}
        ss<<"| ";
        for(int i=0;i<(int)outIds.size();i++){
            auto*g=surface_->circuit.find(outIds[i]);
            ss<<((g&&!g->label.empty())?g->label:("O"+std::to_string(i+1)))<<" ";}
        ss<<"\n";
        int w=int(inIds.size())*3+2+int(outIds.size())*3;
        for(int i=0;i<w;i++)ss<<"-";ss<<"\n";
        for(auto&row:table){
            bool isCur=true;
            for(int i=0;i<(int)row.inputs.size();i++){
                auto*g=surface_->circuit.find(inIds[i]);if(!g||g->inputVal!=row.inputs[i]){isCur=false;break;}}
            ss<<(isCur?"►":" ");
            for(bool v:row.inputs) ss<<" "<<(v?"1":"0");
            ss<<" |";
            for(bool v:row.outputs)ss<<" "<<(v?"1":"0");
            ss<<"\n";
        }
        ttText.set(ss.str());
    }

    void doSave(){
        if(!surface_)return;
        std::string path=dlgSave(GetForegroundWindow());
        if(path.empty())return;
        if(surface_->saveToFile(path)){currentFile.set(path);statusMsg.set("Saved: "+path);}
        else statusMsg.set("Save failed.");
    }
    void doLoad(){
        if(!surface_)return;
        std::string path=dlgOpen(GetForegroundWindow());
        if(path.empty())return;
        surface_->simStop();
        if(surface_->loadFromFile(path)){
            currentFile.set(path);
            gateCount.set(int(surface_->circuit.gates.size()));
            rebuildTruthTable();surface_->fitToView();
            statusMsg.set("Loaded: "+path);
            if(canvas_)canvas_->redraw();
        }else statusMsg.set("Load failed.");
    }
    void doLabelEdit(int gateId,const std::string&current){
        std::string r=dlgLabel(GetForegroundWindow(),current);
        if(r=="__cancelled__")return;
        surface_->applyLabel(gateId,r);
        rebuildTruthTable();
    }

public:
    LogicSimApp()
        : gateCount(0,context),
          statusMsg("Place gates · Connect wires · Press ▶ Play",context),
          zoomPct(100.0,context),
          simRunning(false,context),
          simActive(false,context),
          ttText("No circuit yet",context),
          snapOn(true,context),
          currentFile("",context){}

    WidgetPtr build()override{
        int SW=GetSystemMetrics(SM_CXSCREEN),SH=GetSystemMetrics(SM_CYSCREEN);
        int vW=SW-kSW,vH=SH-kTH-kHH;

        auto cv=std::make_shared<CanvasWidget>()->setSize(vW,vH);
        cv->setCanvasSize(vW,vH);cv->setViewportEnabled(false);
        surface_=cv->setSurface<CircuitSurface>();
        canvas_=cv.get();

        surface_->onCircuitChanged=[this](){gateCount.set(int(surface_->circuit.gates.size()));};
        surface_->onStatusMessage=[this](const char*m){statusMsg.set(m);};
        surface_->onRedrawNeeded=[this](){if(canvas_)canvas_->redraw();};
        surface_->onZoomChanged=[this](float z){notifyZoom(z);};
        surface_->onSimModeChanged=[this](SimMode m){simRunning.set(m==SimMode::Running);simActive.set(m!=SimMode::Edit);};
        surface_->onSimUpdated=[this](){rebuildTruthTable();};
        surface_->onLabelEdit=[this](int id,const std::string&cur){doLabelEdit(id,cur);};
        surface_->onSaveRequest=[this](){doSave();};
        surface_->onLoadRequest=[this](){doLoad();};

        zoomPct.listen([this](double p){applyZoom(p);});
        snapOn.listen([this](bool v){if(surface_)surface_->snapEnabled=v;if(canvas_)canvas_->redraw();});

        COLORREF kBg=RGB(12,12,18),kCard=RGB(18,18,28),kBord=RGB(40,42,62);
        COLORREF kAccent=RGB(174,129,255),kGreen=RGB(148,226,213),kDim=RGB(80,84,110),kText=RGB(192,202,232);

        struct TI{GateType t;COLORREF ac;const char*sub;};
        const std::vector<TI>tiles={
            {GateType::AND,  RGB(70,120,220),"A & B"},
            {GateType::OR,   RGB(70,190,110),"A | B"},
            {GateType::NOT,  RGB(170,70,220),"! A"},
            {GateType::NAND, RGB(220,70,70), "!(A&B)"},
            {GateType::NOR,  RGB(200,70,170),"!(A|B)"},
            {GateType::XOR,  RGB(70,170,220),"A ^ B"},
            {GateType::XNOR, RGB(110,110,220),"!(A^B)"},
            {GateType::INPUT,RGB(70,210,110),"Toggle 0/1"},
            {GateType::OUTPUT,RGB(220,130,40),"LED output"},
            {GateType::CLOCK,RGB(47,210,229),"0.5–8 Hz auto"},  // ← NEW
        };
        auto makeTile=[&](const TI&ti)->WidgetPtr{
            GateType t=ti.t;COLORREF ac=ti.ac;
            return GestureDetector(
                Container(Row(
                    Container(nullptr)->setWidth(3)->setHeight(32)->setBackgroundColor(ac)->setBorderRadius(2),
                    SizedBox(7,0),
                    Column(
                        Text(kGateName[(int)t])->setFontSize(10)->setFontWeight(FontWeight::Bold)->setTextColor(ac),
                        SizedBox(0,1),Text(ti.sub)->setFontSize(8)->setTextColor(kDim)
                    )->setSpacing(0)
                )->setSpacing(0)->setCrossAxisAlignment(CrossAxisAlignment::Center))
                ->setWidth(kSW-16)->setHeight(38)->setBackgroundColor(kCard)
                ->setBorderRadius(6)->setBorderWidth(1)->setBorderColor(kBord)
                ->setHoverBackgroundColor(RGB(26,26,42))
            )->setOnTap([this,t](){
                if(!surface_)return;
                if(surface_->isSimActive()){statusMsg.set("Stop simulation first");return;}
                surface_->dropGate(t);
                std::string extra=(t==GateType::CLOCK)
                    ?"  ·  Dbl-click to change Hz  ·  Click in SIM to cycle Hz"
                    :"  ·  Drag out→in  ·  Dbl-click to label";
                statusMsg.set(std::string("Placed ")+kGateName[(int)t]+extra);
            });
        };
        auto tileCol=std::make_shared<ColumnWidget>();tileCol->setSpacing(4);
        for(auto&ti:tiles)tileCol->addChild(makeTile(ti));

        auto ttPanel=Container(
            Column(
                Text("TRUTH TABLE")->setFontSize(7)->setFontWeight(FontWeight::Bold)->setTextColor(kDim),
                SizedBox(0,5),
                Text(ttText,[](const std::string&s){return s;})->setFontSize(9)->setTextColor(RGB(120,200,140))
            )->setSpacing(0)
        )->setWidth(kSW-8)->setBackgroundColor(RGB(10,14,10))
         ->setBorderRadius(6)->setBorderWidth(1)->setBorderColor(RGB(30,60,35))->setPaddingAll(7,7,7,7);

        auto sidebar=Container(
            Column(Text("GATES")->setFontSize(7)->setFontWeight(FontWeight::Bold)->setTextColor(kDim),
                   SizedBox(0,6),tileCol,SizedBox(0,10),ttPanel)->setSpacing(0)
        )->setWidth(kSW)->setBackgroundColor(kBg)->setPaddingAll(8,8,8,8);

        auto mkBtn=[&](const std::string&lbl,COLORREF col,COLORREF bg,COLORREF bord,std::function<void()>fn)->WidgetPtr{
            return GestureDetector(
                Container(Text(lbl)->setFontSize(11)->setFontWeight(FontWeight::Bold)->setTextColor(col))
                ->setHeight(30)->setBorderRadius(6)->setBackgroundColor(bg)
                ->setBorderWidth(1)->setBorderColor(bord)->setPaddingAll(10,4,10,4)
                ->setHoverBackgroundColor(RGB(GetRValue(bg)+8,GetGValue(bg)+8,GetBValue(bg)+8))
            )->setOnTap(fn);
        };

        auto toolbar=Container(
            Column(
                Row(
                    Text("⚡ Logic Sim")->setFontSize(13)->setFontWeight(FontWeight::Bold)->setTextColor(kAccent),
                    SizedBox(8,0),
                    mkBtn("💾 Save",kGreen,RGB(10,20,18),RGB(30,80,60),[this](){doSave();}),
                    mkBtn("📂 Open",kGreen,RGB(10,20,18),RGB(30,80,60),[this](){doLoad();}),
                    SizedBox(4,0),
                    mkBtn("↩ Undo",kAccent,RGB(22,20,34),RGB(70,55,100),[this](){if(surface_)surface_->undo();}),
                    mkBtn("↪ Redo",kAccent,RGB(22,20,34),RGB(70,55,100),[this](){if(surface_)surface_->redo();}),
                    SizedBox(4,0),
                    GestureDetector(
                        Container(Text(snapOn,[](bool v){return v?"⊞ Snap":"⊟ Snap";})
                            ->setFontSize(10)->setFontWeight(FontWeight::Bold)->setTextColor(RGB(174,129,255)))
                        ->setHeight(28)->setBorderRadius(5)->setBackgroundColor(RGB(22,18,32))
                        ->setBorderWidth(1)->setBorderColor(RGB(70,55,100))->setPaddingAll(8,3,8,3)
                    )->setOnTap([this](){snapOn.set(!snapOn.get());}),
                    SizedBox(6,0),
                    Text("Zoom")->setFontSize(9)->setTextColor(kDim),
                    Slider(10.0,400.0,1.0)->setValue(zoomPct)->setTrackFillColor(kAccent)->setWidth(80),
                    Text(zoomPct,[](double v){char b[16];_snprintf_s(b,sizeof(b),_TRUNCATE,"%.0f%%",v);return std::string(b);})
                        ->setFontSize(10)->setTextColor(kText)->setMinWidth(34),
                    mkBtn("Fit",kAccent,RGB(22,20,34),RGB(70,55,100),[this](){if(surface_){surface_->fitToView();notifyZoom(surface_->getZoom());}}),
                    mkBtn("1:1",kAccent,RGB(22,20,34),RGB(70,55,100),[this](){if(surface_){surface_->resetZoom();notifyZoom(1.f);}}),
                    SizedBox(6,0),
                    mkBtn("🗑 Clear",RGB(243,139,168),RGB(34,14,20),RGB(120,40,60),[this](){
                        if(!surface_)return;surface_->pushUndo();surface_->simStop();
                        surface_->circuit.clearAll();gateCount.set(0);ttText.set("No circuit yet");
                        currentFile.set("");if(canvas_)canvas_->redraw();}),
                    SizedBox(6,0),
                    Text(gateCount,[](int n){return std::to_string(n)+(n==1?" gate":" gates");})->setFontSize(10)->setTextColor(kDim)
                )->setSpacing(5)->setCrossAxisAlignment(CrossAxisAlignment::Center),
                SizedBox(0,4),
                Row(
                    GestureDetector(
                        Container(Text(simRunning,[](bool r){return r?"⏸ Pause":"▶ Play";})
                            ->setFontSize(11)->setFontWeight(FontWeight::Bold)->setTextColor(RGB(80,220,120)))
                        ->setHeight(30)->setBorderRadius(6)->setBackgroundColor(RGB(14,34,18))
                        ->setBorderWidth(1)->setBorderColor(RGB(40,120,55))->setPaddingAll(10,4,10,4)
                        ->setHoverBackgroundColor(RGB(18,44,22))
                    )->setOnTap([this](){if(!surface_)return;if(!surface_->isSimActive())surface_->simPlay();else surface_->simPause();}),
                    SizedBox(5,0),
                    mkBtn("■ Stop",RGB(230,80,80),RGB(34,14,14),RGB(120,40,40),[this](){if(surface_)surface_->simStop();}),
                    SizedBox(5,0),
                    mkBtn("⏭ Step",RGB(100,180,255),RGB(14,22,38),RGB(40,80,150),[this](){if(surface_)surface_->simStep();}),
                    SizedBox(12,0),
                    Container(Text(simActive,[](bool a){return a?"● SIM MODE":"○ EDIT MODE";})
                        ->setFontSize(9)->setFontWeight(FontWeight::Bold)->setTextColor(RGB(220,200,60)))
                    ->setPaddingAll(7,3,7,3)->setBorderRadius(4)->setBackgroundColor(RGB(20,18,8))
                    ->setBorderWidth(1)->setBorderColor(RGB(70,62,15)),
                    SizedBox(12,0),
                    Text("SIM: click INPUT to toggle · click/dbl-click CLOCK to cycle Hz · Space=pause  |  EDIT: Dbl-click=label/Hz · G=snap · Ctrl+Z/Y · Ctrl+S/O  |  Scroll=zoom · MMB=pan")
                        ->setFontSize(9)->setTextColor(kDim)
                )->setSpacing(0)->setCrossAxisAlignment(CrossAxisAlignment::Center)
            )->setSpacing(0)
        )->setBackgroundColor(kBg)->setPaddingAll(10,5,10,5)->setHeight(kTH);

        auto hints=Container(
            Row(
                Text(statusMsg,[](const std::string&s){return s;})->setFontSize(10)->setTextColor(kGreen),
                SizedBox(14,0),
                Text(currentFile,[](const std::string&s){return s.empty()?"Unsaved":s;})->setFontSize(9)->setTextColor(RGB(55,60,85))
            )->setSpacing(0)
        )->setBackgroundColor(kBg)->setPaddingAll(10,3,10,3)->setHeight(kHH);

        return Scaffold(Row(sidebar,Column(toolbar,hints,cv)->setSpacing(0))->setSpacing(0));
    }
};

// =============================================================================
// §8  ENTRY POINT
// =============================================================================

int WINAPI WinMain(HINSTANCE hInst,HINSTANCE,LPSTR,int){
    FluxUI app(hInst);
    app.build([&](){return FluxApp("Logic Sim",BuildComponent<LogicSimApp>(),AppTheme::dark());});
    RECT wa;SystemParametersInfo(SPI_GETWORKAREA,0,&wa,0);
    app.createWindow("FluxUI – Logic Sim",wa.right-wa.left,wa.bottom-wa.top);
    return app.run();
}