// =============================================================================
// logic_sim.hpp  —  Logic Gate Simulator  v13  (+ Subcircuits)
// =============================================================================

#pragma once
#include "flux.hpp"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <commdlg.h>
#include <windows.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#pragma comment(lib, "comdlg32.lib")

// ---------------------------------------------------------------------------
// Extra GL procs
// ---------------------------------------------------------------------------
using PFNGLUNIFORM4FPROC_LS = void(APIENTRY *)(GLint, GLfloat, GLfloat, GLfloat,
                                               GLfloat);
using PFNGLUNIFORM2FPROC_LS = void(APIENTRY *)(GLint, GLfloat, GLfloat);
using PFNGLACTIVETEXTUREPROC_LS = void(APIENTRY *)(GLenum);
using PFNGLUNIFORM1IPROC_LS = void(APIENTRY *)(GLint, GLint);

#ifndef GL_TEXTURE0
#define GL_TEXTURE0 0x84C0
#endif

namespace ls_gl {
inline PFNGLUNIFORM4FPROC_LS uniform4f = nullptr;
inline PFNGLUNIFORM2FPROC_LS uniform2f = nullptr;
inline PFNGLACTIVETEXTUREPROC_LS activeTexture = nullptr;
inline PFNGLUNIFORM1IPROC_LS uniform1i = nullptr;
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
  activeTexture = (PFNGLACTIVETEXTUREPROC_LS)load("glActiveTexture");
  uniform1i = (PFNGLUNIFORM1IPROC_LS)load("glUniform1i");
  assert(uniform4f && uniform2f && activeTexture && uniform1i);
}
} // namespace ls_gl

// =============================================================================
// §0  MINIMAL JSON
// =============================================================================
namespace js {
static std::string esc(const std::string &s) {
  std::string r;
  r.reserve(s.size() + 2);
  r += '"';
  for (char c : s) {
    if (c == '"')
      r += "\\\"";
    else if (c == '\\')
      r += "\\\\";
    else if (c == '\n')
      r += "\\n";
    else
      r += c;
  }
  r += '"';
  return r;
}
static std::string kv(const std::string &k, const std::string &v) {
  return esc(k) + ":" + v;
}
static std::string kv(const std::string &k, int v) {
  return esc(k) + ":" + std::to_string(v);
}
static std::string kv(const std::string &k, float v) {
  char b[32];
  snprintf(b, 32, "%.4g", v);
  return esc(k) + ":" + b;
}
static std::string kv(const std::string &k, bool v) {
  return esc(k) + ":" + (v ? "true" : "false");
}
static std::string obj(std::initializer_list<std::string> fs) {
  std::string r = "{";
  bool f = true;
  for (auto &x : fs) {
    if (!f)
      r += ",";
    r += x;
    f = false;
  }
  return r + "}";
}
static std::string arr(const std::vector<std::string> &items) {
  std::string r = "[";
  bool f = true;
  for (auto &x : items) {
    if (!f)
      r += ",";
    r += x;
    f = false;
  }
  return r + "]";
}
static void skipWS(const std::string &s, size_t &p) {
  while (p < s.size() &&
         (s[p] == ' ' || s[p] == '\t' || s[p] == '\r' || s[p] == '\n'))
    ++p;
}
static std::string readStr(const std::string &s, size_t &p) {
  if (p >= s.size() || s[p] != '"')
    return {};
  ++p;
  std::string r;
  while (p < s.size() && s[p] != '"') {
    if (s[p] == '\\' && p + 1 < s.size()) {
      ++p;
      if (s[p] == 'n')
        r += '\n';
      else
        r += s[p];
    } else
      r += s[p];
    ++p;
  }
  if (p < s.size())
    ++p;
  return r;
}
static std::string grabVal(const std::string &s, size_t &p) {
  skipWS(s, p);
  if (p >= s.size())
    return {};
  char o = s[p];
  if (o == '{' || o == '[') {
    char c = (o == '{') ? '}' : ']';
    int d = 0;
    size_t st = p;
    while (p < s.size()) {
      if (s[p] == o)
        d++;
      else if (s[p] == c) {
        d--;
        if (d == 0) {
          ++p;
          break;
        }
      } else if (s[p] == '"') {
        ++p;
        while (p < s.size() && s[p] != '"') {
          if (s[p] == '\\')
            ++p;
          ++p;
        }
      }
      ++p;
    }
    return s.substr(st, p - st);
  }
  if (o == '"') {
    size_t st = p;
    readStr(s, p);
    return s.substr(st, p - st);
  }
  size_t st = p;
  while (p < s.size() && s[p] != ',' && s[p] != '}' && s[p] != ']')
    ++p;
  return s.substr(st, p - st);
}
using KVList = std::vector<std::pair<std::string, std::string>>;
static KVList parseObj(const std::string &s, size_t &p) {
  KVList out;
  skipWS(s, p);
  if (p >= s.size() || s[p] != '{')
    return out;
  ++p;
  while (p < s.size() && s[p] != '}') {
    skipWS(s, p);
    if (s[p] == '}')
      break;
    auto key = readStr(s, p);
    skipWS(s, p);
    if (p < s.size() && s[p] == ':')
      ++p;
    auto val = grabVal(s, p);
    out.push_back({key, val});
    skipWS(s, p);
    if (p < s.size() && s[p] == ',')
      ++p;
  }
  if (p < s.size() && s[p] == '}')
    ++p;
  return out;
}
static std::vector<std::string> parseArr(const std::string &s) {
  std::vector<std::string> out;
  size_t p = 0;
  skipWS(s, p);
  if (p >= s.size() || s[p] != '[')
    return out;
  ++p;
  while (p < s.size() && s[p] != ']') {
    skipWS(s, p);
    if (s[p] == ']')
      break;
    out.push_back(grabVal(s, p));
    skipWS(s, p);
    if (p < s.size() && s[p] == ',')
      ++p;
  }
  return out;
}
static int asInt(const std::string &v) {
  try {
    return std::stoi(v);
  } catch (...) {
    return 0;
  }
}
static float asFloat(const std::string &v) {
  try {
    return std::stof(v);
  } catch (...) {
    return 0.f;
  }
}
static bool asBool(const std::string &v) { return v == "true" || v == "1"; }
static std::string asStr(const std::string &v) {
  if (v.size() >= 2 && v.front() == '"' && v.back() == '"') {
    size_t p = 0;
    return readStr(v, p);
  }
  return v;
}
} // namespace js

// =============================================================================
// §1  DATA MODEL
// =============================================================================

// ---------------------------------------------------------------------------
// Bus widths (1 = normal single-bit wire, stays unchanged)
// ---------------------------------------------------------------------------
enum class BusWidth : uint8_t { B1 = 1, B2 = 2, B4 = 4, B8 = 8 };
static int busWidthBits(BusWidth w) { return (int)w; }
static const char *busWidthLabel(BusWidth w) {
  switch (w) {
  case BusWidth::B2:
    return "2b";
  case BusWidth::B4:
    return "4b";
  case BusWidth::B8:
    return "8b";
  default:
    return "";
  }
}

enum class GateType {
  AND = 0,
  OR,
  NOT,
  NAND,
  NOR,
  XOR,
  XNOR,
  INPUT,
  OUTPUT,
  CLOCK,
  DFF,
  DLATCH,
  TFLIPFLOP,
  JKFLIPFLOP,
  BUS_IN_2,  // packs 2 × 1-bit inputs  → 1 × 2-bit bus output
  BUS_IN_4,  // packs 4 × 1-bit inputs  → 1 × 4-bit bus output
  BUS_IN_8,  // packs 8 × 1-bit inputs  → 1 × 8-bit bus output
  BUS_OUT_2, // unpacks 1 × 2-bit bus   → 2 × 1-bit outputs
  BUS_OUT_4, // unpacks 1 × 4-bit bus   → 4 × 1-bit outputs
  BUS_OUT_8, // unpacks 1 × 8-bit bus   → 8 × 1-bit outputs
  COUNT
};
static constexpr int kGTC = (int)GateType::COUNT;
static constexpr int kInPins[kGTC] = {
    2, 2, 1, 2, 2, 2, 2, 0, 1,
    0, 3, 2, 2, 3, 2, 4, 8, // BUS_IN_2/4/8  (N single-bit inputs)
    1, 1, 1                 // BUS_OUT_2/4/8 (1 bus input)
};
static constexpr int kOutPins[kGTC] = {
    1, 1, 1, 1, 1, 1, 1, 1, 0,
    1, 2, 2, 2, 2, 1, 1, 1, // BUS_IN_2/4/8  (1 bus output)
    2, 4, 8                 // BUS_OUT_2/4/8 (N single-bit outputs)
};
static const char *kGateName[kGTC] = {
    "AND",   "OR",     "NOT",   "NAND",  "NOR",    "XOR",  "XNOR",
    "INPUT", "OUTPUT", "CLOCK", "DFF",   "DLATCH", "TFF",  "JKFF",
    "BUS▲2", "BUS▲4",  "BUS▲8", "BUS▼2", "BUS▼4",  "BUS▼8"};
static const char *kInPinLabel[kGTC][3] = {
    {"A", "B", ""},
    {"A", "B", ""},
    {"A", "", ""},
    {"A", "B", ""},
    {"A", "B", ""},
    {"A", "B", ""},
    {"A", "B", ""},
    {"", "", ""},
    {"D", "", ""},
    {"", "", ""},
    {"D", "CLK", "RST"},
    {"D", "EN", ""},
    {"T", "CLK", ""},
    {"J", "K", "CLK"},
    // BUS_IN  — up to 3 shown (gate drawing only shows first 3 labels)
    {"b0", "b1", ""},
    {"b0", "b1", ""},
    {"b0", "b1", ""},
    // BUS_OUT
    {"BUS", "", ""},
    {"BUS", "", ""},
    {"BUS", "", ""},
};
static const char *kOutPinLabel[kGTC][2] = {
    {"Q", ""},
    {"Q", ""},
    {"Q", ""},
    {"Q", ""},
    {"Q", ""},
    {"Q", ""},
    {"Q", ""},
    {"Q", ""},
    {"", ""},
    {"Q", ""},
    {"Q", "/Q"},
    {"Q", "/Q"},
    {"Q", "/Q"},
    {"Q", "/Q"},
    // BUS_IN
    {"BUS", ""},
    {"BUS", ""},
    {"BUS", ""},
    // BUS_OUT
    {"b0", "b1"},
    {"b0", "b1"},
    {"b0", "b1"},
};
static bool isSequential(GateType t) {
  return t == GateType::DFF || t == GateType::DLATCH ||
         t == GateType::TFLIPFLOP || t == GateType::JKFLIPFLOP;
}
static bool isBusIn(GateType t) {
  return t == GateType::BUS_IN_2 || t == GateType::BUS_IN_4 ||
         t == GateType::BUS_IN_8;
}
static bool isBusOut(GateType t) {
  return t == GateType::BUS_OUT_2 || t == GateType::BUS_OUT_4 ||
         t == GateType::BUS_OUT_8;
}
static bool isBusGate(GateType t) { return isBusIn(t) || isBusOut(t); }
static BusWidth busGateWidth(GateType t) {
  if (t == GateType::BUS_IN_2 || t == GateType::BUS_OUT_2)
    return BusWidth::B2;
  if (t == GateType::BUS_IN_4 || t == GateType::BUS_OUT_4)
    return BusWidth::B4;
  if (t == GateType::BUS_IN_8 || t == GateType::BUS_OUT_8)
    return BusWidth::B8;
  return BusWidth::B1;
}
static constexpr float kClockFreqPresets[] = {0.5f, 1.f, 2.f, 4.f, 8.f};
static constexpr int kClockFreqCount = 5;
static constexpr float kGW = 88.f, kGH = 60.f, kPinR = 5.f, kPinLen = 16.f,
                       kCorner = 8.f;
static constexpr float kGH_SEQ = 84.f;
static constexpr float kGrid = 20.f;
static float snapGrid(float v) { return std::round(v / kGrid) * kGrid; }
static float gateH(GateType t) { return isSequential(t) ? kGH_SEQ : kGH; }

struct Pin {
  float cx = 0, cy = 0;
  bool connected = false;
};

struct Gate {
  int id = 0;
  GateType type = GateType::AND;
  float x = 0, y = 0;
  bool selected = false;
  bool inputVal = false, simVal = false, stateVal = false, prevClk = false;
  std::string label;
  float clockHz = 1.f;
  std::vector<Pin> inPins, outPins;

  void recomputePins() {
    int ni = kInPins[(int)type], no = kOutPins[(int)type];
    inPins.resize(ni);
    outPins.resize(no);
    float gh = gateH(type);
    float L = x - kGW * .5f, R = x + kGW * .5f, B = y - gh * .5f;
    for (int i = 0; i < ni; i++) {
      float t = (ni == 1) ? .5f : float(i) / (ni - 1);
      inPins[i] = {L - kPinLen, B + t * gh};
    }
    for (int i = 0; i < no; i++) {
      float t = (no == 1) ? .5f : float(i) / (no - 1);
      outPins[i] = {R + kPinLen, B + t * gh};
    }
  }
  std::string toJson() const {
    return js::obj({js::kv("id", id), js::kv("type", (int)type), js::kv("x", x),
                    js::kv("y", y), js::kv("inputVal", inputVal),
                    js::kv("label", label), js::kv("clockHz", clockHz),
                    js::kv("stateVal", stateVal)});
  }
  void fromKV(const js::KVList &m) {
    for (auto &[k, v] : m) {
      if (k == "id")
        id = js::asInt(v);
      else if (k == "type")
        type = (GateType)js::asInt(v);
      else if (k == "x")
        x = js::asFloat(v);
      else if (k == "y")
        y = js::asFloat(v);
      else if (k == "inputVal")
        inputVal = js::asBool(v);
      else if (k == "label")
        label = js::asStr(v);
      else if (k == "clockHz")
        clockHz = js::asFloat(v);
      else if (k == "stateVal")
        stateVal = js::asBool(v);
    }
    if (clockHz <= 0.f)
      clockHz = 1.f;
    recomputePins();
  }
};

struct WireEndpoint {
  int gateId = -1;
  int pinIdx = 0;
  bool isOutput = false;
};

// Wire — gateId < 0 means subcircuit instance (id = -gateId)
struct Wire {
  int id = 0;
  WireEndpoint src, dst;
  // Single-bit wires: value is used, busValue is empty
  // Bus wires: value unused, busValue holds each bit
  bool value = false, selected = false;
  float splitX = 1e30f;
  BusWidth width = BusWidth::B1;
  std::vector<bool> busValue; // size == busWidthBits(width) when width != B1

  bool isBus() const { return width != BusWidth::B1; }

  bool srcIsSubInst() const { return src.gateId < 0; }
  bool dstIsSubInst() const { return dst.gateId < 0; }
  int srcInstId() const { return -src.gateId; }
  int dstInstId() const { return -dst.gateId; }

  // Returns bit i (0-indexed). Safe to call on 1-bit wires (returns value).
  bool bit(int i) const {
    if (!isBus())
      return value;
    if (i < (int)busValue.size())
      return busValue[i];
    return false;
  }
  void setBit(int i, bool v) {
    if (!isBus()) {
      value = v;
      return;
    }
    if (i < (int)busValue.size())
      busValue[i] = v;
  }
  void resizeBus() {
    if (isBus())
      busValue.assign(busWidthBits(width), false);
  }

  std::string toJson() const {
    std::string bvStr = "[]";
    if (isBus()) {
      std::vector<std::string> bits;
      for (bool b : busValue)
        bits.push_back(b ? "1" : "0");
      bvStr = js::arr(bits);
    }
    return js::obj({js::kv("id", id), js::kv("srcId", src.gateId),
                    js::kv("srcPin", src.pinIdx), js::kv("dstId", dst.gateId),
                    js::kv("dstPin", dst.pinIdx), js::kv("splitX", splitX),
                    js::kv("busWidth", (int)width), "\"busValue\":" + bvStr});
  }
  void fromKV(const js::KVList &m) {
    splitX = 1e30f;
    width = BusWidth::B1;
    for (auto &[k, v] : m) {
      if (k == "id")
        id = js::asInt(v);
      else if (k == "srcId")
        src.gateId = js::asInt(v);
      else if (k == "srcPin")
        src.pinIdx = js::asInt(v);
      else if (k == "dstId")
        dst.gateId = js::asInt(v);
      else if (k == "dstPin")
        dst.pinIdx = js::asInt(v);
      else if (k == "splitX")
        splitX = js::asFloat(v);
      else if (k == "busWidth")
        width = (BusWidth)js::asInt(v);
      else if (k == "busValue") {
        busValue.clear();
        for (auto &bv : js::parseArr(v))
          busValue.push_back(js::asInt(bv) != 0);
      }
    }
    src.isOutput = true;
    dst.isOutput = false;
    resizeBus();
  }
};

struct PinRef {
  int gateId;
  int pinIdx;
  bool isOutput;
  float cx, cy;
  bool isSubInst = false;
};
struct TruthRow {
  std::vector<bool> inputs, outputs;
};
enum class SimMode { Edit, Running, Paused };

// =============================================================================
// §1b  SUBCIRCUIT TYPES
// =============================================================================

static constexpr float kSCMinH = kGH_SEQ;
static constexpr float kSCPinSpacing = 20.f;
static constexpr float kSCWidth = 100.f;

static float subcircuitH(int nIn, int nOut) {
  int m = max(max(nIn, nOut), 1);
  return max(float(m) * kSCPinSpacing + kSCPinSpacing, kSCMinH);
}

struct SubcircuitPinInfo {
  std::string label;
  int internalGateId = 0;
  bool isOutput = false;
  float cx = 0, cy = 0;
  bool connected = false;
  bool value = false; // last evaluated value (for display + wire drive)
};

// Forward-declare Circuit so SubcircuitDef can hold one
struct Circuit;

struct SubcircuitDef {
  int id = 0;
  std::string name = "Module";
  Circuit *inner = nullptr; // heap-allocated to avoid incomplete-type issues
  std::vector<SubcircuitPinInfo> inputPins;
  std::vector<SubcircuitPinInfo> outputPins;

  SubcircuitDef(); // body after Circuit definition
  SubcircuitDef(const SubcircuitDef &o);
  SubcircuitDef &operator=(const SubcircuitDef &o);
  ~SubcircuitDef(); // body after Circuit definition

  void rebuildPins();
  std::string toJson() const;
  void fromJson(const std::string &src);
};

struct SubcircuitInst {
  int id = 0, defId = 0;
  float x = 0, y = 0;
  bool selected = false;
  std::string label;

  std::vector<SubcircuitPinInfo> inputPins;
  std::vector<SubcircuitPinInfo> outputPins;

  std::unordered_map<int, bool> internalState;
  std::unordered_map<int, bool> internalPrevClk;

  void recomputePins(const SubcircuitDef &def);

  std::string toJson() const {
    std::vector<std::string> sa, ca;
    for (auto &[k, v] : internalState)
      sa.push_back(js::obj({js::kv("g", k), js::kv("v", v)}));
    for (auto &[k, v] : internalPrevClk)
      ca.push_back(js::obj({js::kv("g", k), js::kv("v", v)}));
    return js::obj({js::kv("id", id), js::kv("defId", defId), js::kv("x", x),
                    js::kv("y", y), js::kv("label", label),
                    "\"istate\":" + js::arr(sa), "\"iclk\":" + js::arr(ca)});
  }
  void fromKV(const js::KVList &m) {
    for (auto &[k, v] : m) {
      if (k == "id")
        id = js::asInt(v);
      else if (k == "defId")
        defId = js::asInt(v);
      else if (k == "x")
        x = js::asFloat(v);
      else if (k == "y")
        y = js::asFloat(v);
      else if (k == "label")
        label = js::asStr(v);
      else if (k == "istate") {
        for (auto &item : js::parseArr(v)) {
          size_t p = 0;
          int gid = 0;
          bool val = false;
          for (auto &[kk, vv] : js::parseObj(item, p)) {
            if (kk == "g")
              gid = js::asInt(vv);
            else if (kk == "v")
              val = js::asBool(vv);
          }
          internalState[gid] = val;
        }
      } else if (k == "iclk") {
        for (auto &item : js::parseArr(v)) {
          size_t p = 0;
          int gid = 0;
          bool val = false;
          for (auto &[kk, vv] : js::parseObj(item, p)) {
            if (kk == "g")
              gid = js::asInt(vv);
            else if (kk == "v")
              val = js::asBool(vv);
          }
          internalPrevClk[gid] = val;
        }
      }
    }
  }
};

// =============================================================================
// §2  CIRCUIT
// =============================================================================

struct Circuit {
  std::vector<Gate> gates;
  std::vector<Wire> wires;
  int nextId = 1, nextWireId = 1;

  // ── Subcircuit registry ──────────────────────────────────────────────
  std::unordered_map<int, SubcircuitDef> subcircuitDefs;
  std::vector<SubcircuitInst> subcircuitInsts;
  int nextSubDefId = 1, nextSubInstId = 1;

  // ── Gate helpers ─────────────────────────────────────────────────────
  int add(GateType t, float x, float y, const std::string &lbl = "") {
    Gate g;
    g.id = nextId++;
    g.type = t;
    g.x = x;
    g.y = y;
    g.label = lbl;
    if (t == GateType::CLOCK)
      g.clockHz = 1.f;
    g.recomputePins();
    gates.push_back(g);
    return g.id;
  }
  Gate *find(int id) {
    for (auto &g : gates)
      if (g.id == id)
        return &g;
    return nullptr;
  }
  const Gate *find(int id) const {
    for (auto &g : gates)
      if (g.id == id)
        return &g;
    return nullptr;
  }
  Wire *find_wire(int id) {
    for (auto &w : wires)
      if (w.id == id)
        return &w;
    return nullptr;
  }

  SubcircuitInst *findInst(int id) {
    for (auto &i : subcircuitInsts)
      if (i.id == id)
        return &i;
    return nullptr;
  }
  const SubcircuitInst *findInst(int id) const {
    for (auto &i : subcircuitInsts)
      if (i.id == id)
        return &i;
    return nullptr;
  }

  void remove(int id) {
    gates.erase(std::remove_if(gates.begin(), gates.end(),
                               [id](const Gate &g) { return g.id == id; }),
                gates.end());
  }
  void removeInst(int instId) {
    subcircuitInsts.erase(std::remove_if(subcircuitInsts.begin(),
                                         subcircuitInsts.end(),
                                         [instId](const SubcircuitInst &i) {
                                           return i.id == instId;
                                         }),
                          subcircuitInsts.end());
  }

  // Returns -1 and sets errOut if there is a width mismatch.
  int connect(int sId, int sPin, int dId, int dPin,
              std::string *errOut = nullptr) {
    // Determine widths of both endpoints
    BusWidth sw = BusWidth::B1, dw = BusWidth::B1;
    // Source gate
    if (sId >= 0) {
      Gate *sg = find(sId);
      if (sg && isBusIn(sg->type))
        sw = busGateWidth(sg->type); // bus output side
    } else {
      SubcircuitInst *si = findInst(-sId);
      (void)si; // subcircuit bus pins: extend later
    }
    // Destination gate
    if (dId >= 0) {
      Gate *dg = find(dId);
      if (dg && isBusOut(dg->type))
        dw = busGateWidth(dg->type); // bus input side
    } else {
      SubcircuitInst *di = findInst(-dId);
      (void)di;
    }
    if (sw != dw) {
      if (errOut)
        *errOut = std::string("Width mismatch: source is ") +
                  (sw == BusWidth::B1 ? "1-bit" : busWidthLabel(sw)) +
                  ", destination is " +
                  (dw == BusWidth::B1 ? "1-bit" : busWidthLabel(dw));
      return -1;
    }
    Wire w;
    w.id = nextWireId++;
    w.src = {sId, sPin, true};
    w.dst = {dId, dPin, false};
    w.width = sw;
    w.resizeBus();
    wires.push_back(w);
    markPinConn(sId, sPin, true, true);
    markPinConn(dId, dPin, false, true);
    return w.id;
  }
  void disconnect(int wireId) {
    for (int i = int(wires.size()) - 1; i >= 0; --i) {
      if (wires[i].id != wireId)
        continue;
      auto &w = wires[i];
      recomputePin(w.src.gateId, w.src.pinIdx, true, wireId);
      recomputePin(w.dst.gateId, w.dst.pinIdx, false, wireId);
      wires.erase(wires.begin() + i);
      return;
    }
  }
  void removeGateWires(int gateId) {
    std::vector<int> ids;
    for (auto &w : wires)
      if (w.src.gateId == gateId || w.dst.gateId == gateId)
        ids.push_back(w.id);
    for (int id : ids)
      disconnect(id);
  }
  void removeInstWires(int instId) {
    int neg = -instId;
    std::vector<int> ids;
    for (auto &w : wires)
      if (w.src.gateId == neg || w.dst.gateId == neg)
        ids.push_back(w.id);
    for (int id : ids)
      disconnect(id);
  }
  void clearAll() {
    gates.clear();
    wires.clear();
    subcircuitDefs.clear();
    subcircuitInsts.clear();
    nextId = 1;
    nextWireId = 1;
    nextSubDefId = 1;
    nextSubInstId = 1;
  }

  void rebuildConnected() {
    for (auto &g : gates) {
      for (auto &p : g.inPins)
        p.connected = false;
      for (auto &p : g.outPins)
        p.connected = false;
    }
    for (auto &inst : subcircuitInsts) {
      for (auto &p : inst.inputPins)
        p.connected = false;
      for (auto &p : inst.outputPins)
        p.connected = false;
    }
    for (auto &w : wires) {
      markPinConn(w.src.gateId, w.src.pinIdx, true, true);
      markPinConn(w.dst.gateId, w.dst.pinIdx, false, true);
    }
  }

  // ── Evaluation ───────────────────────────────────────────────────────
  void evaluate() {
    // Phase 1: combinational gates + subcircuit output init
    for (int idx : topoSort()) {
      Gate &g = gates[idx];
      if (!isSequential(g.type)) {
        g.simVal = computeGate(g);
        for (auto &w : wires)
          if (w.src.gateId == g.id && w.src.pinIdx == 0)
            w.value = g.simVal;
      } else {
        for (auto &w : wires)
          if (w.src.gateId == g.id)
            w.value = (w.src.pinIdx == 0) ? g.stateVal : !g.stateVal;
      }
    }

    // ── Bus gate evaluation ──────────────────────────────────────────
    for (auto &g : gates) {
      if (isBusIn(g.type)) {
        int bits = busWidthBits(busGateWidth(g.type));
        // Find the single bus output wire and pack bits into it
        for (auto &w : wires) {
          if (w.src.gateId == g.id && w.src.pinIdx == 0) {
            w.resizeBus();
            for (int i = 0; i < bits; i++) {
              bool b = false;
              for (auto &wIn : wires)
                if (wIn.dst.gateId == g.id && wIn.dst.pinIdx == i) {
                  b = wIn.value;
                  break;
                }
              w.setBit(i, b);
            }
          }
        }
      }
      if (isBusOut(g.type)) {
        int bits = busWidthBits(busGateWidth(g.type));
        // Read the single bus input wire and unpack bits to output wires
        bool busInput[8] = {};
        for (auto &w : wires)
          if (w.dst.gateId == g.id && w.dst.pinIdx == 0)
            for (int i = 0; i < bits; i++)
              busInput[i] = w.bit(i);
        for (auto &w : wires)
          if (w.src.gateId == g.id && w.src.pinIdx < bits)
            w.value = busInput[w.src.pinIdx];
      }
    }
    // Evaluate subcircuit instances
    for (auto &inst : subcircuitInsts)
      evaluateSubcircuitInst(inst);
    pushSubcircuitWires();

    // Phase 2: sequential update
    for (auto &g : gates) {
      if (!isSequential(g.type))
        continue;
      g.stateVal = computeSequential(g);
      g.simVal = g.stateVal;
      bool clk = getInput(g, g.type == GateType::JKFLIPFLOP ? 2 : 1);
      g.prevClk = clk;
    }

    // Phase 3: re-propagate
    for (auto &g : gates) {
      if (!isSequential(g.type))
        continue;
      for (auto &w : wires)
        if (w.src.gateId == g.id)
          w.value = (w.src.pinIdx == 0) ? g.stateVal : !g.stateVal;
    }
    for (int idx : topoSort()) {
      Gate &g = gates[idx];
      if (isSequential(g.type))
        continue;
      g.simVal = computeGate(g);
      for (auto &w : wires)
        if (w.src.gateId == g.id && w.src.pinIdx == 0)
          w.value = g.simVal;
    }
    for (auto &inst : subcircuitInsts)
      evaluateSubcircuitInst(inst);
    pushSubcircuitWires();
  }

  void resetSequentialState() {
    for (auto &g : gates)
      if (isSequential(g.type)) {
        g.stateVal = false;
        g.simVal = false;
        g.prevClk = false;
      }
    for (auto &inst : subcircuitInsts) {
      inst.internalState.clear();
      inst.internalPrevClk.clear();
    }
  }

  // ── Grouping: turn selected gates into a subcircuit instance ─────────
  // Returns the new inst id, or -1 on failure.
  int groupSelected(const std::string &defName) {
    // Collect selected gate ids
    std::vector<int> selIds;
    for (auto &g : gates)
      if (g.selected)
        selIds.push_back(g.id);
    if (selIds.empty())
      return -1;

    std::unordered_set<int> selSet(selIds.begin(), selIds.end());

    // Classify wires: internal (both ends inside), crossing-in (src outside),
    // crossing-out (dst outside), external (both outside — ignore)
    struct CrossWire {
      int wireId;
      bool srcOutside;
      int pinIdx;
    }; // pinIdx = boundary pin slot
    std::vector<CrossWire> crossWires;

    // Centroid of selection for placing the new instance
    float cx = 0, cy = 0;
    for (int id : selIds) {
      const Gate *g = find(id);
      if (g) {
        cx += g->x;
        cy += g->y;
      }
    }
    cx /= float(selIds.size());
    cy /= float(selIds.size());

    // Build the def
    SubcircuitDef def;
    def.id = nextSubDefId++;
    def.name = defName;

    // Copy selected gates into def inner circuit
    // Remap ids to fresh ids inside inner (avoid collision with outer ids)
    std::unordered_map<int, int> idRemap; // outer id -> inner id
    for (int oid : selIds) {
      const Gate *og = find(oid);
      if (!og)
        continue;
      int nid = def.inner->nextId++;
      idRemap[oid] = nid;
      Gate ng = *og;
      ng.id = nid;
      ng.x -= cx;
      ng.y -= cy; // centre the inner circuit
      ng.selected = false;
      ng.recomputePins();
      def.inner->gates.push_back(ng);
    }

    // Copy internal wires (both endpoints inside selection)
    for (auto &w : wires) {
      bool srcIn = selSet.count(w.src.gateId) > 0;
      bool dstIn = selSet.count(w.dst.gateId) > 0;
      if (srcIn && dstIn) {
        Wire nw = w;
        nw.id = def.inner->nextWireId++;
        nw.src.gateId = idRemap[w.src.gateId];
        nw.dst.gateId = idRemap[w.dst.gateId];
        nw.splitX = 1e30f;
        def.inner->wires.push_back(nw);
      }
    }

    // For crossing wires, create INPUT/OUTPUT gates inside def
    // and record which outer wire maps to which inner boundary gate
    struct BoundaryMap {
      int outerWireId;
      int innerBoundaryGateId;
      bool inputSide;
    };
    std::vector<BoundaryMap> boundaryMaps;

    // First pass: wires whose src is OUTSIDE and dst is INSIDE
    //   → create an INPUT gate inside def; the outer wire will connect to
    //     the new inst's input pin
    int pinAutoIdx = 0;
    for (auto &w : wires) {
      bool srcIn = selSet.count(w.src.gateId) > 0;
      bool dstIn = selSet.count(w.dst.gateId) > 0;
      if (!srcIn && dstIn) {
        // Create INPUT gate inside def
        int igid = def.inner->nextId++;
        Gate ig;
        ig.id = igid;
        ig.type = GateType::INPUT;
        // Position it to the left of the inner centroid
        ig.x = -kGW * 1.8f - float(pinAutoIdx) * 0.f;
        ig.y = float(pinAutoIdx) * (kGH + 10.f) -
               float(selIds.size()) * (kGH + 10.f) * 0.5f;
        ig.label = "in" + std::to_string(pinAutoIdx);
        ig.recomputePins();
        def.inner->gates.push_back(ig);

        // Wire from that INPUT gate to the original dst gate (remapped)
        Wire bw;
        bw.id = def.inner->nextWireId++;
        bw.src = {igid, 0, true};
        bw.dst = {idRemap.count(w.dst.gateId) ? idRemap[w.dst.gateId]
                                              : w.dst.gateId,
                  w.dst.pinIdx, false};
        bw.splitX = 1e30f;
        def.inner->wires.push_back(bw);

        boundaryMaps.push_back({w.id, igid, true});
        pinAutoIdx++;
      }
    }

    // Second pass: wires whose src is INSIDE and dst is OUTSIDE
    //   → create an OUTPUT gate inside def
    int outAutoIdx = 0;
    for (auto &w : wires) {
      bool srcIn = selSet.count(w.src.gateId) > 0;
      bool dstIn = selSet.count(w.dst.gateId) > 0;
      if (srcIn && !dstIn) {
        int ogid = def.inner->nextId++;
        Gate og;
        og.id = ogid;
        og.type = GateType::OUTPUT;
        og.x = kGW * 1.8f;
        og.y = float(outAutoIdx) * (kGH + 10.f) -
               float(selIds.size()) * (kGH + 10.f) * 0.5f;
        og.label = "out" + std::to_string(outAutoIdx);
        og.recomputePins();
        def.inner->gates.push_back(og);

        Wire bw;
        bw.id = def.inner->nextWireId++;
        bw.src = {idRemap.count(w.src.gateId) ? idRemap[w.src.gateId]
                                              : w.src.gateId,
                  w.src.pinIdx, true};
        bw.dst = {ogid, 0, false};
        bw.splitX = 1e30f;
        def.inner->wires.push_back(bw);

        boundaryMaps.push_back({w.id, ogid, false});
        outAutoIdx++;
      }
    }

    def.rebuildPins();
    // Capture id before the move (move leaves def.id valid but we want
    // certainty)
    int defId = def.id;
    subcircuitDefs.emplace(defId, std::move(def));
    SubcircuitDef *pDef = &subcircuitDefs.at(defId);
    if (!pDef)
      return -1;

    // Create the instance
    SubcircuitInst inst;
    inst.id = nextSubInstId++;
    inst.defId = defId;
    inst.x = cx;
    inst.y = cy;
    inst.recomputePins(*pDef);
    subcircuitInsts.push_back(inst);
    SubcircuitInst &newInst = subcircuitInsts.back();
    int instId = newInst.id;

    // Re-route crossing wires to the new instance
    // Build lookup: innerBoundaryGateId -> pin index in inputPins / outputPins
    auto pinIndexForInner = [&](int innerGateId, bool isInputSide) -> int {
      auto &pins = isInputSide ? pDef->inputPins : pDef->outputPins;
      for (int i = 0; i < (int)pins.size(); i++)
        if (pins[i].internalGateId == innerGateId)
          return i;
      return 0;
    };

    for (auto &bm : boundaryMaps) {
      Wire *ow = find_wire(bm.outerWireId);
      if (!ow)
        continue;
      if (bm.inputSide) {
        // Outer wire was: someGate(out) → selGate(in)
        // Reroute dst to the new instance input pin
        int pi = pinIndexForInner(bm.innerBoundaryGateId, true);
        ow->dst.gateId = -instId;
        ow->dst.pinIdx = pi;
        ow->splitX = 1e30f;
      } else {
        // Outer wire was: selGate(out) → someGate(in)
        // Reroute src to the new instance output pin
        int pi = pinIndexForInner(bm.innerBoundaryGateId, false);
        ow->src.gateId = -instId;
        ow->src.pinIdx = pi;
        ow->splitX = 1e30f;
      }
    }

    // Remove selected gates and their now-internal wires from parent
    // (internal wires were not crossing, so they're not in boundaryMaps)
    std::vector<int> toDisconnect;
    for (auto &w : wires) {
      bool srcIn = selSet.count(w.src.gateId) > 0;
      bool dstIn = selSet.count(w.dst.gateId) > 0;
      if (srcIn || dstIn)
        toDisconnect.push_back(w.id);
    }
    // But keep the re-routed boundary wires
    std::unordered_set<int> boundaryWireIds;
    for (auto &bm : boundaryMaps)
      boundaryWireIds.insert(bm.outerWireId);
    for (int wid : toDisconnect) {
      if (boundaryWireIds.count(wid))
        continue;
      // Remove directly (don't call disconnect which re-marks pins)
      wires.erase(std::remove_if(wires.begin(), wires.end(),
                                 [wid](const Wire &w) { return w.id == wid; }),
                  wires.end());
    }
    for (int gid : selIds)
      remove(gid);

    rebuildConnected();
    return instId;
  }

  // ── Truth table (combinational only, no subcircuits in scope) ────────
  std::vector<TruthRow> buildTruthTable(std::vector<int> &inIds,
                                        std::vector<int> &outIds) const {
    inIds.clear();
    outIds.clear();
    std::vector<int> inIdx, outIdx;
    for (int i = 0; i < (int)gates.size(); i++) {
      if (gates[i].type == GateType::INPUT ||
          gates[i].type == GateType::CLOCK) {
        inIdx.push_back(i);
        inIds.push_back(gates[i].id);
      }
      if (gates[i].type == GateType::OUTPUT) {
        outIdx.push_back(i);
        outIds.push_back(gates[i].id);
      }
    }
    if (inIdx.empty() || outIdx.empty())
      return {};
    for (auto &g : gates)
      if (isSequential(g.type))
        return {};
    if (!subcircuitInsts.empty())
      return {}; // skip if subcircuits present

    int ni = (int)inIdx.size(), combos = 1 << min(ni, 6);
    std::vector<Gate> gs = gates;
    std::vector<Wire> ws = wires;
    auto topo = [&]() -> std::vector<int> {
      int sz = (int)gs.size();
      std::vector<int> ind(sz, 0);
      for (auto &w : ws)
        for (int i = 0; i < sz; i++)
          if (gs[i].id == w.dst.gateId) {
            ind[i]++;
            break;
          }
      std::vector<int> q, r;
      for (int i = 0; i < sz; i++)
        if (ind[i] == 0)
          q.push_back(i);
      while (!q.empty()) {
        int c = q.back();
        q.pop_back();
        r.push_back(c);
        for (auto &w : ws) {
          if (w.src.gateId != gs[c].id)
            continue;
          for (int i = 0; i < sz; i++)
            if (gs[i].id == w.dst.gateId && --ind[i] == 0) {
              q.push_back(i);
              break;
            }
        }
      }
      if ((int)r.size() < sz) {
        std::vector<bool> vis(sz, false);
        for (int i : r)
          vis[i] = true;
        for (int i = 0; i < sz; i++)
          if (!vis[i])
            r.push_back(i);
      }
      return r;
    };
    auto eval = [&]() {
      for (int idx : topo()) {
        Gate &g = gs[idx];
        if (g.type == GateType::INPUT || g.type == GateType::CLOCK) {
          g.simVal = g.inputVal;
        } else {
          bool ins[2] = {};
          for (auto &w : ws)
            if (w.dst.gateId == g.id && w.dst.pinIdx < 2)
              ins[w.dst.pinIdx] = w.value;
          bool a = ins[0], b = ins[1];
          switch (g.type) {
          case GateType::AND:
            g.simVal = a && b;
            break;
          case GateType::OR:
            g.simVal = a || b;
            break;
          case GateType::NOT:
            g.simVal = !a;
            break;
          case GateType::NAND:
            g.simVal = !(a && b);
            break;
          case GateType::NOR:
            g.simVal = !(a || b);
            break;
          case GateType::XOR:
            g.simVal = a ^ b;
            break;
          case GateType::XNOR:
            g.simVal = !(a ^ b);
            break;
          case GateType::BUS_IN_2:
          case GateType::BUS_IN_4:
          case GateType::BUS_IN_8:
            // Packing happens in evaluate(), not here. Return false as
            // placeholder.
            return false;
          case GateType::BUS_OUT_2:
          case GateType::BUS_OUT_4:
          case GateType::BUS_OUT_8:
            return false;
          default:
            g.simVal = false;
            break;
          }
        }
        for (auto &w : ws)
          if (w.src.gateId == g.id)
            w.value = g.simVal;
      }
    };
    std::vector<TruthRow> table;
    for (int mask = 0; mask < combos; mask++) {
      for (int b = 0; b < ni; b++)
        gs[inIdx[b]].inputVal = (mask >> b) & 1;
      eval();
      TruthRow row;
      for (int i : inIdx)
        row.inputs.push_back(gs[i].inputVal);
      for (int i : outIdx)
        row.outputs.push_back(gs[i].simVal);
      table.push_back(row);
    }
    return table;
  }

  // ── Serialisation ────────────────────────────────────────────────────
  std::string toJson() const {
    std::vector<std::string> ga, wa, da, ia;
    for (auto &g : gates)
      ga.push_back(g.toJson());
    for (auto &w : wires)
      wa.push_back(w.toJson());
    for (auto &[k, v] : subcircuitDefs)
      da.push_back(v.toJson());
    for (auto &inst : subcircuitInsts)
      ia.push_back(inst.toJson());
    return js::obj(
        {js::kv("version", 3), js::kv("nextId", nextId),
         js::kv("nextWireId", nextWireId), js::kv("nextSubDefId", nextSubDefId),
         js::kv("nextSubInstId", nextSubInstId), "\"gates\":" + js::arr(ga),
         "\"wires\":" + js::arr(wa), "\"subcircuitDefs\":" + js::arr(da),
         "\"subcircuitInsts\":" + js::arr(ia)});
  }
  bool fromJson(const std::string &src) {
    clearAll();
    size_t p = 0;
    for (auto &[k, v] : js::parseObj(src, p)) {
      if (k == "nextId")
        nextId = js::asInt(v);
      else if (k == "nextWireId")
        nextWireId = js::asInt(v);
      else if (k == "nextSubDefId")
        nextSubDefId = js::asInt(v);
      else if (k == "nextSubInstId")
        nextSubInstId = js::asInt(v);
      else if (k == "gates") {
        for (auto &gj : js::parseArr(v)) {
          size_t gp = 0;
          Gate g;
          g.fromKV(js::parseObj(gj, gp));
          gates.push_back(g);
        }
      } else if (k == "wires") {
        for (auto &wj : js::parseArr(v)) {
          size_t wp = 0;
          Wire w;
          w.fromKV(js::parseObj(wj, wp));
          wires.push_back(w);
        }
      } else if (k == "subcircuitDefs") {
        for (auto &dj : js::parseArr(v)) {
          SubcircuitDef d;
          d.fromJson(dj);
          int did = d.id;
          subcircuitDefs.emplace(did, std::move(d));
        }
      } else if (k == "subcircuitInsts") {
        for (auto &ij : js::parseArr(v)) {
          size_t ip = 0;
          SubcircuitInst inst;
          inst.fromKV(js::parseObj(ij, ip));
          auto it = subcircuitDefs.find(inst.defId);
          if (it != subcircuitDefs.end())
            inst.recomputePins(it->second);
          subcircuitInsts.push_back(inst);
        }
      }
    }
    rebuildConnected();
    return !gates.empty() || !subcircuitInsts.empty();
  }

private:
  // ── Subcircuit instance evaluation ───────────────────────────────────
  void evaluateSubcircuitInst(SubcircuitInst &inst) {
    auto it = subcircuitDefs.find(inst.defId);
    if (it == subcircuitDefs.end())
      return;
    const SubcircuitDef &def = it->second;

    // Deep-copy inner circuit for this evaluation pass
    Circuit inner;
    inner.gates = def.inner->gates;
    inner.wires = def.inner->wires;
    inner.nextId = def.inner->nextId;
    inner.nextWireId = def.inner->nextWireId;

    // Restore sequential state
    for (auto &g : inner.gates) {
      if (isSequential(g.type)) {
        auto si = inst.internalState.find(g.id);
        if (si != inst.internalState.end()) {
          g.stateVal = si->second;
          g.simVal = si->second;
        }
        auto ci = inst.internalPrevClk.find(g.id);
        if (ci != inst.internalPrevClk.end())
          g.prevClk = ci->second;
      }
    }

    // Stamp input values from parent wires onto inner INPUT gates
    for (int pi = 0; pi < (int)inst.inputPins.size(); pi++) {
      bool val = false;
      int neg = -inst.id;
      for (auto &w : wires)
        if (w.dst.gateId == neg && w.dst.pinIdx == pi) {
          val = w.value;
          break;
        }
      int igid = inst.inputPins[pi].internalGateId;
      for (auto &g : inner.gates)
        if (g.id == igid) {
          g.inputVal = val;
          g.simVal = val;
          break;
        }
    }

    // Run inner evaluation (non-recursive: inner has no sub-insts)
    inner.evaluate();

    // Read output gate input values back
    for (int pi = 0; pi < (int)inst.outputPins.size(); pi++) {
      int ogid = inst.outputPins[pi].internalGateId;
      bool val = false;
      for (auto &w : inner.wires)
        if (w.dst.gateId == ogid) {
          val = w.value;
          break;
        }
      inst.outputPins[pi].value = val;
    }

    // Persist sequential state
    for (auto &g : inner.gates) {
      if (isSequential(g.type)) {
        inst.internalState[g.id] = g.stateVal;
        inst.internalPrevClk[g.id] = g.prevClk;
      }
    }
  }

  // Push subcircuit output pin values onto outgoing parent wires
  void pushSubcircuitWires() {
    for (auto &inst : subcircuitInsts) {
      int neg = -inst.id;
      for (auto &w : wires) {
        if (w.src.gateId != neg)
          continue;
        int pi = w.src.pinIdx;
        if (pi < (int)inst.outputPins.size())
          w.value = inst.outputPins[pi].value;
      }
    }
  }

  bool getInput(const Gate &g, int pinIdx) const {
    for (auto &w : wires)
      if (w.dst.gateId == g.id && w.dst.pinIdx == pinIdx)
        return w.value;
    return false;
  }
  bool computeGate(const Gate &g) const {
    if (g.type == GateType::INPUT || g.type == GateType::CLOCK)
      return g.inputVal;
    bool a = getInput(g, 0), b = getInput(g, 1);
    switch (g.type) {
    case GateType::AND:
      return a && b;
    case GateType::OR:
      return a || b;
    case GateType::NOT:
      return !a;
    case GateType::NAND:
      return !(a && b);
    case GateType::NOR:
      return !(a || b);
    case GateType::XOR:
      return a ^ b;
    case GateType::XNOR:
      return !(a ^ b);
    default:
      return false;
    }
  }
  bool computeSequential(const Gate &g) const {
    switch (g.type) {
    case GateType::DFF: {
      bool d = getInput(g, 0), clk = getInput(g, 1), rst = getInput(g, 2);
      if (!(clk && !g.prevClk))
        return g.stateVal;
      return rst ? false : d;
    }
    case GateType::DLATCH: {
      bool d = getInput(g, 0), en = getInput(g, 1);
      return en ? d : g.stateVal;
    }
    case GateType::TFLIPFLOP: {
      bool t = getInput(g, 0), clk = getInput(g, 1);
      if (!(clk && !g.prevClk))
        return g.stateVal;
      return t ? !g.stateVal : g.stateVal;
    }
    case GateType::JKFLIPFLOP: {
      bool j = getInput(g, 0), k = getInput(g, 1), clk = getInput(g, 2);
      if (!(clk && !g.prevClk))
        return g.stateVal;
      if (j && !k)
        return true;
      if (!j && k)
        return false;
      if (j && k)
        return !g.stateVal;
      return g.stateVal;
    }
    default:
      return g.stateVal;
    }
  }
  std::vector<int> topoSort() const {
    int sz = (int)gates.size();
    std::vector<int> ind(sz, 0);
    for (auto &w : wires)
      for (int i = 0; i < sz; i++)
        if (gates[i].id == w.dst.gateId) {
          ind[i]++;
          break;
        }
    std::vector<int> q, r;
    for (int i = 0; i < sz; i++)
      if (ind[i] == 0)
        q.push_back(i);
    while (!q.empty()) {
      int c = q.back();
      q.pop_back();
      r.push_back(c);
      for (auto &w : wires) {
        if (w.src.gateId != gates[c].id)
          continue;
        for (int i = 0; i < sz; i++)
          if (gates[i].id == w.dst.gateId && --ind[i] == 0) {
            q.push_back(i);
            break;
          }
      }
    }
    if ((int)r.size() < sz) {
      std::vector<bool> vis(sz, false);
      for (int i : r)
        vis[i] = true;
      for (int i = 0; i < sz; i++)
        if (!vis[i])
          r.push_back(i);
    }
    return r;
  }
  void markPinConn(int gateId, int pinIdx, bool isOut, bool val) {
    if (gateId < 0) { // subcircuit instance
      int instId = -gateId;
      SubcircuitInst *inst = findInst(instId);
      if (!inst)
        return;
      if (isOut && pinIdx < (int)inst->outputPins.size())
        inst->outputPins[pinIdx].connected = val;
      if (!isOut && pinIdx < (int)inst->inputPins.size())
        inst->inputPins[pinIdx].connected = val;
      return;
    }
    Gate *g = find(gateId);
    if (!g)
      return;
    if (isOut && pinIdx < (int)g->outPins.size())
      g->outPins[pinIdx].connected = val;
    if (!isOut && pinIdx < (int)g->inPins.size())
      g->inPins[pinIdx].connected = val;
  }
  void recomputePin(int gateId, int pinIdx, bool isOut, int skipId) {
    bool still = false;
    for (auto &w : wires) {
      if (w.id == skipId)
        continue;
      if (isOut && w.src.gateId == gateId && w.src.pinIdx == pinIdx) {
        still = true;
        break;
      }
      if (!isOut && w.dst.gateId == gateId && w.dst.pinIdx == pinIdx) {
        still = true;
        break;
      }
    }
    markPinConn(gateId, pinIdx, isOut, still);
  }
};

// =============================================================================
// §1c  SubcircuitDef / SubcircuitInst method bodies (need Circuit complete)
// =============================================================================

inline SubcircuitDef::SubcircuitDef()
    : id(0), name("Module"), inner(new Circuit()) {}

inline SubcircuitDef::~SubcircuitDef() { delete inner; }

inline SubcircuitDef::SubcircuitDef(const SubcircuitDef &o)
    : id(o.id), name(o.name), inputPins(o.inputPins), outputPins(o.outputPins) {
  inner = new Circuit(*o.inner);
}

inline SubcircuitDef &SubcircuitDef::operator=(const SubcircuitDef &o) {
  if (this == &o)
    return *this;
  id = o.id;
  name = o.name;
  inputPins = o.inputPins;
  outputPins = o.outputPins;
  delete inner;
  inner = new Circuit(*o.inner);
  return *this;
}

inline void SubcircuitDef::rebuildPins() {
  inputPins.clear();
  outputPins.clear();
  for (auto &g : inner->gates) {
    if (g.type == GateType::INPUT || g.type == GateType::CLOCK) {
      SubcircuitPinInfo p;
      p.label = g.label.empty() ? "I" + std::to_string(g.id) : g.label;
      p.internalGateId = g.id;
      p.isOutput = false;
      inputPins.push_back(p);
    } else if (g.type == GateType::OUTPUT) {
      SubcircuitPinInfo p;
      p.label = g.label.empty() ? "O" + std::to_string(g.id) : g.label;
      p.internalGateId = g.id;
      p.isOutput = true;
      outputPins.push_back(p);
    }
  }
  auto byY = [&](const SubcircuitPinInfo &a, const SubcircuitPinInfo &b) {
    const Gate *ga = inner->find(a.internalGateId);
    const Gate *gb = inner->find(b.internalGateId);
    return (ga ? ga->y : 0.f) < (gb ? gb->y : 0.f);
  };
  std::sort(inputPins.begin(), inputPins.end(), byY);
  std::sort(outputPins.begin(), outputPins.end(), byY);
}

inline std::string SubcircuitDef::toJson() const {
  return js::obj(
      {js::kv("id", id), js::kv("name", name), "\"inner\":" + inner->toJson()});
}
inline void SubcircuitDef::fromJson(const std::string &src) {
  size_t p = 0;
  for (auto &[k, v] : js::parseObj(src, p)) {
    if (k == "id")
      id = js::asInt(v);
    else if (k == "name")
      name = js::asStr(v);
    else if (k == "inner")
      inner->fromJson(v);
  }
  rebuildPins();
}

inline void SubcircuitInst::recomputePins(const SubcircuitDef &def) {
  inputPins = def.inputPins;
  outputPins = def.outputPins;
  int ni = (int)inputPins.size(), no = (int)outputPins.size();
  float h = subcircuitH(ni, no);
  float L = x - kSCWidth * 0.5f, R = x + kSCWidth * 0.5f, yT = y - h * 0.5f;
  auto place = [&](SubcircuitPinInfo &p, int i, int total, bool isIn) {
    float t = (total <= 1) ? 0.5f : float(i) / float(total - 1);
    p.cx = isIn ? (L - kPinLen) : (R + kPinLen);
    p.cy = yT + t * h;
  };
  for (int i = 0; i < ni; i++)
    place(inputPins[i], i, ni, true);
  for (int i = 0; i < no; i++)
    place(outputPins[i], i, no, false);
}

// =============================================================================
// §2  VERTEX HELPERS  (unchanged from v12)
// =============================================================================

static void pushQuad(std::vector<float> &v, float x0, float y0, float x1,
                     float y1) {
  v.insert(v.end(), {x0, y0, x1, y0, x1, y1, x1, y1, x0, y1, x0, y0});
}
static void pushLine(std::vector<float> &v, float x0, float y0, float x1,
                     float y1, float hw) {
  float dx = x1 - x0, dy = y1 - y0, len = sqrtf(dx * dx + dy * dy);
  if (len < 0.001f) {
    pushQuad(v, x0 - hw, y0 - hw, x0 + hw, y0 + hw);
    return;
  }
  float nx = -dy / len * hw, ny = dx / len * hw;
  v.insert(v.end(), {x0 + nx, y0 + ny, x0 - nx, y0 - ny, x1 - nx, y1 - ny,
                     x1 - nx, y1 - ny, x1 + nx, y1 + ny, x0 + nx, y0 + ny});
}
static void pushCircle(std::vector<float> &v, float cx, float cy, float r,
                       int s = 16) {
  for (int i = 0; i < s; i++) {
    float a0 = float(i) / s * 6.2831853f, a1 = float(i + 1) / s * 6.2831853f;
    v.insert(v.end(), {cx, cy, cx + cosf(a0) * r, cy + sinf(a0) * r,
                       cx + cosf(a1) * r, cy + sinf(a1) * r});
  }
}
static void pushRRFill(std::vector<float> &v, float L, float B, float R2,
                       float T, float cr) {
  cr = min(cr, min((R2 - L) * .45f, (T - B) * .45f));
  pushQuad(v, L + cr, B, R2 - cr, T);
  pushQuad(v, L, B + cr, L + cr, T - cr);
  pushQuad(v, R2 - cr, B + cr, R2, T - cr);
  float ccx[4] = {R2 - cr, L + cr, L + cr, R2 - cr},
        ccy[4] = {T - cr, T - cr, B + cr, B + cr},
        sa[4] = {0, 1.5708f, 3.1416f, 4.7124f};
  for (int c = 0; c < 4; c++)
    for (int i = 0; i < 8; i++) {
      float a0 = sa[c] + float(i) / 8 * 1.5708f,
            a1 = sa[c] + float(i + 1) / 8 * 1.5708f;
      v.insert(v.end(),
               {ccx[c], ccy[c], ccx[c] + cosf(a0) * cr, ccy[c] + sinf(a0) * cr,
                ccx[c] + cosf(a1) * cr, ccy[c] + sinf(a1) * cr});
    }
}
static void pushRR(std::vector<float> &v, float L, float B, float R2, float T,
                   float cr, float hw, int cs = 6) {
  cr = min(cr, min((R2 - L) * .4f, (T - B) * .4f));
  pushLine(v, L + cr, B, R2 - cr, B, hw);
  pushLine(v, R2, B + cr, R2, T - cr, hw);
  pushLine(v, R2 - cr, T, L + cr, T, hw);
  pushLine(v, L, T - cr, L, B + cr, hw);
  float ox[4] = {R2 - cr, L + cr, L + cr, R2 - cr},
        oy[4] = {T - cr, T - cr, B + cr, B + cr},
        sa[4] = {0, 1.5708f, 3.1416f, 4.7124f};
  for (int c = 0; c < 4; c++)
    for (int i = 0; i < cs; i++) {
      float a0 = sa[c] + float(i) / cs * 1.5708f,
            a1 = sa[c] + float(i + 1) / cs * 1.5708f;
      pushLine(v, ox[c] + cosf(a0) * cr, oy[c] + sinf(a0) * cr,
               ox[c] + cosf(a1) * cr, oy[c] + sinf(a1) * cr, hw);
    }
}

// §2a  ORTHOGONAL WIRE HELPERS (unchanged)
static float orthoMidSX(float sx0, float sx1, float splitXS) {
  return splitXS < 1e29f ? splitXS : (sx0 + sx1) * 0.5f;
}
static void pushOrtho(std::vector<float> &v, float sx0, float sy0, float sx1,
                      float sy1, float splitXS, float hw) {
  float mx = orthoMidSX(sx0, sx1, splitXS);
  pushLine(v, sx0, sy0, mx, sy0, hw);
  pushLine(v, mx, sy0, mx, sy1, hw);
  pushLine(v, mx, sy1, sx1, sy1, hw);
}
static void pushOrthoBends(std::vector<float> &v, float sx0, float sy0,
                           float sx1, float sy1, float splitXS, float r) {
  float mx = orthoMidSX(sx0, sx1, splitXS);
  pushCircle(v, mx, sy0, r);
  pushCircle(v, mx, sy1, r);
}
static bool orthoHit(float sx0, float sy0, float sx1, float sy1, float splitXS,
                     float px, float py, float thr = 6.f) {
  float mx = orthoMidSX(sx0, sx1, splitXS), t2 = thr * thr;
  auto seg2 = [&](float ax, float ay, float bx, float by) -> float {
    float dx = bx - ax, dy = by - ay, len2 = dx * dx + dy * dy;
    float t = (len2 > 0.f) ? ((px - ax) * dx + (py - ay) * dy) / len2 : 0.f;
    t = t < 0.f ? 0.f : t > 1.f ? 1.f : t;
    float cx = ax + dx * t - px, cy = ay + dy * t - py;
    return cx * cx + cy * cy;
  };
  return seg2(sx0, sy0, mx, sy0) <= t2 || seg2(mx, sy0, mx, sy1) <= t2 ||
         seg2(mx, sy1, sx1, sy1) <= t2;
}

static std::vector<PinRef> collectPins(const Circuit &c) {
  std::vector<PinRef> out;
  for (auto &g : c.gates) {
    for (int i = 0; i < (int)g.outPins.size(); i++)
      out.push_back({g.id, i, true, g.outPins[i].cx, g.outPins[i].cy, false});
    for (int i = 0; i < (int)g.inPins.size(); i++)
      out.push_back({g.id, i, false, g.inPins[i].cx, g.inPins[i].cy, false});
  }
  // Add subcircuit instance pins (encode id as negative)
  for (auto &inst : c.subcircuitInsts) {
    int neg = -inst.id;
    for (int i = 0; i < (int)inst.outputPins.size(); i++)
      out.push_back(
          {neg, i, true, inst.outputPins[i].cx, inst.outputPins[i].cy, true});
    for (int i = 0; i < (int)inst.inputPins.size(); i++)
      out.push_back(
          {neg, i, false, inst.inputPins[i].cx, inst.inputPins[i].cy, true});
  }
  return out;
}
static bool nearestPin(const std::vector<PinRef> &pins, float wx, float wy,
                       float radius, bool mustOut, bool mustIn, PinRef &out) {
  float best = radius * radius;
  bool found = false;
  for (auto &p : pins) {
    if (mustOut && !p.isOutput)
      continue;
    if (mustIn && p.isOutput)
      continue;
    float dx = p.cx - wx, dy = p.cy - wy, d2 = dx * dx + dy * dy;
    if (d2 < best) {
      best = d2;
      out = p;
      found = true;
    }
  }
  return found;
}

// §2b  IEEE/ANSI SHAPE BUILDERS (unchanged from v12 — omitted for brevity,
//      paste in full from original file here)
static void pushCubic(std::vector<float> &v, float x0, float y0, float x1,
                      float y1, float x2, float y2, float x3, float y3,
                      float hw, int segs = 22) {
  float px = x0, py = y0;
  for (int i = 1; i <= segs; ++i) {
    float t = float(i) / float(segs), u = 1 - t, u2 = u * u, u3 = u2 * u,
          t2 = t * t, t3 = t2 * t;
    float qx = u3 * x0 + 3 * u2 * t * x1 + 3 * u * t2 * x2 + t3 * x3,
          qy = u3 * y0 + 3 * u2 * t * y1 + 3 * u * t2 * y2 + t3 * y3;
    pushLine(v, px, py, qx, qy, hw);
    px = qx;
    py = qy;
  }
}
static void pushArc(std::vector<float> &v, float cx, float cy, float r,
                    float a0, float a1, float hw, int segs = 24) {
  float px = cx + cosf(a0) * r, py = cy + sinf(a0) * r;
  for (int i = 1; i <= segs; ++i) {
    float a = a0 + (a1 - a0) * float(i) / float(segs);
    float qx = cx + cosf(a) * r, qy = cy + sinf(a) * r;
    pushLine(v, px, py, qx, qy, hw);
    px = qx;
    py = qy;
  }
}
static void pushRing(std::vector<float> &v, float cx, float cy, float r,
                     float hw) {
  pushArc(v, cx, cy, r, 0.f, 6.2831853f, hw, 20);
}
static void pushTriFill(std::vector<float> &v, float L, float yT, float R,
                        float yB) {
  float MY = (yT + yB) * 0.5f;
  v.insert(v.end(), {L, yT, R, MY, L, yB});
}
static void pushANDFill(std::vector<float> &v, float L, float yT, float R,
                        float yB) {
  float H = yB - yT, MY = (yT + yB) * 0.5f, flatX = L + (R - L) * 0.44f,
        arcR = H * 0.5f;
  pushQuad(v, L, yT, flatX, yB);
  for (int i = 0; i < 24; ++i) {
    float a0 = -1.5707963f + float(i) / 24 * 3.1415926f,
          a1 = -1.5707963f + float(i + 1) / 24 * 3.1415926f;
    v.insert(v.end(), {flatX, MY, flatX + cosf(a0) * arcR, MY + sinf(a0) * arcR,
                       flatX + cosf(a1) * arcR, MY + sinf(a1) * arcR});
  }
}
static void pushANDOutline(std::vector<float> &v, float L, float yT, float R,
                           float yB, float hw) {
  float H = yB - yT, MY = (yT + yB) * 0.5f, flatX = L + (R - L) * 0.44f,
        arcR = H * 0.5f;
  pushLine(v, L, yT, L, yB, hw);
  pushLine(v, L, yT, flatX, yT, hw);
  pushLine(v, L, yB, flatX, yB, hw);
  pushArc(v, flatX, MY, arcR, -1.5707963f, 1.5707963f, hw, 28);
}
static void pushORFill(std::vector<float> &v, float L, float yT, float R,
                       float yB) {
  float H = yB - yT, W = R - L, MY = (yT + yB) * 0.5f, bulgX = W * 0.30f;
  auto backX = [&](float y) -> float {
    float t = (y - yT) / H, u = 1.f - t;
    return u * u * u * L + 3 * u * u * t * (L + bulgX) +
           3 * u * t * t * (L + bulgX) + t * t * t * L;
  };
  auto frontX = [&](float y) -> float {
    bool top = y <= MY;
    float lo = 0.f, hi = 1.f;
    for (int k = 0; k < 18; ++k) {
      float m = (lo + hi) * 0.5f, u = 1.f - m;
      float ym = top ? (u * u * u * yT + 3 * u * u * m * yT +
                        3 * u * m * m * (MY - H * 0.10f) + m * m * m * MY)
                     : (u * u * u * yB + 3 * u * u * m * yB +
                        3 * u * m * m * (MY + H * 0.10f) + m * m * m * MY);
      if (top ? (ym < y) : (ym > y))
        lo = m;
      else
        hi = m;
    }
    float t = (lo + hi) * 0.5f, u = 1.f - t;
    return u * u * u * L + 3 * u * u * t * (L + W * 0.55f) +
           3 * u * t * t * (R - W * 0.12f) + t * t * t * R;
  };
  for (int i = 0; i < 64; ++i) {
    float y0 = yT + float(i) / 64 * H, y1 = yT + float(i + 1) / 64 * H;
    float lx0 = backX(y0), rx0 = frontX(y0), lx1 = backX(y1), rx1 = frontX(y1);
    v.insert(v.end(), {lx0, y0, rx0, y0, rx1, y1, lx0, y0, rx1, y1, lx1, y1});
  }
}
static void pushOROutline(std::vector<float> &v, float L, float yT, float R,
                          float yB, float hw) {
  float W = R - L, MY = (yT + yB) * 0.5f, bulgX = W * 0.30f;
  pushCubic(v, L, yT, L + bulgX, yT, L + bulgX, yB, L, yB, hw);
  pushCubic(v, L, yT, L + W * 0.55f, yT, R - W * 0.12f, MY - (yB - yT) * 0.10f,
            R, MY, hw);
  pushCubic(v, L, yB, L + W * 0.55f, yB, R - W * 0.12f, MY + (yB - yT) * 0.10f,
            R, MY, hw);
}
static void pushXORBackArc(std::vector<float> &v, float L, float yT, float R,
                           float yB, float hw) {
  float W = R - L, bulgX = W * 0.30f, offset = W * 0.11f;
  pushCubic(v, L - offset, yT, L - offset + bulgX, yT, L - offset + bulgX, yB,
            L - offset, yB, hw);
}
static void pushTRIOutline(std::vector<float> &v, float L, float yT, float R,
                           float yB, float hw, bool negate) {
  float MY = (yT + yB) * 0.5f, H = yB - yT, tipX = negate ? (R - H * 0.20f) : R;
  pushLine(v, L, yT, L, yB, hw);
  pushLine(v, L, yT, tipX, MY, hw);
  pushLine(v, tipX, MY, L, yB, hw);
}
static void pushSquareWave(std::vector<float> &v, float cx, float cy, float gW,
                           float gH, float hw) {
  float x0 = cx - gW * 0.5f, y_lo = cy + gH * 0.35f, y_hi = cy - gH * 0.35f,
        pw = gW / 3.f;
  float xs[10] = {x0,
                  x0 + pw * .5f,
                  x0 + pw * .5f,
                  x0 + pw,
                  x0 + pw * 1.5f,
                  x0 + pw * 1.5f,
                  x0 + pw * 2.f,
                  x0 + pw * 2.5f,
                  x0 + pw * 2.5f,
                  x0 + pw * 3.f};
  float ys[10] = {y_lo, y_lo, y_hi, y_hi, y_lo, y_lo, y_hi, y_hi, y_lo, y_lo};
  for (int i = 0; i < 9; ++i)
    pushLine(v, xs[i], ys[i], xs[i + 1], ys[i + 1], hw);
}

// =============================================================================
// §3  SHADERS  (unchanged)
// =============================================================================
static const char *kGVert = R"GLSL(
#version 330 core
layout(location=0)in vec2 aPos;
uniform vec2 uViewSize;
void main(){vec2 ndc=aPos/uViewSize*2.0-1.0;ndc.y=-ndc.y;gl_Position=vec4(ndc,0.0,1.0);}
)GLSL";
static const char *kGFrag = R"GLSL(
#version 330 core
out vec4 fragColor;
uniform vec4 uColor;
void main(){fragColor=uColor;}
)GLSL";
static const char *kTexVert = R"GLSL(
#version 330 core
layout(location=0)in vec2 aPos;layout(location=1)in vec2 aUV;
uniform vec2 uViewSize;out vec2 vUV;
void main(){vUV=aUV;vec2 ndc=aPos/uViewSize*2.0-1.0;ndc.y=-ndc.y;gl_Position=vec4(ndc,0.0,1.0);}
)GLSL";
static const char *kTexFrag = R"GLSL(
#version 330 core
in vec2 vUV;uniform sampler2D uTex;out vec4 fragColor;
void main(){fragColor=texture(uTex,vUV);}
)GLSL";

// =============================================================================
// §4  GATE COLORS  (unchanged)
// =============================================================================
struct GateColors {
  float body[4], border[4], label[4];
};
static GateColors gateColors(GateType t, bool sel) {
static const float bodies[kGTC][4] = {
    {0.08f, 0.15f, 0.30f, 1}, {0.08f, 0.25f, 0.15f, 1},
    {0.22f, 0.10f, 0.28f, 1}, {0.26f, 0.08f, 0.10f, 1},
    {0.20f, 0.08f, 0.22f, 1}, {0.08f, 0.20f, 0.30f, 1},
    {0.12f, 0.12f, 0.28f, 1}, {0.08f, 0.22f, 0.13f, 1},
    {0.24f, 0.14f, 0.05f, 1}, {0.05f, 0.15f, 0.28f, 1},
    {0.06f, 0.18f, 0.26f, 1}, {0.06f, 0.22f, 0.20f, 1},
    {0.20f, 0.16f, 0.06f, 1}, {0.22f, 0.10f, 0.18f, 1},
    // BUS_IN_2/4/8  — blue tint
    {0.05f, 0.14f, 0.30f, 1}, {0.05f, 0.14f, 0.30f, 1}, {0.05f, 0.14f, 0.30f, 1},
    // BUS_OUT_2/4/8 — amber tint
    {0.28f, 0.14f, 0.04f, 1}, {0.28f, 0.14f, 0.04f, 1}, {0.28f, 0.14f, 0.04f, 1},
};
static const float borders[kGTC][4] = {
    {0.27f, 0.47f, 0.86f, 1}, {0.27f, 0.75f, 0.43f, 1},
    {0.66f, 0.27f, 0.86f, 1}, {0.86f, 0.27f, 0.27f, 1},
    {0.78f, 0.27f, 0.67f, 1}, {0.27f, 0.67f, 0.86f, 1},
    {0.43f, 0.43f, 0.86f, 1}, {0.27f, 0.82f, 0.43f, 1},
    {0.86f, 0.51f, 0.16f, 1}, {0.18f, 0.82f, 0.90f, 1},
    {0.20f, 0.65f, 0.95f, 1}, {0.20f, 0.85f, 0.75f, 1},
    {0.95f, 0.72f, 0.20f, 1}, {0.95f, 0.38f, 0.60f, 1},
    // BUS_IN_2/4/8
    {0.31f, 0.63f, 0.95f, 1}, {0.31f, 0.63f, 0.95f, 1}, {0.31f, 0.63f, 0.95f, 1},
    // BUS_OUT_2/4/8
    {0.95f, 0.63f, 0.31f, 1}, {0.95f, 0.63f, 0.31f, 1}, {0.95f, 0.63f, 0.31f, 1},
};
int i = (int)t;
  GateColors c;
  memcpy(c.body, bodies[i], 16);
  memcpy(c.label, borders[i], 16);
  if (sel) {
    c.border[0] = 0.80f;
    c.border[1] = 0.65f;
    c.border[2] = 0.97f;
    c.border[3] = 1.f;
  } else
    memcpy(c.border, borders[i], 16);
  return c;
}

// =============================================================================
// §4b  GATE CONTEXT MENU  (unchanged)
// =============================================================================
enum class GateMenuAction {
  None = -1,
  Delete = 0,
  Copy = 1,
  GroupSubcircuit = 2
};
struct GateMenuItem {
  GateMenuAction action;
  const char *label;
  int iconType;
};
static constexpr GateMenuItem kGateMenuItems[] = {
    {GateMenuAction::Copy, "Copy", 1},
    {GateMenuAction::Delete, "Delete", 0},
};
static constexpr int kGateMenuItemCount = 2;
static constexpr float kCMItemH = 28.f, kCMWidth = 130.f, kCMPadV = 6.f,
                       kCMCorner = 6.f;
static constexpr float kCMRowGap = 4.f, kCMRowStride = kCMItemH + kCMRowGap;
static constexpr float kCMIconSize = 11.f, kCMIconOff = 14.f, kCMTextOff = 28.f;
static constexpr float kCMHeight = kCMPadV * 2.f +
                                   kGateMenuItemCount * kCMItemH +
                                   (kGateMenuItemCount - 1) * kCMRowGap;

static void pushIconTrash(std::vector<float> &v, float cx, float cy, float sz,
                          float lw) {
  float hs = sz * 0.5f, bW = sz * 0.68f, bH = sz * 0.62f, bL = cx - bW * 0.5f,
        bR = cx + bW * 0.5f, bT = cy - hs * 0.10f, bB = cy + hs * 0.78f;
  pushLine(v, bL, bT, bL, bB, lw);
  pushLine(v, bR, bT, bR, bB, lw);
  pushLine(v, bL, bB, bR, bB, lw);
  float lidT = bT - sz * 0.22f;
  pushLine(v, bL - sz * 0.10f, bT, bR + sz * 0.10f, bT, lw);
  pushLine(v, cx - sz * 0.18f, lidT, cx + sz * 0.18f, lidT, lw);
  pushLine(v, cx - sz * 0.18f, lidT, bL - sz * 0.10f, bT, lw);
  pushLine(v, cx + sz * 0.18f, lidT, bR + sz * 0.10f, bT, lw);
  float lineY0 = bT + bH * 0.18f, lineY1 = bB - sz * 0.08f;
  pushLine(v, cx, lineY0, cx, lineY1, lw * 0.8f);
  pushLine(v, cx - bW * 0.22f, lineY0, cx - bW * 0.22f, lineY1, lw * 0.8f);
  pushLine(v, cx + bW * 0.22f, lineY0, cx + bW * 0.22f, lineY1, lw * 0.8f);
}
static void pushIconCopy(std::vector<float> &v, float cx, float cy, float sz,
                         float lw) {
  float hs = sz * 0.5f, pW = sz * 0.58f, pH = sz * 0.68f, off = sz * 0.14f;
  float bL = cx - pW * 0.5f + off, bB = cy - pH * 0.5f + off;
  pushRR(v, bL, bB, bL + pW, bB + pH, 2.f, lw * 0.7f, 3);
  float fL = cx - pW * 0.5f, fB = cy - pH * 0.5f;
  pushRRFill(v, fL, fB, fL + pW, fB + pH, 2.f);
  pushRR(v, fL, fB, fL + pW, fB + pH, 2.f, lw, 3);
  float lx0 = fL + pW * 0.18f, lx1 = fL + pW * 0.82f, ly0 = fB + pH * 0.30f,
        ly1 = fB + pH * 0.55f, ly2 = fB + pH * 0.75f;
  pushLine(v, lx0, ly0, lx1, ly0, lw * 0.7f);
  pushLine(v, lx0, ly1, lx1, ly1, lw * 0.7f);
  pushLine(v, lx0, ly2, lx1 * 0.75f + lx0 * 0.25f, ly2, lw * 0.7f);
}

// =============================================================================
// §5  CIRCUIT SURFACE
// =============================================================================
static constexpr UINT_PTR kClockTimerBase = 0x4C4B0000;

class CircuitSurface : public RenderSurface {
public:
  Circuit circuit;
  SimMode simMode = SimMode::Edit;
  bool snapEnabled = true;

  std::function<void()> onCircuitChanged;
  std::function<void(const char *)> onStatusMessage;
  std::function<void()> onRedrawNeeded;
  std::function<void(float)> onZoomChanged;
  std::function<void(SimMode)> onSimModeChanged;
  std::function<void()> onSimUpdated;
  std::function<void(int, const std::string &)> onLabelEdit;
  std::function<void()> onSaveRequest;
  std::function<void()> onLoadRequest;

  void pushUndo() {
    if (undoPos_ < (int)undoStack_.size())
      undoStack_.erase(undoStack_.begin() + undoPos_, undoStack_.end());
    undoStack_.push_back(circuit.toJson());
    if ((int)undoStack_.size() > kMaxUndo)
      undoStack_.erase(undoStack_.begin());
    undoPos_ = (int)undoStack_.size();
  }
  void undo() {
    if (undoPos_ <= 0)
      return;
    if (undoPos_ == (int)undoStack_.size())
      undoStack_.push_back(circuit.toJson());
    --undoPos_;
    circuit.fromJson(undoStack_[undoPos_]);
    afterEdit("Undo");
  }
  void redo() {
    if (undoPos_ >= (int)undoStack_.size() - 1)
      return;
    ++undoPos_;
    circuit.fromJson(undoStack_[undoPos_]);
    afterEdit("Redo");
  }

  // ── Clock timers ──────────────────────────────────────────────────────
  void startClockTimers() {
    if (!hwnd_)
      return;
    for (auto &g : circuit.gates)
      if (g.type == GateType::CLOCK) {
        float hz = g.clockHz > 0.f ? g.clockHz : 1.f;
        UINT ms = max(16u, (UINT)(500.f / hz));
        SetTimer(hwnd_, kClockTimerBase + (UINT_PTR)g.id, ms, nullptr);
      }
  }
  void stopClockTimers() {
    if (!hwnd_)
      return;
    for (auto &g : circuit.gates)
      if (g.type == GateType::CLOCK)
        KillTimer(hwnd_, kClockTimerBase + (UINT_PTR)g.id);
  }
  void restartClockTimer(int gateId) {
    Gate *g = circuit.find(gateId);
    if (!g || !hwnd_)
      return;
    KillTimer(hwnd_, kClockTimerBase + (UINT_PTR)gateId);
    if (simMode == SimMode::Running) {
      float hz = g->clockHz > 0.f ? g->clockHz : 1.f;
      UINT ms = max(16u, (UINT)(500.f / hz));
      SetTimer(hwnd_, kClockTimerBase + (UINT_PTR)gateId, ms, nullptr);
    }
  }
  void cycleClockFreq(int gateId) {
    Gate *g = circuit.find(gateId);
    if (!g || g->type != GateType::CLOCK)
      return;
    pushUndo();
    int best = 0;
    float bdiff = 1e9f;
    for (int i = 0; i < kClockFreqCount; ++i) {
      float d = fabsf(g->clockHz - kClockFreqPresets[i]);
      if (d < bdiff) {
        bdiff = d;
        best = i;
      }
    }
    g->clockHz = kClockFreqPresets[(best + 1) % kClockFreqCount];
    restartClockTimer(gateId);
    char buf[64];
    snprintf(buf, sizeof(buf), "Clock %.1f Hz", g->clockHz);
    if (onStatusMessage)
      onStatusMessage(buf);
    if (onCircuitChanged)
      onCircuitChanged();
    redraw();
  }

  void simPlay() {
    if (circuit.gates.empty() && circuit.subcircuitInsts.empty())
      return;
    simMode = SimMode::Running;
    circuit.evaluate();
    notifyMode();
    startClockTimers();
    if (onSimUpdated)
      onSimUpdated();
    if (onStatusMessage)
      onStatusMessage("▶ Running  ·  Click INPUT/CLOCK  ·  Space=pause");
    redraw();
  }
  void simPause() {
    if (simMode == SimMode::Edit)
      return;
    simMode =
        (simMode == SimMode::Running) ? SimMode::Paused : SimMode::Running;
    if (simMode == SimMode::Running)
      startClockTimers();
    else
      stopClockTimers();
    notifyMode();
    if (onStatusMessage)
      onStatusMessage(simMode == SimMode::Paused ? "⏸ Paused"
                                                 : "▶ Running  ·  Space=pause");
    redraw();
  }
  void simStop() {
    stopClockTimers();
    simMode = SimMode::Edit;
    for (auto &w : circuit.wires)
      w.value = false;
    for (auto &g : circuit.gates) {
      g.simVal = false;
      if (g.type == GateType::CLOCK)
        g.inputVal = false;
    }
    circuit.resetSequentialState();
    notifyMode();
    if (onSimUpdated)
      onSimUpdated();
    if (onStatusMessage)
      onStatusMessage("■ Stopped  ·  Edit circuit then press ▶ Play");
    redraw();
  }
  void simStep() {
    if (simMode == SimMode::Edit)
      simMode = SimMode::Paused;
    for (auto &g : circuit.gates)
      if (g.type == GateType::CLOCK)
        g.inputVal = !g.inputVal;
    circuit.evaluate();
    notifyMode();
    if (onSimUpdated)
      onSimUpdated();
    if (onStatusMessage)
      onStatusMessage("⏭ Stepped");
    redraw();
  }
  bool isSimActive() const { return simMode != SimMode::Edit; }
  bool isSimRunning() const { return simMode == SimMode::Running; }

  void applyLabel(int gateId, const std::string &lbl) {
    Gate *g = circuit.find(gateId);
    if (!g)
      return;
    pushUndo();
    g->label = lbl;
    if (onCircuitChanged)
      onCircuitChanged();
    if (onSimUpdated)
      onSimUpdated();
    redraw();
  }
  bool saveToFile(const std::string &path) {
    std::ofstream f(path);
    if (!f)
      return false;
    f << circuit.toJson();
    return f.good();
  }
  bool loadFromFile(const std::string &path) {
    std::ifstream f(path);
    if (!f)
      return false;
    std::string src((std::istreambuf_iterator<char>(f)),
                    std::istreambuf_iterator<char>());
    if (!circuit.fromJson(src))
      return false;
    undoStack_.clear();
    undoPos_ = 0;
    pushUndo();
    afterEdit("Loaded");
    return true;
  }

  void resetZoom() {
    zoom_ = 1.f;
    camX_ = 0.f;
    camY_ = 0.f;
    redraw();
  }
  void fitToView() {
    if (circuit.gates.empty() && circuit.subcircuitInsts.empty()) {
      resetZoom();
      return;
    }
    float mnX = 1e9f, mnY = 1e9f, mxX = -1e9f, mxY = -1e9f;
    for (auto &g : circuit.gates) {
      float gh = gateH(g.type);
      mnX = min(mnX, g.x - kGW);
      mxX = max(mxX, g.x + kGW);
      mnY = min(mnY, g.y - gh);
      mxY = max(mxY, g.y + gh);
    }
    for (auto &inst : circuit.subcircuitInsts) {
      auto it = circuit.subcircuitDefs.find(inst.defId);
      int ni = it != circuit.subcircuitDefs.end()
                   ? (int)it->second.inputPins.size()
                   : 1;
      int no = it != circuit.subcircuitDefs.end()
                   ? (int)it->second.outputPins.size()
                   : 1;
      float h = subcircuitH(ni, no);
      mnX = min(mnX, inst.x - kSCWidth);
      mxX = max(mxX, inst.x + kSCWidth);
      mnY = min(mnY, inst.y - h);
      mxY = max(mxY, inst.y + h);
    }
    float ww = mxX - mnX, wh = mxY - mnY;
    if (ww < 1 || wh < 1) {
      resetZoom();
      return;
    }
    float pad = 60.f, zx = (viewW_ - pad * 2) / ww,
          zy = (viewH_ - pad * 2) / wh;
    zoom_ = std::clamp(min(zx, zy), 0.05f, 4.f);
    camX_ = (mnX + mxX) * .5f - viewW_ * .5f / zoom_;
    camY_ = (mnY + mxY) * .5f - viewH_ * .5f / zoom_;
    redraw();
  }
  void setZoomLevel(float z) {
    zoom_ = std::clamp(z, 0.05f, 8.f);
    redraw();
    if (onZoomChanged)
      onZoomChanged(zoom_);
  }
  float getZoom() const { return zoom_; }

  void dropGate(GateType type) {
    if (isSimActive())
      return;
    float wx = camX_ + viewW_ * .5f / zoom_, wy = camY_ + viewH_ * .5f / zoom_;
    static int n = 0;
    int slot = (n++) % 7;
    wx += float(slot - 3) * (kGW + 24.f);
    wy += float(slot % 3 - 1) * (kGH + 18.f);
    if (snapEnabled) {
      wx = snapGrid(wx);
      wy = snapGrid(wy);
    }
    pushUndo();
    circuit.add(type, wx, wy);
    if (onCircuitChanged)
      onCircuitChanged();
    redraw();
  }

  // ── Group selected gates into a subcircuit ───────────────────────────
  void groupSelected() {
    if (isSimActive()) {
      if (onStatusMessage)
        onStatusMessage("Stop simulation before grouping");
      return;
    }
    std::vector<int> selIds;
    for (auto &g : circuit.gates)
      if (g.selected)
        selIds.push_back(g.id);
    if (selIds.size() < 1) {
      if (onStatusMessage)
        onStatusMessage("Select gates first (click/shift-click), then Group");
      return;
    }
    std::string name = dlgLabelInternal_("Subcircuit name", "Module");
    if (name == "__cancelled__")
      return;
    if (name.empty())
      name = "Module";
    pushUndo();
    int instId = circuit.groupSelected(name);
    if (instId < 0) {
      if (onStatusMessage)
        onStatusMessage("Group failed");
      return;
    }
    circuit.evaluate();
    if (onCircuitChanged)
      onCircuitChanged();
    if (onSimUpdated)
      onSimUpdated();
    if (onStatusMessage)
      onStatusMessage(("Grouped as subcircuit '" + name +
                       "'  ·  Double-click instance to inspect")
                          .c_str());
    redraw();
  }

  void initialize(int w, int h) override {
    ls_gl::init();
    viewW_ = w;
    viewH_ = h;
    camX_ = -w * .5f;
    camY_ = -h * .5f;
    buildShader();
    buildTexShader();
    buildVAO();
    buildTexVAO();
    HWND hwnd = WindowFromDC(wglGetCurrentDC());
    if (hwnd)
      hookWindow(hwnd);
  }
  void resize(int w, int h) override {
    viewW_ = w;
    viewH_ = h;
    ctxMenuTexDirty_ = true;
  }
  void update(double) override {}

  void render(const float *) override {
    glViewport(0, 0, viewW_, viewH_);
    glClearColor(0.055f, 0.055f, 0.08f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    GL.useProgram(prog_);
    ls_gl::uniform2f(uViewSize_, float(viewW_), float(viewH_));
    GL.bindVertexArray(vao_);
    drawGrid();
    drawAllWires();
    // Rubber-band selection rectangle
    if (dragSelect_) {
      float sx0, sy0, sx1, sy1;
      w2s(dragSelX0_, dragSelY0_, sx0, sy0);
      w2s(dragSelX1_, dragSelY1_, sx1, sy1);
      float bx0 = min(sx0, sx1), bx1 = max(sx0, sx1);
      float by0 = min(sy0, sy1), by1 = max(sy0, sy1);
      {
        std::vector<float> v;
        pushQuad(v, bx0, by0, bx1, by1);
        dc(v, 0.68f, 0.48f, 1.0f, 0.07f);
      }
      {
        std::vector<float> v;
        pushRR(v, bx0, by0, bx1, by1, 2.f, 1.2f);
        dc(v, 0.68f, 0.48f, 1.0f, 0.70f);
      }
    }
    drawDragPreview();
    for (auto &inst : circuit.subcircuitInsts)
      drawSubcircuitInst(inst);
    for (auto &g : circuit.gates)
      drawGate(g);
    drawPinHovers();
    if (isSimActive())
      drawSimBorder();
    if (ctxMenu_.open)
      drawContextMenu();
    GL.bindVertexArray(0);
    GL.useProgram(0);
    glDisable(GL_BLEND);
  }
  void destroy() override {
    stopClockTimers();
    if (prog_) {
      GL.deleteProgram(prog_);
      prog_ = 0;
    }
    if (texProg_) {
      GL.deleteProgram(texProg_);
      texProg_ = 0;
    }
    if (vao_) {
      GL.deleteVertexArrays(1, &vao_);
      vao_ = 0;
    }
    if (vbo_) {
      GL.deleteBuffers(1, &vbo_);
      vbo_ = 0;
    }
    if (texVao_) {
      GL.deleteVertexArrays(1, &texVao_);
      texVao_ = 0;
    }
    if (texVbo_) {
      GL.deleteBuffers(1, &texVbo_);
      texVbo_ = 0;
    }
    destroyCtxMenuTex();
  }
  bool needsContinuousRedraw() const override { return false; }

  // =========================================================================
  // Mouse events
  // =========================================================================
  void onMouseDown(float sx, float sy) override {
    if (ctxMenu_.open) {
      float mx = sx, my = sy;
      if (hwnd_) {
        POINT pt;
        GetCursorPos(&pt);
        ScreenToClient(hwnd_, &pt);
        mx = float(pt.x);
        my = float(pt.y);
      }
      int item = ctxMenuHitItem(mx, my);
      int gateId = ctxMenu_.gateId;
      ctxMenuClose();
      if (item >= 0 && gateId >= 0) {
        switch (kGateMenuItems[item].action) {
        case GateMenuAction::Delete: {
          Gate *g = circuit.find(gateId);
          if (g) {
            if (g->type == GateType::CLOCK && hwnd_)
              KillTimer(hwnd_, kClockTimerBase + (UINT_PTR)gateId);
            pushUndo();
            circuit.removeGateWires(gateId);
            circuit.remove(gateId);
            circuit.evaluate();
            if (onCircuitChanged)
              onCircuitChanged();
            if (onStatusMessage)
              onStatusMessage("Gate deleted");
          }
          break;
        }
        case GateMenuAction::Copy: {
          Gate *g = circuit.find(gateId);
          if (g) {
            pushUndo();
            int nid = circuit.add(g->type, g->x + kGW + 20.f, g->y, g->label);
            Gate *ng = circuit.find(nid);
            if (ng) {
              ng->inputVal = g->inputVal;
              ng->clockHz = g->clockHz;
            }
            circuit.evaluate();
            if (onCircuitChanged)
              onCircuitChanged();
            if (onStatusMessage)
              onStatusMessage("Gate copied");
          }
          break;
        }
        default:
          break;
        }
      }
      redraw();
      return;
    }

    lastSX_ = sx;
    lastSY_ = sy;
    float wx, wy;
    s2w(sx, sy, wx, wy);
    bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

    if (isSimActive()) {
      int hit = hitGate(wx, wy);
      if (hit >= 0) {
        Gate *g = circuit.find(hit);
        if (g && g->type == GateType::INPUT) {
          g->inputVal = !g->inputVal;
          circuit.evaluate();
          if (onSimUpdated)
            onSimUpdated();
          if (onCircuitChanged)
            onCircuitChanged();
          redraw();
        } else if (g && g->type == GateType::CLOCK)
          cycleClockFreq(hit);
      }
      return;
    }

    // Wire split drag (unchanged)
    for (auto &w : circuit.wires) {
      auto [wsx0, wsy0, wsx1, wsy1] = wireSC(w);
      float msx = wireSplitSX(w, wsx0, wsx1), thr = max(6.f, 5.f / zoom_);
      if (std::abs(sx - msx) <= thr && sy >= min(wsy0, wsy1) - thr &&
          sy <= max(wsy0, wsy1) + thr) {
        dragSplit_ = true;
        dragSplitId_ = w.id;
        float worldMX = (w.splitX < 1e29f) ? w.splitX : (camX_ + msx / zoom_);
        dragSplitOff_ = wx - worldMX;
        if (!shift) {
          for (auto &g2 : circuit.gates)
            g2.selected = false;
          for (auto &inst : circuit.subcircuitInsts)
            inst.selected = false;
        }
        for (auto &w2 : circuit.wires)
          w2.selected = (w2.id == w.id);
        if (onStatusMessage)
          onStatusMessage("Drag to reposition wire");
        redraw();
        return;
      }
    }

    // Start wire drag from output pin (unchanged)
    {
      auto pins = collectPins(circuit);
      PinRef src{};
      if (nearestPin(pins, wx, wy, 16.f / zoom_, true, false, src)) {
        dragWire_ = true;
        dragSrc_ = src;
        dragCurWX_ = wx;
        dragCurWY_ = wy;
        hasDstSnap_ = false;
        if (!shift) {
          for (auto &g : circuit.gates)
            g.selected = false;
          for (auto &inst : circuit.subcircuitInsts)
            inst.selected = false;
        }
        for (auto &w : circuit.wires)
          w.selected = false;
        if (onStatusMessage)
          onStatusMessage("Drag to an input pin  ·  Esc to cancel");
        redraw();
        return;
      }
    }

    // Wire select (unchanged except don't clear if shift)
    if (!shift)
      for (auto &w : circuit.wires)
        w.selected = false;
    for (auto &w : circuit.wires) {
      auto [wsx0, wsy0, wsx1, wsy1] = wireSC(w);
      float msx = wireSplitSX(w, wsx0, wsx1);
      if (orthoHit(wsx0, wsy0, wsx1, wsy1, msx, sx, sy, 8.f)) {
        w.selected = true;
        if (!shift) {
          for (auto &g : circuit.gates)
            g.selected = false;
          for (auto &inst : circuit.subcircuitInsts)
            inst.selected = false;
        }
        if (onStatusMessage)
          onStatusMessage("Wire selected  ·  Del to remove");
        redraw();
        return;
      }
    }

    // Hit-test gate / subcircuit instance
    int hitInst = hitSubInst(wx, wy);
    int hitG = hitGate(wx, wy);

    if (hitInst >= 0) {
      SubcircuitInst *inst = circuit.findInst(hitInst);
      if (inst) {
        if (shift) {
          inst->selected = !inst->selected;
          redraw();
          return;
        }
        if (inst->selected) {
          // Already selected → begin multi-drag
          beginMultiDrag(wx, wy);
          return;
        }
        // Single select + single drag
        for (auto &g : circuit.gates)
          g.selected = false;
        for (auto &i2 : circuit.subcircuitInsts)
          i2.selected = false;
        inst->selected = true;
        dragInstId_ = hitInst;
        dragOx_ = wx - inst->x;
        dragOy_ = wy - inst->y;
      }
    } else if (hitG >= 0) {
      Gate *g = circuit.find(hitG);
      if (g) {
        if (shift) {
          g->selected = !g->selected;
          redraw();
          return;
        }
        if (g->selected) {
          // Already selected → begin multi-drag
          beginMultiDrag(wx, wy);
          return;
        }
        // Single select + single drag
        for (auto &g2 : circuit.gates)
          g2.selected = false;
        for (auto &inst : circuit.subcircuitInsts)
          inst.selected = false;
        g->selected = true;
        dragId_ = hitG;
        dragOx_ = wx - g->x;
        dragOy_ = wy - g->y;
      }
    } else {
      // Clicked empty space
      if (!shift) {
        for (auto &g : circuit.gates)
          g.selected = false;
        for (auto &inst : circuit.subcircuitInsts)
          inst.selected = false;
      }
      // Start rubber-band
      dragSelect_ = true;
      dragSelX0_ = dragSelX1_ = wx;
      dragSelY0_ = dragSelY1_ = wy;
      dragSelAdditive_ = shift;
    }
    redraw();
  }

  void onMouseMove(float sx, float sy) override {
    if (ctxMenu_.open) {
      float mx = sx, my = sy;
      if (hwnd_) {
        POINT pt;
        GetCursorPos(&pt);
        ScreenToClient(hwnd_, &pt);
        mx = float(pt.x);
        my = float(pt.y);
      }
      int item = ctxMenuHitItem(mx, my);
      if (item != ctxMenu_.hoveredItem) {
        ctxMenu_.hoveredItem = item;
        ctxMenuTexDirty_ = true;
        redraw();
      }
      return;
    }
    float wx, wy;
    s2w(sx, sy, wx, wy);
    if (mmb_) {
      camX_ -= (sx - lastSX_) / zoom_;
      camY_ -= (sy - lastSY_) / zoom_;
      lastSX_ = sx;
      lastSY_ = sy;
      redraw();
      return;
    }
    lastSX_ = sx;
    lastSY_ = sy;
    if (isSimActive())
      return;

    if (dragSplit_) {
      Wire *w = circuit.find_wire(dragSplitId_);
      if (w) {
        float nw = wx - dragSplitOff_;
        if (snapEnabled)
          nw = snapGrid(nw);
        w->splitX = nw;
        redraw();
      }
      return;
    }
    if (dragWire_) {
      dragCurWX_ = wx;
      dragCurWY_ = wy;
      auto pins = collectPins(circuit);
      PinRef dst{};
      hasDstSnap_ = nearestPin(pins, wx, wy, 20.f / zoom_, false, true, dst);
      if (hasDstSnap_ && dst.gateId == dragSrc_.gateId)
        hasDstSnap_ = false;
      if (hasDstSnap_)
        dstSnap_ = dst;
      redraw();
      return;
    }

    // Rubber-band drag select
    if (dragSelect_) {
      dragSelX1_ = wx;
      dragSelY1_ = wy;
      redraw();
      return;
    }

    // Multi-drag
    if (dragMulti_) {
      float rawDX = wx - dragMultiStartWX_;
      float rawDY = wy - dragMultiStartWY_;
      // Snap the delta using the first origin as anchor
      float snappedDX = rawDX, snappedDY = rawDY;
      if (snapEnabled && !dragMultiOrigins_.empty()) {
        snappedDX =
            snapGrid(dragMultiOrigins_[0].ox + rawDX) - dragMultiOrigins_[0].ox;
        snappedDY =
            snapGrid(dragMultiOrigins_[0].oy + rawDY) - dragMultiOrigins_[0].oy;
      }
      for (auto &orig : dragMultiOrigins_) {
        if (orig.isInst) {
          SubcircuitInst *inst = circuit.findInst(orig.id);
          if (inst) {
            inst->x = orig.ox + snappedDX;
            inst->y = orig.oy + snappedDY;
            auto it = circuit.subcircuitDefs.find(inst->defId);
            if (it != circuit.subcircuitDefs.end())
              inst->recomputePins(it->second);
          }
        } else {
          Gate *g = circuit.find(orig.id);
          if (g) {
            g->x = orig.ox + snappedDX;
            g->y = orig.oy + snappedDY;
            g->recomputePins();
          }
        }
      }
      redraw();
      return;
    }

    // Pin hover feedback (unchanged)
    {
      auto pins = collectPins(circuit);
      PinRef h{};
      bool found = nearestPin(pins, wx, wy, 16.f / zoom_, false, false, h);
      bool changed = (found != hovPinValid_);
      if (!changed && found)
        changed = (h.gateId != hovPin_.gateId || h.pinIdx != hovPin_.pinIdx ||
                   h.isOutput != hovPin_.isOutput);
      hovPinValid_ = found;
      if (found)
        hovPin_ = h;
      if (changed)
        redraw();
    }
    if (dragInstId_ >= 0) {
      SubcircuitInst *inst = circuit.findInst(dragInstId_);
      if (inst) {
        float nx = wx - dragOx_, ny = wy - dragOy_;
        if (snapEnabled) {
          nx = snapGrid(nx);
          ny = snapGrid(ny);
        }
        inst->x = nx;
        inst->y = ny;
        auto it = circuit.subcircuitDefs.find(inst->defId);
        if (it != circuit.subcircuitDefs.end())
          inst->recomputePins(it->second);
        redraw();
      }
      return;
    }
    if (dragId_ >= 0) {
      Gate *g = circuit.find(dragId_);
      if (g) {
        float nx = wx - dragOx_, ny = wy - dragOy_;
        if (snapEnabled) {
          nx = snapGrid(nx);
          ny = snapGrid(ny);
        }
        g->x = nx;
        g->y = ny;
        g->recomputePins();
        redraw();
      }
    }
  }

  void onMouseUp(float sx, float sy) override {
    mmb_ = false;
    if (isSimActive())
      return;
    if (dragSplit_) {
      dragSplit_ = false;
      if (dragSplitId_ >= 0) {
        pushUndo();
        if (onCircuitChanged)
          onCircuitChanged();
      }
      dragSplitId_ = -1;
      return;
    }
    if (dragWire_) {
      dragWire_ = false;
      if (hasDstSnap_) {
        bool occ = false;
        for (auto &w : circuit.wires)
          if (w.dst.gateId == dstSnap_.gateId &&
              w.dst.pinIdx == dstSnap_.pinIdx) {
            occ = true;
            break;
          }
        if (!occ) {

          std::string err;
          int wid = circuit.connect(dragSrc_.gateId, dragSrc_.pinIdx,
                                    dstSnap_.gateId, dstSnap_.pinIdx, &err);
          if (wid < 0) {
            if (onStatusMessage)
              onStatusMessage(("⚠ " + err).c_str());
          } else {
            pushUndo();
            circuit.evaluate();
            if (onCircuitChanged)
              onCircuitChanged();
            if (onStatusMessage)
              onStatusMessage("Connected");
          }

        } else if (onStatusMessage)
          onStatusMessage(
              "Input already connected — remove existing wire first");
      }
      hasDstSnap_ = false;
      redraw();
      return;
    }
    // Rubber-band finalise
    if (dragSelect_) {
      dragSelect_ = false;
      float minX = min(dragSelX0_, dragSelX1_),
            maxX = max(dragSelX0_, dragSelX1_);
      float minY = min(dragSelY0_, dragSelY1_),
            maxY = max(dragSelY0_, dragSelY1_);
      // Only do selection if the band has some area (avoids accidental clear on
      // tiny drags)
      if (maxX - minX > 4.f / zoom_ || maxY - minY > 4.f / zoom_) {
        for (auto &g : circuit.gates) {
          float gh = gateH(g.type);
          bool inside =
              g.x >= minX && g.x <= maxX && g.y >= minY && g.y <= maxY;
          if (dragSelAdditive_)
            g.selected = g.selected || inside;
          else
            g.selected = inside;
        }
        for (auto &inst : circuit.subcircuitInsts) {
          bool inside = inst.x >= minX && inst.x <= maxX && inst.y >= minY &&
                        inst.y <= maxY;
          if (dragSelAdditive_)
            inst.selected = inst.selected || inside;
          else
            inst.selected = inside;
        }
        // Count selection for status
        int n = 0;
        for (auto &g : circuit.gates)
          if (g.selected)
            n++;
        for (auto &inst : circuit.subcircuitInsts)
          if (inst.selected)
            n++;
        if (n > 0 && onStatusMessage) {
          char buf[64];
          snprintf(buf, sizeof(buf), "%d item%s selected  ·  Ctrl+G to group",
                   n, n == 1 ? "" : "s");
          onStatusMessage(buf);
        }
      }
      redraw();
      return;
    }
    // Multi-drag commit
    if (dragMulti_) {
      dragMulti_ = false;
      dragMultiOrigins_.clear();
      pushUndo();
      if (onCircuitChanged)
        onCircuitChanged();
      return;
    }
    if (dragInstId_ >= 0) {
      pushUndo();
      dragInstId_ = -1;
      if (onCircuitChanged)
        onCircuitChanged();
      return;
    }
    if (dragId_ >= 0) {
      pushUndo();
      dragId_ = -1;
      if (onCircuitChanged)
        onCircuitChanged();
    }
  }

  void onRightMouseDown(float sx, float sy) override {
    if (ctxMenu_.open) {
      ctxMenuClose();
      redraw();
      return;
    }
    if (isSimActive())
      return;
    float wx, wy;
    s2w(sx, sy, wx, wy);
    int hit = hitGate(wx, wy);
    if (hit >= 0) {
      for (auto &g : circuit.gates)
        g.selected = (g.id == hit);
      for (auto &inst : circuit.subcircuitInsts)
        inst.selected = false;
      float mx = sx, my = sy;
      if (hwnd_) {
        POINT pt;
        GetCursorPos(&pt);
        ScreenToClient(hwnd_, &pt);
        mx = float(pt.x);
        my = float(pt.y);
      }
      ctxMenuOpen(hit, mx, my);
      redraw();
      return;
    }
    // Right-click subcircuit instance → delete it
    int hitInst = hitSubInst(wx, wy);
    if (hitInst >= 0) {
      pushUndo();
      circuit.removeInstWires(hitInst);
      circuit.removeInst(hitInst);
      circuit.evaluate();
      if (onCircuitChanged)
        onCircuitChanged();
      if (onStatusMessage)
        onStatusMessage("Subcircuit instance deleted");
      redraw();
      return;
    }
    for (int i = int(circuit.wires.size()) - 1; i >= 0; --i) {
      auto [wsx0, wsy0, wsx1, wsy1] = wireSC(circuit.wires[i]);
      float msx = wireSplitSX(circuit.wires[i], wsx0, wsx1);
      if (orthoHit(wsx0, wsy0, wsx1, wsy1, msx, sx, sy, 10.f)) {
        pushUndo();
        circuit.disconnect(circuit.wires[i].id);
        circuit.evaluate();
        if (onCircuitChanged)
          onCircuitChanged();
        if (onStatusMessage)
          onStatusMessage("Wire removed");
        redraw();
        return;
      }
    }
  }

  void onDoubleClick(float sx, float sy) {
    float wx, wy;
    s2w(sx, sy, wx, wy);
    if (isSimActive()) {
      int hit = hitGate(wx, wy);
      if (hit >= 0) {
        Gate *g = circuit.find(hit);
        if (g && g->type == GateType::INPUT) {
          g->inputVal = !g->inputVal;
          circuit.evaluate();
          if (onSimUpdated)
            onSimUpdated();
          redraw();
        } else if (g && g->type == GateType::CLOCK)
          cycleClockFreq(hit);
      }
      return;
    }
    int hitInst = hitSubInst(wx, wy);
    if (hitInst >= 0) {
      // Future: open inner editor.  For now show pin summary.
      SubcircuitInst *inst = circuit.findInst(hitInst);
      auto it = circuit.subcircuitDefs.find(inst ? inst->defId : -1);
      if (inst && it != circuit.subcircuitDefs.end()) {
        std::string msg = "[" + it->second.name + "]  ";
        msg += std::to_string(inst->inputPins.size()) + " in / " +
               std::to_string(inst->outputPins.size()) + " out";
        if (onStatusMessage)
          onStatusMessage(msg.c_str());
      }
      return;
    }
    int hit = hitGate(wx, wy);
    if (hit >= 0) {
      Gate *g = circuit.find(hit);
      if (g && g->type == GateType::CLOCK) {
        cycleClockFreq(hit);
        return;
      }
      if (g && onLabelEdit)
        onLabelEdit(hit, g->label);
      return;
    }
    for (int i = int(circuit.wires.size()) - 1; i >= 0; --i) {
      auto [wsx0, wsy0, wsx1, wsy1] = wireSC(circuit.wires[i]);
      float msx = wireSplitSX(circuit.wires[i], wsx0, wsx1);
      if (orthoHit(wsx0, wsy0, wsx1, wsy1, msx, sx, sy, 10.f)) {
        pushUndo();
        circuit.disconnect(circuit.wires[i].id);
        circuit.evaluate();
        if (onCircuitChanged)
          onCircuitChanged();
        if (onStatusMessage)
          onStatusMessage("Wire removed");
        redraw();
        return;
      }
    }
  }

  void hookWindow(HWND hwnd) {
    if (hwnd_)
      return;
    hwnd_ = hwnd;
    sMap()[hwnd] = this;
    origProc_ =
        (WNDPROC)SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)WndProcHook);
  }

private:
  HWND hwnd_ = nullptr;
  WNDPROC origProc_ = nullptr;
  static std::unordered_map<HWND, CircuitSurface *> &sMap() {
    static std::unordered_map<HWND, CircuitSurface *> m;
    return m;
  }

  static LRESULT CALLBACK WndProcHook(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    auto it = sMap().find(h);
    if (it == sMap().end())
      return DefWindowProcW(h, msg, wp, lp);
    CircuitSurface *s = it->second;
    WNDPROC orig = s->origProc_;
    switch (msg) {
    case WM_MBUTTONDOWN: {
      SetCapture(h);
      s->mmb_ = true;
      s->lastSX_ = float(GET_X_LPARAM(lp));
      s->lastSY_ = float(GET_Y_LPARAM(lp));
      return 0;
    }
    case WM_MBUTTONUP: {
      ReleaseCapture();
      s->mmb_ = false;
      return 0;
    }
    case WM_MOUSEWHEEL: {
      POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
      ScreenToClient(h, &pt);
      float cx = float(pt.x), cy = float(pt.y),
            dy = float((short)HIWORD(wp)) / float(WHEEL_DELTA);
      float wx0, wy0;
      s->s2w(cx, cy, wx0, wy0);
      float f = (dy > 0) ? 1.12f : 1.f / 1.12f;
      s->zoom_ = std::clamp(s->zoom_ * f, 0.05f, 8.f);
      s->camX_ = wx0 - cx / s->zoom_;
      s->camY_ = wy0 - cy / s->zoom_;
      if (s->onZoomChanged)
        s->onZoomChanged(s->zoom_);
      s->redraw();
      return 0;
    }
    case WM_TIMER: {
      UINT_PTR tid = (UINT_PTR)wp;
      if (tid >= kClockTimerBase && s->simMode == SimMode::Running) {
        int gateId = (int)(tid - kClockTimerBase);
        Gate *g = s->circuit.find(gateId);
        if (g && g->type == GateType::CLOCK) {
          g->inputVal = !g->inputVal;
          s->circuit.evaluate();
          if (s->onSimUpdated)
            s->onSimUpdated();
          if (s->onCircuitChanged)
            s->onCircuitChanged();
          s->redraw();
        }
        return 0;
      }
      break;
    }
    case WM_NCDESTROY: {
      s->stopClockTimers();
      sMap().erase(h);
      SetWindowLongPtrW(h, GWLP_WNDPROC, (LONG_PTR)orig);
      return CallWindowProcW(orig, h, msg, wp, lp);
    }
    }
    return CallWindowProcW(orig, h, msg, wp, lp);
  }

public:
  void onKeyDown(int key) override {
    if (key == VK_ESCAPE && ctxMenu_.open) {
      ctxMenuClose();
      redraw();
      return;
    }
    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    if (ctrl && key == 'Z') {
      undo();
      return;
    }
    if (ctrl && key == 'Y') {
      redo();
      return;
    }
    if (ctrl && key == 'S') {
      if (onSaveRequest)
        onSaveRequest();
      return;
    }
    if (ctrl && key == 'O') {
      if (onLoadRequest)
        onLoadRequest();
      return;
    }
    // Ctrl+G = group selected
    if (ctrl && key == 'G') {
      groupSelected();
      return;
    }
    if (key == 'G' && !ctrl) {
      snapEnabled = !snapEnabled;
      if (onStatusMessage)
        onStatusMessage(snapEnabled ? "⊞ Snap to grid ON"
                                    : "⊟ Snap to grid OFF");
      redraw();
      return;
    }

    // Ctrl+A = select all
    if (ctrl && key == 'A') {
      for (auto &g : circuit.gates)
        g.selected = true;
      for (auto &inst : circuit.subcircuitInsts)
        inst.selected = true;
      int n = int(circuit.gates.size()) + int(circuit.subcircuitInsts.size());
      if (n > 0 && onStatusMessage) {
        char buf[64];
        snprintf(buf, sizeof(buf), "All %d items selected  ·  Ctrl+G to group",
                 n);
        onStatusMessage(buf);
      }
      redraw();
      return;
    }
    if (key == VK_ESCAPE && dragWire_) {
      dragWire_ = false;
      hasDstSnap_ = false;
      redraw();
      return;
    }
    if (key == VK_SPACE) {
      if (isSimActive()) {
        simPause();
        return;
      }
      for (auto &g : circuit.gates)
        if (g.selected && g.type == GateType::INPUT) {
          g.inputVal = !g.inputVal;
          circuit.evaluate();
          if (onCircuitChanged)
            onCircuitChanged();
          redraw();
        }
    }
    if (!isSimActive() && (key == VK_DELETE || key == VK_BACK)) {
      bool anyW = false;
      for (int i = int(circuit.wires.size()) - 1; i >= 0; --i)
        if (circuit.wires[i].selected) {
          circuit.disconnect(circuit.wires[i].id);
          anyW = true;
        }
      if (anyW) {
        pushUndo();
        circuit.evaluate();
        if (onCircuitChanged)
          onCircuitChanged();
        redraw();
        return;
      }
      bool anyG = false;
      for (int i = int(circuit.gates.size()) - 1; i >= 0; --i)
        if (circuit.gates[i].selected) {
          if (circuit.gates[i].type == GateType::CLOCK && hwnd_)
            KillTimer(hwnd_, kClockTimerBase + (UINT_PTR)circuit.gates[i].id);
          circuit.removeGateWires(circuit.gates[i].id);
          circuit.remove(circuit.gates[i].id);
          anyG = true;
        }
      bool anyI = false;
      for (int i = int(circuit.subcircuitInsts.size()) - 1; i >= 0; --i)
        if (circuit.subcircuitInsts[i].selected) {
          circuit.removeInstWires(circuit.subcircuitInsts[i].id);
          circuit.removeInst(circuit.subcircuitInsts[i].id);
          anyI = true;
        }
      if (anyG || anyI) {
        pushUndo();
        circuit.evaluate();
        if (onCircuitChanged)
          onCircuitChanged();
        redraw();
      }
    }
  }

private:
  static constexpr int kMaxUndo = 50;
  std::vector<std::string> undoStack_;
  int undoPos_ = 0;
  int viewW_ = 1, viewH_ = 1;
  float camX_ = 0, camY_ = 0, zoom_ = 1.f;
  GLuint prog_ = 0, vao_ = 0, vbo_ = 0;
  GLint uViewSize_ = -1, uColor_ = -1;
  GLuint texProg_ = 0, texVao_ = 0, texVbo_ = 0;
  GLint uTexViewSize_ = -1, uTexSampler_ = -1;
  GLuint ctxMenuTex_ = 0;
  int ctxMenuTexW_ = 0, ctxMenuTexH_ = 0;
  bool ctxMenuTexDirty_ = true;
  int dragId_ = -1, dragInstId_ = -1;
  float dragOx_ = 0, dragOy_ = 0, lastSX_ = 0, lastSY_ = 0;
  bool mmb_ = false, dragWire_ = false;
  PinRef dragSrc_{}, dstSnap_{};
  float dragCurWX_ = 0, dragCurWY_ = 0;
  bool hasDstSnap_ = false, hovPinValid_ = false;
  PinRef hovPin_{};
  bool dragSplit_ = false;
  int dragSplitId_ = -1;
  float dragSplitOff_ = 0.f;

  // ── Rubber-band drag-select ───────────────────────────────────────────────
  bool dragSelect_ = false;
  float dragSelX0_ = 0, dragSelY0_ = 0; // world-space anchor corner
  float dragSelX1_ = 0, dragSelY1_ = 0; // world-space live corner
  bool dragSelAdditive_ = false;        // shift was held at drag start

  // ── Multi-drag ────────────────────────────────────────────────────────────
  bool dragMulti_ = false;
  float dragMultiStartWX_ = 0, dragMultiStartWY_ = 0; // world pos at drag start
  struct ItemOrigin {
    bool isInst;
    int id;
    float ox, oy;
  };
  std::vector<ItemOrigin> dragMultiOrigins_; // start positions of all selected

  struct CtxMenuState {
    bool open = false;
    int gateId = -1;
    float sx = 0, sy = 0;
    int hoveredItem = -1;
  } ctxMenu_;

  void beginMultiDrag(float wx, float wy) {
    dragMulti_ = true;
    dragMultiStartWX_ = wx;
    dragMultiStartWY_ = wy;
    dragMultiOrigins_.clear();
    for (auto &g : circuit.gates)
      if (g.selected)
        dragMultiOrigins_.push_back({false, g.id, g.x, g.y});
    for (auto &inst : circuit.subcircuitInsts)
      if (inst.selected)
        dragMultiOrigins_.push_back({true, inst.id, inst.x, inst.y});
  }

  // ── Hit testing ───────────────────────────────────────────────────────
  int hitGate(float wx, float wy) const {
    for (int i = int(circuit.gates.size()) - 1; i >= 0; --i) {
      const Gate &g = circuit.gates[i];
      float gh = gateH(g.type);
      if (wx >= g.x - kGW * .5f && wx <= g.x + kGW * .5f &&
          wy >= g.y - gh * .5f && wy <= g.y + gh * .5f)
        return g.id;
    }
    return -1;
  }
  int hitSubInst(float wx, float wy) const {
    for (int i = int(circuit.subcircuitInsts.size()) - 1; i >= 0; --i) {
      const SubcircuitInst &inst = circuit.subcircuitInsts[i];
      auto it = circuit.subcircuitDefs.find(inst.defId);
      int ni = it != circuit.subcircuitDefs.end()
                   ? (int)it->second.inputPins.size()
                   : 1;
      int no = it != circuit.subcircuitDefs.end()
                   ? (int)it->second.outputPins.size()
                   : 1;
      float h = subcircuitH(ni, no);
      if (wx >= inst.x - kSCWidth * .5f && wx <= inst.x + kSCWidth * .5f &&
          wy >= inst.y - h * .5f && wy <= inst.y + h * .5f)
        return inst.id;
    }
    return -1;
  }

  // ── Context menu (unchanged internals) ───────────────────────────────
  float ctxRowY(int i) const {
    return ctxMenu_.sy + kCMPadV + float(i) * kCMRowStride;
  }
  void ctxMenuOpen(int gateId, float sx, float sy) {
    ctxMenu_.gateId = gateId;
    ctxMenu_.hoveredItem = -1;
    ctxMenu_.open = true;
    float mx = sx + 4.f, my = sy + 4.f;
    if (mx + kCMWidth > float(viewW_))
      mx = sx - kCMWidth - 4.f;
    if (my + kCMHeight > float(viewH_))
      my = sy - kCMHeight - 4.f;
    ctxMenu_.sx = mx;
    ctxMenu_.sy = my;
    ctxMenuTexDirty_ = true;
  }
  void ctxMenuClose() {
    ctxMenu_.open = false;
    ctxMenu_.gateId = -1;
    ctxMenu_.hoveredItem = -1;
    destroyCtxMenuTex();
  }
  int ctxMenuHitItem(float sx, float sy) const {
    if (!ctxMenu_.open)
      return -1;
    float mx = ctxMenu_.sx, my = ctxMenu_.sy;
    if (sx < mx || sx > mx + kCMWidth || sy < my || sy > my + kCMHeight)
      return -1;
    for (int i = 0; i < kGateMenuItemCount; ++i) {
      float iy = ctxRowY(i),
            hitH = (i < kGateMenuItemCount - 1) ? kCMRowStride : kCMItemH;
      if (sy >= iy && sy < iy + hitH)
        return i;
    }
    return -1;
  }
  void destroyCtxMenuTex() {
    if (ctxMenuTex_) {
      glDeleteTextures(1, &ctxMenuTex_);
      ctxMenuTex_ = 0;
    }
    ctxMenuTexW_ = 0;
    ctxMenuTexH_ = 0;
    ctxMenuTexDirty_ = true;
  }

  void rebuildCtxMenuTexture() {
    ctxMenuTexDirty_ = false;
    int texW = int(kCMWidth), texH = int(kCMHeight);
    if (!ctxMenuTex_ || ctxMenuTexW_ != texW || ctxMenuTexH_ != texH) {
      if (ctxMenuTex_)
        glDeleteTextures(1, &ctxMenuTex_);
      glGenTextures(1, &ctxMenuTex_);
      glBindTexture(GL_TEXTURE_2D, ctxMenuTex_);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, 0x812F);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, 0x812F);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texW, texH, 0, GL_RGBA,
                   GL_UNSIGNED_BYTE, nullptr);
      glBindTexture(GL_TEXTURE_2D, 0);
      ctxMenuTexW_ = texW;
      ctxMenuTexH_ = texH;
    }
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = texW;
    bmi.bmiHeader.biHeight = -texH;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void *bits = nullptr;
    HDC hdcMem = CreateCompatibleDC(nullptr);
    HBITMAP hBmp =
        CreateDIBSection(hdcMem, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!hBmp) {
      DeleteDC(hdcMem);
      return;
    }
    HGDIOBJ oldBmp = SelectObject(hdcMem, hBmp);
    {
      RECT rc = {0, 0, texW, texH};
      HBRUSH br = CreateSolidBrush(RGB(255, 0, 255));
      FillRect(hdcMem, &rc, br);
      DeleteObject(br);
    }
    HFONT hFont =
        CreateFontA(13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                    CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
    HGDIOBJ oldFont = SelectObject(hdcMem, hFont);
    SetBkMode(hdcMem, TRANSPARENT);
    for (int i = 0; i < kGateMenuItemCount; ++i) {
      float iy = ctxRowY(i) - ctxMenu_.sy;
      bool hov = (i == ctxMenu_.hoveredItem);
      COLORREF col = (kGateMenuItems[i].action == GateMenuAction::Delete)
                         ? (hov ? RGB(255, 145, 145) : RGB(200, 110, 110))
                         : (hov ? RGB(230, 215, 255) : RGB(175, 168, 205));
      SetTextColor(hdcMem, col);
      RECT r;
      r.left = (LONG)kCMTextOff;
      r.top = (LONG)iy;
      r.right = (LONG)(kCMWidth - 8.f);
      r.bottom = (LONG)(iy + kCMItemH);
      DrawTextA(hdcMem, kGateMenuItems[i].label, -1, &r,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    GdiFlush();
    const uint8_t *dibBits = reinterpret_cast<const uint8_t *>(bits);
    std::vector<uint8_t> rgba(size_t(texW) * texH * 4);
    for (int row = 0; row < texH; ++row) {
      int glRow = texH - 1 - row;
      for (int col = 0; col < texW; ++col) {
        const uint8_t *p = dibBits + (row * texW + col) * 4;
        uint8_t b = p[0], g = p[1], r = p[2];
        bool isSentinel = (r >= 250 && g < 10 && b >= 250);
        uint8_t *d = rgba.data() + (glRow * texW + col) * 4;
        d[0] = r;
        d[1] = g;
        d[2] = b;
        d[3] = isSentinel ? 0 : 255;
      }
    }
    glBindTexture(GL_TEXTURE_2D, ctxMenuTex_);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, texW, texH, GL_RGBA,
                    GL_UNSIGNED_BYTE, rgba.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    SelectObject(hdcMem, oldFont);
    DeleteObject(hFont);
    SelectObject(hdcMem, oldBmp);
    DeleteObject(hBmp);
    DeleteDC(hdcMem);
  }

  void blitCtxMenuLabelTex() {
    if (!ctxMenuTex_)
      return;
    float x0 = ctxMenu_.sx, y0 = ctxMenu_.sy, x1 = ctxMenu_.sx + kCMWidth,
          y1 = ctxMenu_.sy + kCMHeight;
    float verts[] = {x0, y0, 0.f, 1.f, x1, y0, 1.f, 1.f, x1, y1, 1.f, 0.f,
                     x1, y1, 1.f, 0.f, x0, y1, 0.f, 0.f, x0, y0, 0.f, 1.f};
    GL.useProgram(texProg_);
    ls_gl::uniform2f(uTexViewSize_, float(viewW_), float(viewH_));
    ls_gl::uniform1i(uTexSampler_, 0);
    ls_gl::activeTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, ctxMenuTex_);
    GL.bindVertexArray(texVao_);
    GL.bindBuffer(GL_ARRAY_BUFFER, texVbo_);
    GL.bufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    GL.bindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    GL.useProgram(prog_);
    ls_gl::uniform2f(uViewSize_, float(viewW_), float(viewH_));
    GL.bindVertexArray(vao_);
  }

  void drawContextMenu() {
    if (ctxMenuTexDirty_)
      rebuildCtxMenuTexture();
    float mx = ctxMenu_.sx, my = ctxMenu_.sy, mR = mx + kCMWidth,
          mB = my + kCMHeight;
    {
      std::vector<float> v;
      pushRRFill(v, mx + 4, my + 4, mR + 4, mB + 4, kCMCorner);
      dc(v, 0, 0, 0, 0.55f);
    }
    {
      std::vector<float> v;
      pushRRFill(v, mx, my, mR, mB, kCMCorner);
      dc(v, 0.10f, 0.10f, 0.16f, 0.97f);
    }
    {
      std::vector<float> v;
      pushRR(v, mx, my, mR, mB, kCMCorner, 1.0f);
      dc(v, 0.32f, 0.30f, 0.52f, 1.0f);
    }
    for (int i = 0; i < kGateMenuItemCount; ++i) {
      float iy = ctxRowY(i), iB = iy + kCMItemH;
      if (i == ctxMenu_.hoveredItem) {
        std::vector<float> v;
        float hcr =
            (i == 0 || i == kGateMenuItemCount - 1) ? kCMCorner - 1.f : 0.f;
        pushRRFill(v, mx + 2, iy, mR - 2, iB, hcr);
        dc(v, 0.28f, 0.22f, 0.50f, 0.85f);
        std::vector<float> acc;
        pushQuad(acc, mx + 2, iy + 3, mx + 4, iB - 3);
        dc(acc, 0.68f, 0.48f, 1.0f, 1.0f);
      }
      if (i > 0) {
        float sepY = iy - kCMRowGap * 0.5f;
        std::vector<float> v;
        pushLine(v, mx + 8, sepY, mR - 8, sepY, 0.5f);
        dc(v, 0.28f, 0.28f, 0.42f, 0.6f);
      }
      float icx = mx + kCMIconOff, icy = iy + kCMItemH * 0.5f;
      bool hov = (i == ctxMenu_.hoveredItem);
      std::vector<float> iv;
      if (kGateMenuItems[i].iconType == 0) {
        pushIconTrash(iv, icx, icy, kCMIconSize, 1.1f);
        dc(iv, hov ? 0.95f : 0.72f, hov ? 0.38f : 0.30f, hov ? 0.38f : 0.30f,
           hov ? 1.f : 0.80f);
      } else {
        {
          std::vector<float> fv;
          float pW = kCMIconSize * 0.58f, pH = kCMIconSize * 0.68f,
                fL = icx - pW * 0.5f, fB = icy - pH * 0.5f;
          pushRRFill(fv, fL, fB, fL + pW, fB + pH, 2.f);
          dc(fv, hov ? 0.18f : 0.12f, hov ? 0.14f : 0.10f, hov ? 0.26f : 0.18f,
             1.f);
        }
        pushIconCopy(iv, icx, icy, kCMIconSize, 1.1f);
        dc(iv, hov ? 0.75f : 0.55f, hov ? 0.65f : 0.48f, hov ? 1.0f : 0.82f,
           hov ? 1.f : 0.80f);
      }
    }
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    blitCtxMenuLabelTex();
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  }

  void redraw() {
    if (onRedrawNeeded)
      onRedrawNeeded();
  }
  void notifyMode() {
    if (onSimModeChanged)
      onSimModeChanged(simMode);
  }
  void afterEdit(const char *msg) {
    if (onCircuitChanged)
      onCircuitChanged();
    if (onSimUpdated)
      onSimUpdated();
    if (onStatusMessage)
      onStatusMessage(msg);
    redraw();
  }
  void w2s(float wx, float wy, float &sx, float &sy) const {
    sx = (wx - camX_) * zoom_;
    sy = viewH_ - (wy - camY_) * zoom_;
  }
  void s2w(float sx, float sy, float &wx, float &wy) const {
    wx = camX_ + sx / zoom_;
    wy = camY_ + sy / zoom_;
  }

  std::tuple<float, float, float, float> wireSC(const Wire &w) const {
    float sx0 = 0, sy0 = 0, sx1 = 0, sy1 = 0;
    // Source
    if (w.srcIsSubInst()) {
      const SubcircuitInst *inst = circuit.findInst(w.srcInstId());
      if (inst && w.src.pinIdx < (int)inst->outputPins.size())
        w2s(inst->outputPins[w.src.pinIdx].cx,
            inst->outputPins[w.src.pinIdx].cy, sx0, sy0);
    } else {
      if (auto *g = circuit.find(w.src.gateId))
        if (w.src.pinIdx < (int)g->outPins.size())
          w2s(g->outPins[w.src.pinIdx].cx, g->outPins[w.src.pinIdx].cy, sx0,
              sy0);
    }
    // Dest
    if (w.dstIsSubInst()) {
      const SubcircuitInst *inst = circuit.findInst(w.dstInstId());
      if (inst && w.dst.pinIdx < (int)inst->inputPins.size())
        w2s(inst->inputPins[w.dst.pinIdx].cx, inst->inputPins[w.dst.pinIdx].cy,
            sx1, sy1);
    } else {
      if (auto *g = circuit.find(w.dst.gateId))
        if (w.dst.pinIdx < (int)g->inPins.size())
          w2s(g->inPins[w.dst.pinIdx].cx, g->inPins[w.dst.pinIdx].cy, sx1, sy1);
    }
    return {sx0, sy0, sx1, sy1};
  }
  float wireSplitSX(const Wire &w, float sx0, float sx1) const {
    return w.splitX < 1e29f ? (w.splitX - camX_) * zoom_ : (sx0 + sx1) * 0.5f;
  }

  void buildShader() {
    prog_ = glutil::linkProgram(kGVert, kGFrag);
    assert(prog_);
    uViewSize_ = GL.getUniformLocation(prog_, "uViewSize");
    uColor_ = GL.getUniformLocation(prog_, "uColor");
  }
  void buildTexShader() {
    texProg_ = glutil::linkProgram(kTexVert, kTexFrag);
    assert(texProg_);
    uTexViewSize_ = GL.getUniformLocation(texProg_, "uViewSize");
    uTexSampler_ = GL.getUniformLocation(texProg_, "uTex");
  }
  void buildVAO() {
    GL.genVertexArrays(1, &vao_);
    GL.genBuffers(1, &vbo_);
    GL.bindVertexArray(vao_);
    GL.bindBuffer(GL_ARRAY_BUFFER, vbo_);
    GL.enableVertexAttribArray(0);
    GL.vertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float),
                           (void *)0);
    GL.bindVertexArray(0);
  }
  void buildTexVAO() {
    GL.genVertexArrays(1, &texVao_);
    GL.genBuffers(1, &texVbo_);
    GL.bindVertexArray(texVao_);
    GL.bindBuffer(GL_ARRAY_BUFFER, texVbo_);
    GL.enableVertexAttribArray(0);
    GL.vertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                           (void *)0);
    GL.enableVertexAttribArray(1);
    GL.vertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                           (void *)(2 * sizeof(float)));
    GL.bufferData(GL_ARRAY_BUFFER, 6 * 4 * sizeof(float), nullptr,
                  GL_DYNAMIC_DRAW);
    GL.bindVertexArray(0);
  }
  void upload(const std::vector<float> &v) {
    GL.bindBuffer(GL_ARRAY_BUFFER, vbo_);
    GL.bufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(v.size() * sizeof(float)),
                  v.data(), GL_DYNAMIC_DRAW);
  }
  void dc(const std::vector<float> &v, float r, float g, float b,
          float a = 1.f) {
    if (v.empty())
      return;
    upload(v);
    ls_gl::uniform4f(uColor_, r, g, b, a);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(v.size() / 2));
  }

  // ── Grid (unchanged) ──────────────────────────────────────────────────
  void drawGrid() {
    float gs = 40.f, step = gs * zoom_;
    if (step < 4.f)
      return;
    float vw = float(viewW_), vh = float(viewH_);
    std::vector<float> lines;
    lines.reserve(2048);
    for (float wx = std::floor(camX_ / gs) * gs;; wx += gs) {
      float sx = (wx - camX_) * zoom_;
      if (sx > vw + step)
        break;
      pushLine(lines, sx, 0, sx, vh, 0.5f);
    }
    for (float wy = std::floor(camY_ / gs) * gs;; wy += gs) {
      float sy = vh - (wy - camY_) * zoom_;
      if (sy < -step)
        break;
      pushLine(lines, 0, sy, vw, sy, 0.5f);
    }
    dc(lines, 0.12f, 0.13f, 0.19f);
    if (snapEnabled && zoom_ > 0.35f) {
      float gsnap = kGrid, sdot = max(1.f, zoom_ * 1.1f);
      std::vector<float> dots;
      for (float wx = std::floor(camX_ / gsnap) * gsnap;
           wx < camX_ + viewW_ / zoom_; wx += gsnap) {
        float sx = (wx - camX_) * zoom_;
        for (float wy = std::floor(camY_ / gsnap) * gsnap;
             wy < camY_ + viewH_ / zoom_; wy += gsnap) {
          float sy = vh - (wy - camY_) * zoom_;
          pushCircle(dots, sx, sy, sdot, 6);
        }
      }
      dc(dots, 0.22f, 0.24f, 0.38f, 0.65f);
    }
    float ox, oy;
    w2s(0, 0, ox, oy);
    std::vector<float> cross;
    pushLine(cross, ox - 10, oy, ox + 10, oy, 1.f);
    pushLine(cross, ox, oy - 10, ox, oy + 10, 1.f);
    dc(cross, 0.22f, 0.23f, 0.36f);
  }

  // ── Wires (unchanged logic, uses updated wireSC) ──────────────────────
  void drawAllWires() {
    bool sim = isSimActive();
    float hw = max(1.0f, zoom_ * 1.4f), dotr = max(2.2f, hw * 1.0f);
    for (auto &w : circuit.wires) {
      auto [sx0, sy0, sx1, sy1] = wireSC(w);
      float msx = wireSplitSX(w, sx0, sx1);
      if (!sim && w.selected) {
        std::vector<float> halo;
        pushOrtho(halo, sx0, sy0, sx1, sy1, msx, hw + 4.f);
        dc(halo, 0.9f, 0.78f, 0.2f, 0.25f);
      }
      {
        std::vector<float> body;
        float busHW = w.isBus() ? hw * 2.8f : hw;
        pushOrtho(body, sx0, sy0, sx1, sy1, msx, busHW);
        if (w.isBus()) {
          // Color encodes how many bits are HIGH
          int hi = 0;
          for (bool b : w.busValue)
            if (b)
              hi++;
          float t =
              w.busValue.empty() ? 0.f : float(hi) / float(w.busValue.size());
          dc(body, 0.28f + t * 0.4f, 0.55f + t * 0.37f, 0.92f, 0.9f);
        } else if (w.value)
          dc(body, 0.28f, 0.92f, 0.48f);
        else if (!sim && w.selected)
          dc(body, 0.88f, 0.75f, 0.18f);
        else
          dc(body, 0.22f, 0.25f, 0.42f);
      }
      // Bus width badge drawn at midpoint
      if (w.isBus()) {
        float bmx = (sx0 + sx1) * 0.5f, bmy = (sy0 + sy1) * 0.5f;
        std::vector<float> badge;
        pushCircle(badge, bmx, bmy, max(7.f, zoom_ * 5.f), 12);
        dc(badge, 0.12f, 0.16f, 0.32f);
        // Tick marks indicating bus width
        int bits = busWidthBits(w.width);
        float tr = max(5.f, zoom_ * 3.5f);
        std::vector<float> ticks;
        for (int i = 0; i < bits; i++) {
          float a = float(i) / float(bits) * 6.2831853f;
          pushLine(ticks, bmx, bmy, bmx + cosf(a) * tr, bmy + sinf(a) * tr,
                   max(0.8f, zoom_ * 0.6f));
        }
        dc(ticks, 0.62f, 0.72f, 0.98f);
      }
      {
        std::vector<float> dots;
        pushOrthoBends(dots, sx0, sy0, sx1, sy1, msx, dotr);
        pushCircle(dots, sx0, sy0, dotr);
        pushCircle(dots, sx1, sy1, dotr);
        if (w.value)
          dc(dots, 0.28f, 0.92f, 0.48f);
        else if (!sim && w.selected)
          dc(dots, 0.88f, 0.75f, 0.18f);
        else
          dc(dots, 0.28f, 0.32f, 0.55f);
      }
      if (!sim && w.selected) {
        std::vector<float> vs;
        pushLine(vs, msx, sy0, msx, sy1, hw + 1.5f);
        dc(vs, 0.68f, 0.48f, 1.0f, 0.55f);
      }
    }
  }

  // ── Drag preview (unchanged) ──────────────────────────────────────────
  void drawDragPreview() {
    if (!dragWire_)
      return;
    float sx0, sy0;
    {
      auto *g = circuit.find(dragSrc_.gateId);
      if (dragSrc_.isSubInst) {
        auto *inst = circuit.findInst(-dragSrc_.gateId);
        if (!inst || dragSrc_.pinIdx >= (int)inst->outputPins.size())
          return;
        w2s(inst->outputPins[dragSrc_.pinIdx].cx,
            inst->outputPins[dragSrc_.pinIdx].cy, sx0, sy0);
      } else {
        if (!g || dragSrc_.pinIdx >= (int)g->outPins.size())
          return;
        w2s(g->outPins[dragSrc_.pinIdx].cx, g->outPins[dragSrc_.pinIdx].cy, sx0,
            sy0);
      }
    }
    float sx1, sy1;
    if (hasDstSnap_)
      w2s(dstSnap_.cx, dstSnap_.cy, sx1, sy1);
    else
      w2s(dragCurWX_, dragCurWY_, sx1, sy1);
    float hw = max(1.f, zoom_ * 1.3f);
    bool sn = hasDstSnap_;
    float msx = (sx0 + sx1) * 0.5f;
    {
      std::vector<float> g;
      pushOrtho(g, sx0, sy0, sx1, sy1, msx, hw + 5.f);
      dc(g, sn ? .25f : .50f, sn ? .90f : .45f, sn ? .45f : .95f, 0.18f);
    }
    {
      std::vector<float> l;
      pushOrtho(l, sx0, sy0, sx1, sy1, msx, hw);
      dc(l, sn ? .28f : .52f, sn ? 1.f : .50f, sn ? .52f : .98f, 0.85f);
    }
    {
      std::vector<float> dots;
      pushOrthoBends(dots, sx0, sy0, sx1, sy1, msx, max(3.f, hw * 1.2f));
      pushCircle(dots, sx1, sy1, max(4.f, hw * 2.f));
      dc(dots, sn ? .28f : .52f, sn ? 1.f : .50f, sn ? .52f : .98f);
    }
  }

  // ── Pin hovers (unchanged) ─────────────────────────────────────────────
  void drawPinHovers() {
    if (dragWire_ && hasDstSnap_) {
      float sx, sy;
      w2s(dstSnap_.cx, dstSnap_.cy, sx, sy);
      std::vector<float> r;
      pushCircle(r, sx, sy, max(6.f, kPinR * zoom_ * 2.f), 24);
      dc(r, 0.28f, 1.f, 0.5f, 0.45f);
    }
    if (!dragWire_ && hovPinValid_ && !isSimActive()) {
      float sx, sy;
      w2s(hovPin_.cx, hovPin_.cy, sx, sy);
      std::vector<float> r;
      pushCircle(r, sx, sy, max(6.f, kPinR * zoom_ * 2.f), 24);
      if (hovPin_.isOutput)
        dc(r, 1.f, 0.72f, 0.28f, 0.40f);
      else
        dc(r, 0.4f, 0.6f, 1.f, 0.40f);
    }
  }

  // ── Sim border (unchanged) ─────────────────────────────────────────────
  void drawSimBorder() {
    float vw = float(viewW_), vh = float(viewH_), bw = 3.f;
    std::vector<float> b;
    pushLine(b, 0, bw, vw, bw, bw);
    pushLine(b, 0, vh - bw, vw, vh - bw, bw);
    pushLine(b, bw, 0, bw, vh, bw);
    pushLine(b, vw - bw, 0, vw - bw, vh, bw);
    if (simMode == SimMode::Running)
      dc(b, 0.28f, 0.86f, 0.47f, 0.55f);
    else
      dc(b, 0.86f, 0.78f, 0.28f, 0.55f);
  }

  // =========================================================================
  // ── Subcircuit instance renderer ─────────────────────────────────────────
  // =========================================================================
  void drawSubcircuitInst(const SubcircuitInst &inst) {
    auto it = circuit.subcircuitDefs.find(inst.defId);
    const SubcircuitDef *def =
        it != circuit.subcircuitDefs.end() ? &it->second : nullptr;

    float sx, sy;
    w2s(inst.x, inst.y, sx, sy);
    float z = zoom_;
    int ni = (int)inst.inputPins.size(), no = (int)inst.outputPins.size();
    float h = subcircuitH(ni, no) * z;
    float hw = kSCWidth * z * 0.5f, hh = h * 0.5f;
    float L = sx - hw, R = sx + hw, yT = sy - hh, yB = sy + hh;
    float lw = max(1.0f, z), cr = min(8.f * z, 7.f);
    bool sim = isSimActive();

    // Accent colour for subcircuit: violet
    static constexpr float kSCBodyR = 0.10f, kSCBodyG = 0.08f, kSCBodyB = 0.22f;
    static constexpr float kSCBordR = 0.62f, kSCBordG = 0.42f, kSCBordB = 0.98f;

    // Shadow
    {
      std::vector<float> v;
      pushRRFill(v, L + 3, yT + 3, R + 3, yB + 3, cr);
      dc(v, 0, 0, 0, 0.45f);
    }
    // Body fill
    {
      std::vector<float> v;
      pushRRFill(v, L, yT, R, yB, cr);
      dc(v, kSCBodyR, kSCBodyG, kSCBodyB);
    }
    // Selection tint
    if (inst.selected) {
      std::vector<float> v;
      pushRRFill(v, L, yT, R, yB, cr);
      dc(v, 0.80f, 0.65f, 0.97f, 0.18f);
    }
    // Active glow (any output high)
    if (sim) {
      bool anyHigh = false;
      for (auto &p : inst.outputPins)
        if (p.value) {
          anyHigh = true;
          break;
        }
      if (anyHigh) {
        std::vector<float> v;
        pushRR(v, L - 1, yT - 1, R + 1, yB + 1, cr + 1, max(1.5f, z));
        dc(v, kSCBordR, kSCBordG, kSCBordB, 0.50f);
      }
    }
    // Outline
    {
      std::vector<float> v;
      pushRR(v, L, yT, R, yB, cr, lw);
      if (inst.selected)
        dc(v, 0.80f, 0.65f, 0.97f);
      else
        dc(v, kSCBordR, kSCBordG, kSCBordB);
    }
    // Selection inner ring
    if (inst.selected) {
      std::vector<float> v;
      pushRR(v, L + 2, yT + 2, R - 2, yB - 2, cr - 2, lw * 0.5f);
      dc(v, 0.80f, 0.65f, 0.97f, 0.3f);
    }

    // Centre label: module name
    // (text rendered via GDI into a tiny texture would be ideal;
    //  for now draw a horizontal rule to suggest the name region)
    {
      float midY = sy;
      std::vector<float> v;
      pushLine(v, L + cr, midY, R - cr, midY, max(0.5f, lw * 0.4f));
      dc(v, kSCBordR, kSCBordG, kSCBordB, 0.25f);
    }
    // Small triangle badge top-right to indicate it's a subcircuit
    {
      float bx = R - 5.f * z, by = yT + 5.f * z, bs = 5.f * z;
      std::vector<float> v;
      v.insert(v.end(), {bx - bs, by, bx, by, bx, by + bs});
      dc(v, kSCBordR, kSCBordG, kSCBordB, 0.6f);
    }

    // ── Input pins ───────────────────────────────────────────────────
    for (int i = 0; i < ni; i++) {
      const SubcircuitPinInfo &pin = inst.inputPins[i];
      float psx, psy;
      w2s(pin.cx, pin.cy, psx, psy);
      bool val = pin.value;
      {
        std::vector<float> v;
        pushLine(v, psx, psy, L, psy, max(0.8f, z * .7f));
        dc(v, 0.38f, 0.42f, 0.60f);
      }
      {
        std::vector<float> v;
        pushCircle(v, psx, psy, max(2.5f, kPinR * z));
        if (sim)
          dc(v, val ? .28f : .4f, val ? .86f : .4f, val ? .47f : .65f);
        else
          dc(v, pin.connected ? 1.f : 0.f, pin.connected ? .86f : .31f,
             pin.connected ? .47f : .51f);
      }
    }

    // ── Output pins ──────────────────────────────────────────────────
    for (int i = 0; i < no; i++) {
      const SubcircuitPinInfo &pin = inst.outputPins[i];
      float psx, psy;
      w2s(pin.cx, pin.cy, psx, psy);
      bool val = pin.value;
      {
        std::vector<float> v;
        pushLine(v, R, psy, psx, psy, max(0.8f, z * .7f));
        dc(v, 0.38f, 0.42f, 0.60f);
      }
      {
        std::vector<float> v;
        pushCircle(v, psx, psy, max(2.5f, kPinR * z));
        if (sim)
          dc(v, val ? .28f : .86f, val ? .86f : .51f, val ? .47f : .16f);
        else
          dc(v, pin.connected ? 1.f : .86f, pin.connected ? .86f : .51f,
             pin.connected ? .47f : .16f);
      }
    }
  }

  // =========================================================================
  // ── Gate drawing (unchanged from v12 — full copy) ────────────────────────
  // =========================================================================
  void drawGate(const Gate &g) {
    float sx, sy;
    w2s(g.x, g.y, sx, sy);
    float z = zoom_, gh = gateH(g.type) * z, hw = kGW * z * 0.5f,
          hh = gh * 0.5f;
    float L = sx - hw, R = sx + hw, yT = sy - hh, yB = sy + hh,
          lw = max(1.0f, z), cr = kCorner * z;
    bool sim = isSimActive();
    GateColors col = gateColors(g.type, g.selected);
    auto DC = [&](const std::vector<float> &v, float r, float gg, float b,
                  float a = 1.f) { dc(v, r, gg, b, a); };
    auto DCb = [&](const std::vector<float> &v, float a = 1.f) {
      DC(v, col.border[0], col.border[1], col.border[2], a);
    };
    auto DCbd = [&](const std::vector<float> &v, float a = 1.f) {
      DC(v, col.body[0], col.body[1], col.body[2], a);
    };

    if (isSequential(g.type)) {
      drawSequentialGate(g, sx, sy, L, R, yT, yB, lw, z, sim, col);
      return;
    }
    if (isBusGate(g.type)) {
      drawBusGate(g, sx, sy, L, R, yT, yB, lw, z, sim, col);
      return;
    }

    if (g.type == GateType::INPUT) {
      {
        std::vector<float> v;
        pushRRFill(v, L + 3, yT + 3, R + 3, yB + 3, cr);
        DC(v, 0, 0, 0, 0.45f);
      }
      {
        std::vector<float> v;
        pushRRFill(v, L, yT, R, yB, cr);
        DCbd(v);
      }
      {
        std::vector<float> v;
        pushRR(v, L, yT, R, yB, cr, lw * 0.5f + 0.5f);
        DCb(v);
      }
      if (g.selected) {
        std::vector<float> v;
        pushRR(v, L + 2, yT + 2, R - 2, yB - 2, cr - 2, lw * 0.5f);
        DCb(v, 0.3f);
      }
      bool on = g.inputVal;
      float cr2 = max(5.f, 8.f * z), bcx = R - cr2 - 2 * z,
            bcy = yT + cr2 + 2 * z;
      if (sim && on) {
        std::vector<float> ring;
        pushRR(ring, L - 1, yT - 1, R + 1, yB + 1, cr + 1, max(1.5f, z));
        DC(ring, 0.28f, 0.86f, 0.47f, 0.55f);
      }
      if (on) {
        std::vector<float> gv;
        pushCircle(gv, bcx, bcy, cr2 * 1.8f, 20);
        DC(gv, 0.28f, 0.86f, 0.47f, 0.20f);
      }
      {
        std::vector<float> v;
        pushCircle(v, bcx, bcy, cr2);
        DC(v, on ? .28f : .55f, on ? .86f : .18f, on ? .47f : .18f);
      }
      {
        float lw2 = hw * .3f, lh = max(1.5f, z * 1.5f);
        std::vector<float> v;
        pushQuad(v, sx - lw2, sy - lh * .5f, sx + lw2, sy + lh * .5f);
        DC(v, on ? .28f : .55f, on ? .86f : .18f, on ? .47f : .18f, 0.7f);
      }
      for (auto &pin : g.outPins) {
        float psx, psy;
        w2s(pin.cx, pin.cy, psx, psy);
        {
          std::vector<float> v;
          pushLine(v, R, psy, psx, psy, max(0.8f, z * .7f));
          DC(v, 0.38f, 0.42f, 0.60f);
        }
        {
          std::vector<float> v;
          pushCircle(v, psx, psy, max(2.5f, kPinR * z));
          DC(v, pin.connected ? 1.f : .86f, pin.connected ? .86f : .51f,
             pin.connected ? .47f : .16f);
        }
      }
      return;
    }
    if (g.type == GateType::OUTPUT) {
      {
        std::vector<float> v;
        pushRRFill(v, L + 3, yT + 3, R + 3, yB + 3, cr);
        DC(v, 0, 0, 0, 0.45f);
      }
      {
        std::vector<float> v;
        pushRRFill(v, L, yT, R, yB, cr);
        DCbd(v);
      }
      {
        std::vector<float> v;
        pushRR(v, L, yT, R, yB, cr, lw * 0.5f + 0.5f);
        DCb(v);
      }
      if (g.selected) {
        std::vector<float> v;
        pushRR(v, L + 2, yT + 2, R - 2, yB - 2, cr - 2, lw * 0.5f);
        DCb(v, 0.3f);
      }
      bool live = false;
      for (auto &w : circuit.wires)
        if (w.dst.gateId == g.id) {
          live = w.value;
          break;
        }
      float cr2 = max(4.f, 7.f * z), bcx = R - cr2 - 2 * z,
            bcy = yT + cr2 + 2 * z;
      if (live) {
        std::vector<float> ring;
        pushRR(ring, L - 1, yT - 1, R + 1, yB + 1, cr + 1, max(1.5f, z));
        DC(ring, 0.28f, 0.86f, 0.47f, 0.60f);
        std::vector<float> tint;
        pushRRFill(tint, L, yT, R, yB, cr);
        DC(tint, 0.05f, 0.32f, 0.10f, 0.22f);
        std::vector<float> gv;
        pushCircle(gv, bcx, bcy, cr2 * 2.2f, 24);
        DC(gv, 0.28f, 0.86f, 0.47f, 0.22f);
      }
      {
        std::vector<float> v;
        pushCircle(v, bcx, bcy, cr2);
        DC(v, live ? .28f : .55f, live ? .86f : .22f, live ? .47f : .10f,
           live ? 1.f : .5f);
      }
      {
        std::vector<float> v;
        pushCircle(v, bcx, bcy, cr2 * .5f);
        DC(v, live ? .5f : .4f, live ? 1.f : .28f, live ? .65f : .12f,
           live ? 1.f : .35f);
      }
      for (auto &pin : g.inPins) {
        float psx, psy;
        w2s(pin.cx, pin.cy, psx, psy);
        {
          std::vector<float> v;
          pushLine(v, L, psy, psx, psy, max(0.8f, z * .7f));
          DC(v, 0.38f, 0.42f, 0.60f);
        }
        {
          std::vector<float> v;
          pushCircle(v, psx, psy, max(2.5f, kPinR * z));
          DC(v, pin.connected ? 1.f : 0.f, pin.connected ? .86f : .31f,
             pin.connected ? .47f : .51f);
        }
      }
      return;
    }
    if (g.type == GateType::CLOCK) {
      bool on = g.inputVal;
      {
        std::vector<float> v;
        pushRRFill(v, L + 3, yT + 3, R + 3, yB + 3, cr);
        DC(v, 0, 0, 0, 0.45f);
      }
      {
        std::vector<float> v;
        pushRRFill(v, L, yT, R, yB, cr);
        DCbd(v);
      }
      {
        std::vector<float> v;
        pushRR(v, L, yT, R, yB, cr, lw * 0.5f + 0.5f);
        DCb(v);
      }
      if (g.selected) {
        std::vector<float> v;
        pushRR(v, L + 2, yT + 2, R - 2, yB - 2, cr - 2, lw * 0.5f);
        DCb(v, 0.3f);
      }
      if (sim && on) {
        std::vector<float> ring;
        pushRR(ring, L - 1, yT - 1, R + 1, yB + 1, cr + 1, max(1.5f, z));
        DC(ring, 0.18f, 0.82f, 0.90f, 0.65f);
        std::vector<float> tint;
        pushRRFill(tint, L, yT, R, yB, cr);
        DC(tint, 0.02f, 0.22f, 0.30f, 0.22f);
      }
      {
        float glyphW = (R - L) * 0.68f, glyphH = (yB - yT) * 0.38f,
              gcx = sx - (R - L) * 0.05f, gcy = (yT + yB) * 0.5f,
              strokeW = max(1.2f, z * 1.1f);
        std::vector<float> wv;
        pushSquareWave(wv, gcx, gcy, glyphW, glyphH, strokeW);
        DC(wv, on ? 0.18f : 0.30f, on ? 0.90f : 0.65f, on ? 0.95f : 0.80f,
           on ? 1.f : 0.65f);
      }
      {
        float cr2 = max(4.5f, 6.5f * z), bcx = R - cr2 - 2 * z,
              bcy = yT + cr2 + 2 * z;
        if (sim) {
          std::vector<float> gv;
          pushCircle(gv, bcx, bcy, cr2 * 1.9f, 20);
          DC(gv, 0.18f, 0.82f, 0.90f, on ? 0.22f : 0.08f);
        }
        {
          std::vector<float> v;
          pushCircle(v, bcx, bcy, cr2);
          DC(v, on ? 0.18f : 0.25f, on ? 0.82f : 0.45f, on ? 0.90f : 0.55f);
        }
        {
          std::vector<float> v;
          pushCircle(v, bcx, bcy, cr2 * 0.45f);
          DC(v, 0.7f, 0.95f, 1.f, on ? 1.f : 0.4f);
        }
      }
      for (auto &pin : g.outPins) {
        float psx, psy;
        w2s(pin.cx, pin.cy, psx, psy);
        {
          std::vector<float> v;
          pushLine(v, R, psy, psx, psy, max(0.8f, z * .7f));
          DC(v, 0.38f, 0.42f, 0.60f);
        }
        {
          std::vector<float> v;
          pushCircle(v, psx, psy, max(2.5f, kPinR * z));
          DC(v, pin.connected ? 1.f : .86f, pin.connected ? .86f : .51f,
             pin.connected ? .47f : .16f);
        }
      }
      return;
    }
    // IEEE/ANSI gates
    bool isNeg = (g.type == GateType::NAND || g.type == GateType::NOR ||
                  g.type == GateType::XNOR || g.type == GateType::NOT);
    bool isOR = (g.type == GateType::OR || g.type == GateType::NOR ||
                 g.type == GateType::XOR || g.type == GateType::XNOR);
    bool isAND = (g.type == GateType::AND || g.type == GateType::NAND);
    bool isTRI = (g.type == GateType::NOT);
    float H = yB - yT, MY = (yT + yB) * 0.5f, bubR = H * 0.115f;
    float tipX = isTRI ? (R - H * 0.20f) : R;
    float bubCX = isAND   ? (L + (R - L) * 0.44f + H * 0.5f + bubR)
                  : isTRI ? (tipX + bubR)
                          : (R + bubR);
    {
      std::vector<float> sv;
      float s = 3.f;
      if (isAND)
        pushANDFill(sv, L + s, yT + s, R + s, yB + s);
      else if (isOR)
        pushORFill(sv, L + s, yT + s, R + s, yB + s);
      else
        pushTriFill(sv, L + s, yT + s, R + s, yB + s);
      DC(sv, 0, 0, 0, 0.38f);
    }
    {
      std::vector<float> fv;
      if (isAND)
        pushANDFill(fv, L, yT, R, yB);
      else if (isOR)
        pushORFill(fv, L, yT, R, yB);
      else
        pushTriFill(fv, L, yT, R, yB);
      DCbd(fv);
    }
    if (g.selected) {
      std::vector<float> sv;
      if (isAND)
        pushANDFill(sv, L, yT, R, yB);
      else if (isOR)
        pushORFill(sv, L, yT, R, yB);
      else
        pushTriFill(sv, L, yT, R, yB);
      DC(sv, 0.80f, 0.65f, 0.97f, 0.18f);
    }
    if (sim && g.simVal) {
      std::vector<float> gv;
      float e = lw * 0.8f;
      if (isAND)
        pushANDOutline(gv, L - e, yT - e, R + e, yB + e, lw * 1.8f);
      else if (isOR) {
        pushOROutline(gv, L - e, yT - e, R + e, yB + e, lw * 1.8f);
        if (g.type == GateType::XOR || g.type == GateType::XNOR)
          pushXORBackArc(gv, L - e, yT - e, R + e, yB + e, lw * 1.8f);
      } else
        pushTRIOutline(gv, L - e, yT - e, R + e, yB + e, lw * 1.8f, isNeg);
      if (isNeg)
        pushRing(gv, bubCX, MY, bubR + e, lw * 1.8f);
      DC(gv, 0.28f, 0.86f, 0.47f, 0.38f);
    }
    {
      std::vector<float> ov;
      if (isAND)
        pushANDOutline(ov, L, yT, R, yB, lw);
      else if (isOR) {
        pushOROutline(ov, L, yT, R, yB, lw);
        if (g.type == GateType::XOR || g.type == GateType::XNOR)
          pushXORBackArc(ov, L, yT, R, yB, lw);
      } else
        pushTRIOutline(ov, L, yT, R, yB, lw, isNeg);
      DCb(ov);
    }
    if (isNeg) {
      {
        std::vector<float> v;
        pushCircle(v, bubCX, MY, bubR, 18);
        DCbd(v);
      }
      {
        std::vector<float> v;
        pushRing(v, bubCX, MY, bubR, lw);
        DCb(v);
      }
    }
    if (g.selected) {
      std::vector<float> rv;
      float i2 = lw * 0.45f;
      if (isAND)
        pushANDOutline(rv, L + 2, yT + 2, R - 2, yB - 2, i2);
      else if (isOR) {
        pushOROutline(rv, L + 2, yT + 2, R - 2, yB - 2, i2);
        if (g.type == GateType::XOR || g.type == GateType::XNOR)
          pushXORBackArc(rv, L + 2, yT + 2, R - 2, yB - 2, i2);
      } else
        pushTRIOutline(rv, L + 2, yT + 2, R - 2, yB - 2, i2, isNeg);
      DCb(rv, 0.35f);
    }
    for (auto &pin : g.inPins) {
      float psx, psy;
      w2s(pin.cx, pin.cy, psx, psy);
      float arcOff = (g.type == GateType::XOR || g.type == GateType::XNOR)
                         ? (R - L) * 0.11f
                         : 0.f;
      {
        std::vector<float> v;
        pushLine(v, psx, psy, L + arcOff, psy, max(0.8f, z * .7f));
        DC(v, 0.38f, 0.42f, 0.60f);
      }
      {
        std::vector<float> v;
        pushCircle(v, psx, psy, max(2.5f, kPinR * z));
        DC(v, pin.connected ? 1.f : 0.f, pin.connected ? .86f : .31f,
           pin.connected ? .47f : .51f);
      }
    }
    for (auto &pin : g.outPins) {
      float psx, psy;
      w2s(pin.cx, pin.cy, psx, psy);
      float stubStart = isNeg ? (bubCX + bubR) : R;
      {
        std::vector<float> v;
        pushLine(v, stubStart, psy, psx, psy, max(0.8f, z * .7f));
        DC(v, 0.38f, 0.42f, 0.60f);
      }
      {
        std::vector<float> v;
        pushCircle(v, psx, psy, max(2.5f, kPinR * z));
        DC(v, pin.connected ? 1.f : .86f, pin.connected ? .86f : .51f,
           pin.connected ? .47f : .16f);
      }
    }
  }

  void drawBusGate(const Gate &g, float sx, float sy, float L, float R,
                   float yT, float yB, float lw, float z, bool sim,
                   const GateColors &col) {
    auto DC = [&](const std::vector<float> &v, float r, float gg, float b,
                  float a = 1.f) { dc(v, r, gg, b, a); };
    auto DCb = [&](const std::vector<float> &v, float a = 1.f) {
      DC(v, col.border[0], col.border[1], col.border[2], a);
    };
    auto DCbd = [&](const std::vector<float> &v) {
      DC(v, col.body[0], col.body[1], col.body[2]);
    };
    float cr = min(8.f * z, 6.f);
    bool isIn = isBusIn(g.type);
    int bits = busWidthBits(busGateWidth(g.type));

    // Shadow
    { std::vector<float> v; pushRRFill(v, L+3, yT+3, R+3, yB+3, cr); DC(v,0,0,0,0.42f); }
    // Body
    { std::vector<float> v; pushRRFill(v, L, yT, R, yB, cr); DCbd(v); }
    // Selection tint
    if (g.selected) {
      std::vector<float> v; pushRRFill(v, L, yT, R, yB, cr);
      DC(v, 0.80f, 0.65f, 0.97f, 0.18f);
    }
    // Outline
    { std::vector<float> v; pushRR(v, L, yT, R, yB, cr, lw); DCb(v); }
    if (g.selected) {
      std::vector<float> v; pushRR(v, L+2, yT+2, R-2, yB-2, cr-2, lw*0.5f);
      DCb(v, 0.3f);
    }

    // Arrow stripe in centre showing pack ▲ or unpack ▼ direction
    {
      float ax = sx, ay = (yT + yB) * 0.5f, aw = (R-L)*0.28f, ah = (yB-yT)*0.22f;
      std::vector<float> v;
      if (isIn) {
        // right-pointing arrow = packing toward output
        v.insert(v.end(), { ax-aw, ay-ah, ax+aw, ay, ax-aw, ay+ah });
      } else {
        // left-pointing arrow = unpacking toward outputs
        v.insert(v.end(), { ax+aw, ay-ah, ax-aw, ay, ax+aw, ay+ah });
      }
      DCb(v, 0.7f);
    }

    // Bus width label: "2b" / "4b" / "8b" centred in gate
    // (drawn as tick marks on the bus-side edge)
    {
      float edgeX = isIn ? R : L;
      float tickLen = 5.f * z;
      float spacing = (yB - yT) / float(bits + 1);
      std::vector<float> ticks;
      for (int i = 0; i < bits; i++) {
        float ty = yT + spacing * float(i + 1);
        float x0 = isIn ? edgeX - tickLen : edgeX;
        float x1 = isIn ? edgeX          : edgeX + tickLen;
        pushLine(ticks, x0, ty, x1, ty, max(1.0f, z * 0.8f));
      }
      DCb(ticks, 0.55f);
    }

    // Input pins
    for (auto &pin : g.inPins) {
      float psx, psy; w2s(pin.cx, pin.cy, psx, psy);
      { std::vector<float> v; pushLine(v, psx, psy, L, psy, max(0.8f, z*.7f)); DC(v, 0.38f,0.42f,0.60f); }
      { std::vector<float> v; pushCircle(v, psx, psy, max(2.5f, kPinR*z));
        DC(v, pin.connected?1.f:0.f, pin.connected?.86f:.31f, pin.connected?.47f:.51f); }
    }
    // Output pins
    for (auto &pin : g.outPins) {
      float psx, psy; w2s(pin.cx, pin.cy, psx, psy);
      { std::vector<float> v; pushLine(v, R, psy, psx, psy, max(0.8f, z*.7f)); DC(v, 0.38f,0.42f,0.60f); }
      { std::vector<float> v; pushCircle(v, psx, psy, max(2.5f, kPinR*z));
        DC(v, pin.connected?1.f:.86f, pin.connected?.86f:.51f, pin.connected?.47f:.16f); }
    }
  }

  void drawSequentialGate(const Gate &g, float sx, float sy, float L, float R,
                          float yT, float yB, float lw, float z, bool sim,
                          const GateColors &col) {
    auto DC = [&](const std::vector<float> &v, float r, float gg, float b,
                  float a = 1.f) { dc(v, r, gg, b, a); };
    auto DCb = [&](const std::vector<float> &v, float a = 1.f) {
      DC(v, col.border[0], col.border[1], col.border[2], a);
    };
    auto DCbd = [&](const std::vector<float> &v, float a = 1.f) {
      DC(v, col.body[0], col.body[1], col.body[2], a);
    };
    float cr = min(8.f * z, 6.f);
    bool Q = g.stateVal;
    {
      std::vector<float> v;
      pushRRFill(v, L + 3, yT + 3, R + 3, yB + 3, cr);
      DC(v, 0, 0, 0, 0.42f);
    }
    {
      std::vector<float> v;
      pushRRFill(v, L, yT, R, yB, cr);
      DCbd(v);
    }
    if (g.selected) {
      std::vector<float> v;
      pushRRFill(v, L, yT, R, yB, cr);
      DC(v, 0.80f, 0.65f, 0.97f, 0.16f);
    }
    if (sim && Q) {
      std::vector<float> v;
      pushRRFill(v, L, yT, R, yB, cr);
      DC(v, col.border[0], col.border[1], col.border[2], 0.12f);
    }
    {
      std::vector<float> v;
      pushRR(v, L, yT, R, yB, cr, lw);
      DCb(v);
    }
    if (g.selected) {
      std::vector<float> v;
      pushRR(v, L + 2, yT + 2, R - 2, yB - 2, cr - 2, lw * 0.5f);
      DCb(v, 0.3f);
    }
    if (sim && Q) {
      std::vector<float> v;
      pushRR(v, L - 1, yT - 1, R + 1, yB + 1, cr + 1, max(1.5f, z));
      DC(v, col.border[0], col.border[1], col.border[2], 0.50f);
    }
    int ni = (int)g.inPins.size();
    for (int i = 0; i < ni; i++) {
      const Pin &pin = g.inPins[i];
      float psx, psy;
      w2s(pin.cx, pin.cy, psx, psy);
      const char *lbl = kInPinLabel[(int)g.type][i];
      bool isClk = (lbl && lbl[0] == 'C');
      {
        std::vector<float> v;
        pushLine(v, psx, psy, L, psy, max(0.8f, z * .7f));
        DC(v, 0.38f, 0.42f, 0.60f);
      }
      {
        std::vector<float> v;
        pushCircle(v, psx, psy, max(2.5f, kPinR * z));
        DC(v, pin.connected ? 1.f : 0.f, pin.connected ? .86f : .31f,
           pin.connected ? .47f : .51f);
      }
      if (isClk) {
        float tw = 7.f * z, th = 5.f * z;
        std::vector<float> tv;
        tv.insert(tv.end(), {L, psy - th, L + tw, psy, L, psy + th});
        DCb(tv, 0.85f);
      }
      if (!isClk && lbl && lbl[0] != 'D' && lbl[0] != 'J') {
        std::vector<float> v;
        float markX = L + min(12.f * z, (R - L) * 0.15f);
        pushLine(v, L, psy, markX, psy, max(1.5f, z));
        DC(v, col.border[0], col.border[1], col.border[2], 0.45f);
      }
    }
    float bubR2 = min(5.f * z, 4.f);
    for (int i = 0; i < (int)g.outPins.size(); i++) {
      const Pin &pin = g.outPins[i];
      float psx, psy;
      w2s(pin.cx, pin.cy, psx, psy);
      bool isQBar = (i == 1);
      bool val = isQBar ? !Q : Q;
      float stubStart = isQBar ? (R + bubR2 * 2.f + lw) : R;
      {
        std::vector<float> v;
        pushLine(v, stubStart, psy, psx, psy, max(0.8f, z * .7f));
        DC(v, 0.38f, 0.42f, 0.60f);
      }
      if (isQBar) {
        {
          std::vector<float> v;
          pushCircle(v, R + bubR2 + lw * 0.5f, psy, bubR2, 14);
          DCbd(v);
        }
        {
          std::vector<float> v;
          pushRing(v, R + bubR2 + lw * 0.5f, psy, bubR2, lw * 0.85f);
          DCb(v);
        }
      }
      {
        std::vector<float> v;
        pushCircle(v, psx, psy, max(2.5f, kPinR * z));
        if (sim)
          DC(v, val ? .28f : .55f, val ? .86f : .25f, val ? .47f : .15f);
        else
          DC(v, pin.connected ? 1.f : .86f, pin.connected ? .86f : .51f,
             pin.connected ? .47f : .16f);
      }
    }
    {
      float cr2 = max(4.5f, 6.f * z), bcx = R - cr2 - 2.5f * z,
            bcy = yT + cr2 + 2.5f * z;
      if (sim) {
        std::vector<float> gv;
        pushCircle(gv, bcx, bcy, cr2 * 1.9f, 20);
        DC(gv, col.border[0], col.border[1], col.border[2], Q ? 0.22f : 0.06f);
      }
      {
        std::vector<float> v;
        pushCircle(v, bcx, bcy, cr2);
        DC(v, Q ? col.border[0] : col.body[0] * 2.f,
           Q ? col.border[1] : col.body[1] * 2.f,
           Q ? col.border[2] : col.body[2] * 2.f);
      }
      {
        std::vector<float> v;
        pushCircle(v, bcx, bcy, cr2 * 0.45f);
        DC(v, 0.85f, 0.95f, 1.f, Q ? 1.f : 0.35f);
      }
    }
  }

  // ── Internal label dialog helper (called from groupSelected) ──────────
  static std::string dlgLabelInternal_(const char *title, const char *initial) {
    struct Ctx {
      char buf[128];
    };
    static Ctx ctx;
    strncpy_s(ctx.buf, sizeof(ctx.buf), initial, _TRUNCATE);
    struct P {
      static INT_PTR CALLBACK Proc(HWND h, UINT m, WPARAM w, LPARAM l) {
        static Ctx *c = nullptr;
        if (m == WM_INITDIALOG) {
          c = (Ctx *)l;
          HWND ed = GetDlgItem(h, 100);
          SetWindowTextA(ed, c->buf);
          SendMessageA(ed, EM_SETSEL, 0, -1);
          SetFocus(ed);
          return FALSE;
        }
        if (m == WM_COMMAND) {
          if (LOWORD(w) == IDOK) {
            GetDlgItemTextA(h, 100, c->buf, sizeof(c->buf));
            EndDialog(h, IDOK);
          } else if (LOWORD(w) == IDCANCEL)
            EndDialog(h, IDCANCEL);
        }
        return FALSE;
      }
    };
    std::vector<WORD> d;
    auto p32 = [&](DWORD v) {
      d.push_back(v & 0xFFFF);
      d.push_back(v >> 16);
    };
    auto p16 = [&](WORD v) { d.push_back(v); };
    auto pws = [&](const wchar_t *s) {
      while (*s)
        d.push_back((WORD)*s++);
      d.push_back(0);
    };
    auto al4 = [&]() {
      while (d.size() % 2)
        d.push_back(0);
    };
    p32(DS_SETFONT | DS_MODALFRAME | DS_CENTER | WS_POPUP | WS_CAPTION |
        WS_SYSMENU);
    p32(0);
    p16(3);
    p16(0);
    p16(0);
    p16(200);
    p16(64);
    p16(0);
    p16(0);
    pws(L"Subcircuit Name");
    p16(9);
    pws(L"Segoe UI");
    auto ctrl = [&](DWORD style, short x, short y, short cx, short cy, WORD id,
                    WORD cls, const wchar_t *txt) {
      al4();
      p32(WS_CHILD | WS_VISIBLE | style);
      p32(0);
      p16(x);
      p16(y);
      p16(cx);
      p16(cy);
      p16(id);
      p16(0xFFFF);
      p16(cls);
      pws(txt);
      p16(0);
    };
    ctrl(WS_BORDER | ES_AUTOHSCROLL, 7, 7, 186, 14, 100, 0x0081, L"");
    ctrl(BS_DEFPUSHBUTTON, 60, 44, 36, 14, IDOK, 0x0080, L"OK");
    ctrl(0, 102, 44, 50, 14, IDCANCEL, 0x0080, L"Cancel");
    INT_PTR r = DialogBoxIndirectParamA(
        GetModuleHandleA(nullptr), (LPCDLGTEMPLATE)d.data(),
        GetForegroundWindow(), P::Proc, (LPARAM)&ctx);
    return (r == IDOK) ? std::string(ctx.buf) : "__cancelled__";
  }
};

// =============================================================================
// §6  WIN32 FILE + LABEL DIALOGS
// =============================================================================
static std::string dlgSave(HWND hwnd) {
  char buf[MAX_PATH] = "circuit.lsim";
  OPENFILENAMEA ofn = {};
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = hwnd;
  ofn.lpstrFilter = "Logic Sim\0*.lsim\0JSON\0*.json\0All Files\0*.*\0\0";
  ofn.lpstrFile = buf;
  ofn.nMaxFile = MAX_PATH;
  ofn.lpstrDefExt = "lsim";
  ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
  return GetSaveFileNameA(&ofn) ? std::string(buf) : "";
}
static std::string dlgOpen(HWND hwnd) {
  char buf[MAX_PATH] = "";
  OPENFILENAMEA ofn = {};
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = hwnd;
  ofn.lpstrFilter = "Logic Sim\0*.lsim\0JSON\0*.json\0All Files\0*.*\0\0";
  ofn.lpstrFile = buf;
  ofn.nMaxFile = MAX_PATH;
  ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
  return GetOpenFileNameA(&ofn) ? std::string(buf) : "";
}
static std::string dlgLabel(HWND hwnd, const std::string &initial) {
  struct Ctx {
    char buf[128];
  };
  static Ctx ctx;
  strncpy_s(ctx.buf, sizeof(ctx.buf), initial.c_str(), _TRUNCATE);
  struct P {
    static INT_PTR CALLBACK Proc(HWND h, UINT m, WPARAM w, LPARAM l) {
      static Ctx *c = nullptr;
      if (m == WM_INITDIALOG) {
        c = (Ctx *)l;
        HWND ed = GetDlgItem(h, 100);
        SetWindowTextA(ed, c->buf);
        SendMessageA(ed, EM_SETSEL, 0, -1);
        SetFocus(ed);
        return FALSE;
      }
      if (m == WM_COMMAND) {
        if (LOWORD(w) == IDOK) {
          GetDlgItemTextA(h, 100, c->buf, sizeof(c->buf));
          EndDialog(h, IDOK);
        } else if (LOWORD(w) == IDCANCEL)
          EndDialog(h, IDCANCEL);
      }
      return FALSE;
    }
  };
  std::vector<WORD> d;
  auto p32 = [&](DWORD v) {
    d.push_back(v & 0xFFFF);
    d.push_back(v >> 16);
  };
  auto p16 = [&](WORD v) { d.push_back(v); };
  auto pws = [&](const wchar_t *s) {
    while (*s)
      d.push_back((WORD)*s++);
    d.push_back(0);
  };
  auto al4 = [&]() {
    while (d.size() % 2)
      d.push_back(0);
  };
  p32(DS_SETFONT | DS_MODALFRAME | DS_CENTER | WS_POPUP | WS_CAPTION |
      WS_SYSMENU);
  p32(0);
  p16(3);
  p16(0);
  p16(0);
  p16(200);
  p16(64);
  p16(0);
  p16(0);
  pws(L"Gate Label");
  p16(9);
  pws(L"Segoe UI");
  auto ctrl = [&](DWORD style, short x, short y, short cx, short cy, WORD id,
                  WORD cls, const wchar_t *txt) {
    al4();
    p32(WS_CHILD | WS_VISIBLE | style);
    p32(0);
    p16(x);
    p16(y);
    p16(cx);
    p16(cy);
    p16(id);
    p16(0xFFFF);
    p16(cls);
    pws(txt);
    p16(0);
  };
  ctrl(WS_BORDER | ES_AUTOHSCROLL, 7, 7, 186, 14, 100, 0x0081, L"");
  ctrl(BS_DEFPUSHBUTTON, 60, 44, 36, 14, IDOK, 0x0080, L"OK");
  ctrl(0, 102, 44, 50, 14, IDCANCEL, 0x0080, L"Cancel");
  INT_PTR r = DialogBoxIndirectParamA(GetModuleHandleA(nullptr),
                                      (LPCDLGTEMPLATE)d.data(), hwnd, P::Proc,
                                      (LPARAM)&ctx);
  return (r == IDOK) ? std::string(ctx.buf) : "__cancelled__";
}

// =============================================================================
// §7  APP
// =============================================================================
class LogicSimApp : public Component {
  State<int> gateCount;
  State<std::string> statusMsg;
  State<double> zoomPct;
  State<bool> simRunning;
  State<bool> simActive;
  State<std::string> ttText;
  State<bool> snapOn;
  State<std::string> currentFile;

  std::shared_ptr<CircuitSurface> surface_;
  CanvasWidget *canvas_ = nullptr;

  static constexpr int kSW = 190, kTH = 88, kHH = 26;

  void notifyZoom(float z) {
    double p = double(z) * 100.0;
    if (std::abs(p - zoomPct.get()) > 0.4)
      zoomPct.set(p);
  }
  void applyZoom(double p) {
    if (!surface_)
      return;
    surface_->setZoomLevel(float(p / 100.0));
    if (canvas_)
      canvas_->redraw();
  }

  void rebuildTruthTable() {
    if (!surface_) {
      ttText.set("No circuit");
      return;
    }
    bool hasSeq = false;
    for (auto &g : surface_->circuit.gates)
      if (isSequential(g.type)) {
        hasSeq = true;
        break;
      }
    bool hasSub = !surface_->circuit.subcircuitInsts.empty();
    if (hasSeq || hasSub) {
      std::ostringstream ss;
      if (hasSub) {
        ss << "SUBCIRCUITS\n──────────────────\n";
        for (auto &inst : surface_->circuit.subcircuitInsts) {
          auto it = surface_->circuit.subcircuitDefs.find(inst.defId);
          std::string n = it != surface_->circuit.subcircuitDefs.end()
                              ? it->second.name
                              : "?";
          std::string lbl = inst.label.empty() ? n : inst.label;
          ss << lbl << "  [" << inst.inputPins.size() << "→"
             << inst.outputPins.size() << "]\n";
          for (auto &p : inst.outputPins)
            ss << "  " << p.label << "=" << (p.value ? "1" : "0") << "\n";
        }
        ss << "──────────────────\n";
      }
      if (hasSeq) {
        ss << "SEQUENTIAL\n──────────────────\n";
        for (auto &g : surface_->circuit.gates) {
          if (!isSequential(g.type))
            continue;
          std::string lbl = g.label.empty() ? kGateName[(int)g.type] : g.label;
          ss << lbl << ": Q=" << (g.stateVal ? "1" : "0")
             << "  /Q=" << (!g.stateVal ? "1" : "0") << "\n";
        }
      }
      ttText.set(ss.str());
      return;
    }
    std::vector<int> inIds, outIds;
    auto table = surface_->circuit.buildTruthTable(inIds, outIds);
    if (table.empty() || inIds.empty() || outIds.empty()) {
      ttText.set("Connect INPUT → gates → OUTPUT\nto see truth table");
      return;
    }
    std::ostringstream ss;
    for (int i = 0; i < (int)inIds.size(); i++) {
      auto *g = surface_->circuit.find(inIds[i]);
      ss << ((g && !g->label.empty()) ? g->label
                                      : ("I" + std::to_string(i + 1)))
         << " ";
    }
    ss << "| ";
    for (int i = 0; i < (int)outIds.size(); i++) {
      auto *g = surface_->circuit.find(outIds[i]);
      ss << ((g && !g->label.empty()) ? g->label
                                      : ("O" + std::to_string(i + 1)))
         << " ";
    }
    ss << "\n";
    int w = int(inIds.size()) * 3 + 2 + int(outIds.size()) * 3;
    for (int i = 0; i < w; i++)
      ss << "-";
    ss << "\n";
    for (auto &row : table) {
      bool isCur = true;
      for (int i = 0; i < (int)row.inputs.size(); i++) {
        auto *g = surface_->circuit.find(inIds[i]);
        if (!g || g->inputVal != row.inputs[i]) {
          isCur = false;
          break;
        }
      }
      ss << (isCur ? "►" : " ");
      for (bool v : row.inputs)
        ss << " " << (v ? "1" : "0");
      ss << " |";
      for (bool v : row.outputs)
        ss << " " << (v ? "1" : "0");
      ss << "\n";
    }
    ttText.set(ss.str());
  }

  void doSave() {
    if (!surface_)
      return;
    std::string path = dlgSave(GetForegroundWindow());
    if (path.empty())
      return;
    if (surface_->saveToFile(path)) {
      currentFile.set(path);
      statusMsg.set("Saved: " + path);
    } else
      statusMsg.set("Save failed.");
  }
  void doLoad() {
    if (!surface_)
      return;
    std::string path = dlgOpen(GetForegroundWindow());
    if (path.empty())
      return;
    surface_->simStop();
    if (surface_->loadFromFile(path)) {
      currentFile.set(path);
      gateCount.set(int(surface_->circuit.gates.size()));
      rebuildTruthTable();
      surface_->fitToView();
      statusMsg.set("Loaded: " + path);
      if (canvas_)
        canvas_->redraw();
    } else
      statusMsg.set("Load failed.");
  }
  void doLabelEdit(int gateId, const std::string &current) {
    std::string r = dlgLabel(GetForegroundWindow(), current);
    if (r == "__cancelled__")
      return;
    surface_->applyLabel(gateId, r);
    rebuildTruthTable();
  }

public:
  LogicSimApp()
      : gateCount(0, context),
        statusMsg("Place gates · Connect wires · Press ▶ Play", context),
        zoomPct(100.0, context), simRunning(false, context),
        simActive(false, context), ttText("No circuit yet", context),
        snapOn(true, context), currentFile("", context) {}

  WidgetPtr build() override {
    int SW = GetSystemMetrics(SM_CXSCREEN), SH = GetSystemMetrics(SM_CYSCREEN);
    int vW = SW - kSW, vH = SH - kTH - kHH;
    auto cv = std::make_shared<CanvasWidget>()->setSize(vW, vH);
    cv->setCanvasSize(vW, vH);
    cv->setViewportEnabled(false);
    surface_ = cv->setSurface<CircuitSurface>();
    canvas_ = cv.get();

    surface_->onCircuitChanged = [this]() {
      gateCount.set(int(surface_->circuit.gates.size()) +
                    int(surface_->circuit.subcircuitInsts.size()));
    };
    surface_->onStatusMessage = [this](const char *m) { statusMsg.set(m); };
    surface_->onRedrawNeeded = [this]() {
      if (canvas_)
        canvas_->redraw();
    };
    surface_->onZoomChanged = [this](float z) { notifyZoom(z); };
    surface_->onSimModeChanged = [this](SimMode m) {
      simRunning.set(m == SimMode::Running);
      simActive.set(m != SimMode::Edit);
    };
    surface_->onSimUpdated = [this]() { rebuildTruthTable(); };
    surface_->onLabelEdit = [this](int id, const std::string &cur) {
      doLabelEdit(id, cur);
    };
    surface_->onSaveRequest = [this]() { doSave(); };
    surface_->onLoadRequest = [this]() { doLoad(); };

    zoomPct.listen([this](double p) { applyZoom(p); });
    snapOn.listen([this](bool v) {
      if (surface_)
        surface_->snapEnabled = v;
      if (canvas_)
        canvas_->redraw();
    });

    COLORREF kBg = RGB(12, 12, 18), kCard = RGB(18, 18, 28),
             kBord = RGB(40, 42, 62);
    COLORREF kAccent = RGB(174, 129, 255), kGreen = RGB(148, 226, 213),
             kDim = RGB(80, 84, 110), kText = RGB(192, 202, 232);

    struct TI {
      GateType t;
      COLORREF ac;
      const char *sub;
    };
    const std::vector<TI> tiles = {
        {GateType::AND, RGB(70, 120, 220), "A & B"},
        {GateType::OR, RGB(70, 190, 110), "A | B"},
        {GateType::NOT, RGB(170, 70, 220), "! A"},
        {GateType::NAND, RGB(220, 70, 70), "!(A&B)"},
        {GateType::NOR, RGB(200, 70, 170), "!(A|B)"},
        {GateType::XOR, RGB(70, 170, 220), "A ^ B"},
        {GateType::XNOR, RGB(110, 110, 220), "!(A^B)"},
        {GateType::INPUT, RGB(70, 210, 110), "Toggle 0/1"},
        {GateType::OUTPUT, RGB(220, 130, 40), "LED output"},
        {GateType::CLOCK, RGB(47, 210, 229), "0.5–8 Hz auto"},
        {GateType::DFF, RGB(50, 165, 240), "D·CLK·RST → Q"},
        {GateType::DLATCH, RGB(50, 215, 190), "D·EN → Q (level)"},
        {GateType::TFLIPFLOP, RGB(240, 185, 50), "T·CLK → Q (toggle)"},
        {GateType::JKFLIPFLOP, RGB(240, 95, 150), "J·K·CLK → Q"},
        {GateType::BUS_IN_2, RGB(80, 160, 240), "2×1b → 2b bus"},
        {GateType::BUS_IN_4, RGB(80, 160, 240), "4×1b → 4b bus"},
        {GateType::BUS_IN_8, RGB(80, 160, 240), "8×1b → 8b bus"},
        {GateType::BUS_OUT_2, RGB(240, 160, 80), "2b bus → 2×1b"},
        {GateType::BUS_OUT_4, RGB(240, 160, 80), "4b bus → 4×1b"},
        {GateType::BUS_OUT_8, RGB(240, 160, 80), "8b bus → 8×1b"},
    };
    auto makeTile = [&](const TI &ti) -> WidgetPtr {
      GateType t = ti.t;
      COLORREF ac = ti.ac;
      return GestureDetector(
                 Container(
                     Row(Container(nullptr)
                             ->setWidth(3)
                             ->setHeight(32)
                             ->setBackgroundColor(ac)
                             ->setBorderRadius(2),
                         SizedBox(7, 0),
                         Column(
                             Text(kGateName[(int)t])
                                 ->setFontSize(10)
                                 ->setFontWeight(FontWeight::Bold)
                                 ->setTextColor(ac),
                             SizedBox(0, 1),
                             Text(ti.sub)->setFontSize(8)->setTextColor(kDim))
                             ->setSpacing(0))
                         ->setSpacing(0)
                         ->setCrossAxisAlignment(CrossAxisAlignment::Center))
                     ->setWidth(kSW - 16)
                     ->setHeight(38)
                     ->setBackgroundColor(kCard)
                     ->setBorderRadius(6)
                     ->setBorderWidth(1)
                     ->setBorderColor(kBord)
                     ->setHoverBackgroundColor(RGB(26, 26, 42)))
          ->setOnTap([this, t]() {
            if (!surface_)
              return;
            if (surface_->isSimActive()) {
              statusMsg.set("Stop simulation first");
              return;
            }
            surface_->dropGate(t);
            std::string extra;
            if (t == GateType::DFF)
              extra = "  ·  D·CLK(rising)·RST → Q and /Q";
            else if (t == GateType::DLATCH)
              extra = "  ·  Transparent when EN=1";
            else if (t == GateType::TFLIPFLOP)
              extra = "  ·  Toggles Q on CLK rising edge when T=1";
            else if (t == GateType::JKFLIPFLOP)
              extra = "  ·  Set/Reset/Toggle on CLK rising edge";
            else if (t == GateType::CLOCK)
              extra = "  ·  Dbl-click to change Hz";
            else
              extra = "  ·  Drag out→in  ·  Dbl-click to label";
            statusMsg.set(std::string("Placed ") + kGateName[(int)t] + extra);
          });
    };
    auto tileCol = std::make_shared<ColumnWidget>();
    tileCol->setSpacing(4);
    for (auto &ti : tiles)
      tileCol->addChild(makeTile(ti));

    auto ttPanel =
        Container(Column(Text("TRUTH TABLE / STATE")
                             ->setFontSize(7)
                             ->setFontWeight(FontWeight::Bold)
                             ->setTextColor(kDim),
                         SizedBox(0, 5),
                         Text(ttText, [](const std::string &s) { return s; })
                             ->setFontSize(9)
                             ->setTextColor(RGB(120, 200, 140)))
                      ->setSpacing(0))
            ->setWidth(kSW - 8)
            ->setBackgroundColor(RGB(10, 14, 10))
            ->setBorderRadius(6)
            ->setBorderWidth(1)
            ->setBorderColor(RGB(30, 60, 35))
            ->setPaddingAll(7, 7, 7, 7);

    auto sidebar =
        Container(Column(Text("GATES")
                             ->setFontSize(7)
                             ->setFontWeight(FontWeight::Bold)
                             ->setTextColor(kDim),
                         SizedBox(0, 6), tileCol, SizedBox(0, 10), ttPanel)
                      ->setSpacing(0))
            ->setWidth(kSW)
            ->setBackgroundColor(kBg)
            ->setPaddingAll(8, 8, 8, 8);

    auto mkBtn = [&](const std::string &lbl, COLORREF col, COLORREF bg,
                     COLORREF bord, std::function<void()> fn) -> WidgetPtr {
      return GestureDetector(Container(Text(lbl)
                                           ->setFontSize(11)
                                           ->setFontWeight(FontWeight::Bold)
                                           ->setTextColor(col))
                                 ->setHeight(30)
                                 ->setBorderRadius(6)
                                 ->setBackgroundColor(bg)
                                 ->setBorderWidth(1)
                                 ->setBorderColor(bord)
                                 ->setPaddingAll(10, 4, 10, 4)
                                 ->setHoverBackgroundColor(
                                     RGB(GetRValue(bg) + 8, GetGValue(bg) + 8,
                                         GetBValue(bg) + 8)))
          ->setOnTap(fn);
    };

    auto toolbar=Container(Column(
            Row(
                Text("⚡ Logic Sim")->setFontSize(13)->setFontWeight(FontWeight::Bold)->setTextColor(kAccent),SizedBox(8,0),
                mkBtn("💾 Save",kGreen,RGB(10,20,18),RGB(30,80,60),[this](){doSave();}),
                mkBtn("📂 Open",kGreen,RGB(10,20,18),RGB(30,80,60),[this](){doLoad();}),SizedBox(4,0),
                mkBtn("↩ Undo",kAccent,RGB(22,20,34),RGB(70,55,100),[this](){if(surface_)surface_->undo();}),
                mkBtn("↪ Redo",kAccent,RGB(22,20,34),RGB(70,55,100),[this](){if(surface_)surface_->redo();}),SizedBox(4,0),
                GestureDetector(Container(Text(snapOn,[](bool v){return v?"⊞ Snap":"⊟ Snap";})
                    ->setFontSize(10)->setFontWeight(FontWeight::Bold)->setTextColor(RGB(174,129,255)))
                    ->setHeight(28)->setBorderRadius(5)->setBackgroundColor(RGB(22,18,32))
                    ->setBorderWidth(1)->setBorderColor(RGB(70,55,100))->setPaddingAll(8,3,8,3))
                ->setOnTap([this](){snapOn.set(!snapOn.get());}),SizedBox(6,0),
                Text("Zoom")->setFontSize(9)->setTextColor(kDim),
                Slider(10.0,400.0,1.0)->setValue(zoomPct)->setTrackFillColor(kAccent)->setWidth(80),
                Text(zoomPct,[](double v){char b[16];_snprintf_s(b,sizeof(b),_TRUNCATE,"%.0f%%",v);return std::string(b);})->setFontSize(10)->setTextColor(kText)->setMinWidth(34),
                mkBtn("Fit",kAccent,RGB(22,20,34),RGB(70,55,100),[this](){if(surface_){surface_->fitToView();notifyZoom(surface_->getZoom());}}),
                mkBtn("1:1",kAccent,RGB(22,20,34),RGB(70,55,100),[this](){if(surface_){surface_->resetZoom();notifyZoom(1.f);}}),SizedBox(6,0),
                mkBtn("🗑 Clear",RGB(243,139,168),RGB(34,14,20),RGB(120,40,60),[this](){if(!surface_)return;surface_->pushUndo();surface_->simStop();surface_->circuit.clearAll();gateCount.set(0);ttText.set("No circuit yet");currentFile.set("");if(canvas_)canvas_->redraw();}),SizedBox(6,0),
                Text(gateCount,[](int n){return std::to_string(n)+(n==1?" item":" items");})->setFontSize(10)->setTextColor(kDim)
            )->setSpacing(5)->setCrossAxisAlignment(CrossAxisAlignment::Center),
            SizedBox(0,4),
            Row(
                GestureDetector(Container(Text(simRunning,[](bool r){return r?"⏸ Pause":"▶ Play";})
                    ->setFontSize(11)->setFontWeight(FontWeight::Bold)->setTextColor(RGB(80,220,120)))
                    ->setHeight(30)->setBorderRadius(6)->setBackgroundColor(RGB(14,34,18))->setBorderWidth(1)->setBorderColor(RGB(40,120,55))->setPaddingAll(10,4,10,4)->setHoverBackgroundColor(RGB(18,44,22)))
                ->setOnTap([this](){if(!surface_)return;if(!surface_->isSimActive())surface_->simPlay();else surface_->simPause();}),SizedBox(5,0),
                mkBtn("■ Stop",RGB(230,80,80),RGB(34,14,14),RGB(120,40,40),[this](){if(surface_)surface_->simStop();}),SizedBox(5,0),
                mkBtn("⏭ Step",RGB(100,180,255),RGB(14,22,38),RGB(40,80,150),[this](){if(surface_)surface_->simStep();}),SizedBox(8,0),
                // ── Group button ──────────────────────────────────────────
                mkBtn("⬡ Group  Ctrl+G",RGB(200,170,255),RGB(20,14,34),RGB(80,55,130),[this](){if(surface_)surface_->groupSelected();}),
                SizedBox(12,0),
                Container(Text(simActive,[](bool a){return a?"● SIM MODE":"○ EDIT MODE";})
                    ->setFontSize(9)->setFontWeight(FontWeight::Bold)->setTextColor(RGB(220,200,60)))
                ->setPaddingAll(7,3,7,3)->setBorderRadius(4)->setBackgroundColor(RGB(20,18,8))->setBorderWidth(1)->setBorderColor(RGB(70,62,15)),
                SizedBox(12,0),
                Text("Select gates → Ctrl+G to group  |  Right-click subcircuit instance to delete  |  Dbl-click for info  |  Wires cross boundary automatically")
                    ->setFontSize(9)->setTextColor(kDim)
            )->setSpacing(0)->setCrossAxisAlignment(CrossAxisAlignment::Center)
        )->setSpacing(0))
        ->setBackgroundColor(kBg)->setPaddingAll(10,5,10,5)->setHeight(kTH);

    auto hints =
        Container(Row(Text(statusMsg, [](const std::string &s) { return s; })
                          ->setFontSize(10)
                          ->setTextColor(kGreen),
                      SizedBox(14, 0),
                      Text(currentFile,
                           [](const std::string &s) {
                             return s.empty() ? "Unsaved" : s;
                           })
                          ->setFontSize(9)
                          ->setTextColor(RGB(55, 60, 85)))
                      ->setSpacing(0))
            ->setBackgroundColor(kBg)
            ->setPaddingAll(10, 3, 10, 3)
            ->setHeight(kHH);

    return Scaffold(
        Row(sidebar, Column(toolbar, hints, cv)->setSpacing(0))->setSpacing(0));
  }
};

// =============================================================================
// §8  ENTRY POINT
// =============================================================================
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
  FluxUI app(hInst);
  app.build([&]() {
    return FluxApp("Logic Sim", BuildComponent<LogicSimApp>(),
                   AppTheme::dark());
  });
  RECT wa;
  SystemParametersInfo(SPI_GETWORKAREA, 0, &wa, 0);
  app.createWindow("FluxUI – Logic Sim", wa.right - wa.left,
                   wa.bottom - wa.top);
  return app.run();
}