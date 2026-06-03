#pragma once
// ============================================================================
// flux_circuit_surface.hpp  —  Logic-gate circuit designer
// ============================================================================

#include "flux/flux.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#include <stb_image_write.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ============================================================================
// CircuitNodeType
// ============================================================================

enum class CircuitNodeType
{
  AND = 0,
  OR,
  NOT,
  NAND,
  NOR,
  XOR,
  XNOR,
  Input,
  Output
};

// ============================================================================
// CircuitNode
// ============================================================================

struct CircuitNode
{
  int id = 0;
  CircuitNodeType type = CircuitNodeType::AND;

  float x = 0, y = 0; // world-space top-left
  float w = 0, h = 0; // world-space size

  bool selected = false;
  bool value = false; // current logic level

  // per-input driven values (size == inputCount())
  std::vector<bool> inputVals;

  std::string label;

  // drag helpers
  float dragOX = 0, dragOY = 0;

  int inputCount() const
  {
    switch (type)
    {
    case CircuitNodeType::AND:
      return 2;
    case CircuitNodeType::OR:
      return 2;
    case CircuitNodeType::NOT:
      return 1;
    case CircuitNodeType::NAND:
      return 2;
    case CircuitNodeType::NOR:
      return 2;
    case CircuitNodeType::XOR:
      return 2;
    case CircuitNodeType::XNOR:
      return 2;
    case CircuitNodeType::Input:
      return 0;
    case CircuitNodeType::Output:
      return 1;
    }
    return 0;
  }
  int outputCount() const
  {
    return (type == CircuitNodeType::Output) ? 0 : 1;
  }
};

// ============================================================================
// Wire
// ============================================================================

struct CircuitWire
{
  int fromNodeId = 0;  // source node  (output port)
  int toNodeId = 0;    // destination node (input port)
  int toPortIndex = 0; // which input on the destination
  bool selected = false;
};

// ============================================================================
// Port reference (used during interactive wiring)
// ============================================================================

struct PortRef
{
  int nodeId = -1;
  bool isOutput = false;
  int portIdx = 0;
  bool valid() const { return nodeId >= 0; }
};

// ============================================================================
// CircuitMode
// ============================================================================

enum class CircuitMode
{
  Select = 0,
  Wire,
  Delete
};

// ============================================================================
// CircuitSurface
// ============================================================================

class CircuitSurface : public RenderSurface
{
public:
  // ── Public config ─────────────────────────────────────────────────────
  float currentZoom_ = 1.f;
  float viewOffsetX_ = 0.f;
  float viewOffsetY_ = 0.f;
  float viewW_ = 1280.f;
  float viewH_ = 800.f;

  CircuitMode mode_ = CircuitMode::Select;

  // ── Callbacks ─────────────────────────────────────────────────────────
  std::function<void()> onChanged;
  std::function<void(float, float)> onCursorMoved;

  // ── Node / wire access ────────────────────────────────────────────────
  std::vector<CircuitNode> &nodes() { return nodes_; }
  std::vector<CircuitWire> &wires() { return wires_; }

  // ── Truth table ───────────────────────────────────────────────────────
  struct TruthTableResult
  {
    std::vector<std::string> inputLabels;
    std::vector<std::string> outputLabels;
    std::vector<std::vector<bool>> rows; // each row: inputs then outputs
  };

  TruthTableResult computeTruthTable()
  {
    TruthTableResult res;

    std::vector<CircuitNode *> inputs, outputs;
    for (auto &n : nodes_)
    {
      if (n.type == CircuitNodeType::Input)
        inputs.push_back(&n);
      if (n.type == CircuitNodeType::Output)
        outputs.push_back(&n);
    }
    std::sort(inputs.begin(), inputs.end(), [](auto *a, auto *b)
              { return a->id < b->id; });
    std::sort(outputs.begin(), outputs.end(), [](auto *a, auto *b)
              { return a->id < b->id; });

    if (inputs.empty() || outputs.empty())
      return res;
    if ((int)inputs.size() > 8)
      inputs.resize(8); // cap at 256 rows

    for (int i = 0; i < (int)inputs.size(); ++i)
      res.inputLabels.push_back(inputs[i]->label.empty()
                                    ? "I" + std::to_string(i)
                                    : inputs[i]->label);
    for (int i = 0; i < (int)outputs.size(); ++i)
      res.outputLabels.push_back(outputs[i]->label.empty()
                                     ? "O" + std::to_string(i)
                                     : outputs[i]->label);

    // Save current input values
    std::vector<bool> saved;
    for (auto *n : inputs)
      saved.push_back(n->value);

    int ni = (int)inputs.size();
    int no = (int)outputs.size();
    int numRows = 1 << ni;
    res.rows.resize(numRows, std::vector<bool>(ni + no));

    for (int row = 0; row < numRows; ++row)
    {
      for (int i = 0; i < ni; ++i)
        inputs[i]->value = (row >> (ni - 1 - i)) & 1;
      evaluate();
      for (int i = 0; i < ni; ++i)
        res.rows[row][i] = inputs[i]->value;
      for (int i = 0; i < no; ++i)
        res.rows[row][ni + i] = outputs[i]->value;
    }

    // Restore
    for (int i = 0; i < ni; ++i)
      inputs[i]->value = saved[i];
    evaluate();

    return res;
  }

  // Make it accessible
  void setCanvasGL(Canvas2DGL *gl) { canvasGL_ = gl; }

  // ── JSON serialization ────────────────────────────────────────────────
  // Serialize the current circuit to a JSON string.
  std::string toJson() const
  {
    // Build node type => string map
    auto typeStr = [](CircuitNodeType t) -> const char *
    {
      switch (t)
      {
      case CircuitNodeType::AND:
        return "AND";
      case CircuitNodeType::OR:
        return "OR";
      case CircuitNodeType::NOT:
        return "NOT";
      case CircuitNodeType::NAND:
        return "NAND";
      case CircuitNodeType::NOR:
        return "NOR";
      case CircuitNodeType::XOR:
        return "XOR";
      case CircuitNodeType::XNOR:
        return "XNOR";
      case CircuitNodeType::Input:
        return "Input";
      case CircuitNodeType::Output:
        return "Output";
      }
      return "AND";
    };

    std::ostringstream o;
    o << "{\n  \"nextId\": " << nextId_ << ",\n";

    // nodes array
    o << "  \"nodes\": [\n";
    for (int i = 0; i < (int)nodes_.size(); ++i)
    {
      const auto &n = nodes_[i];
      o << "    {"
        << "\"id\": " << n.id << ", "
        << "\"type\": \"" << typeStr(n.type) << "\", "
        << "\"x\": " << n.x << ", "
        << "\"y\": " << n.y << ", "
        << "\"value\": " << (n.value ? "true" : "false") << ", "
        << "\"label\": \"" << n.label << "\""
        << "}";
      if (i + 1 < (int)nodes_.size())
        o << ",";
      o << "\n";
    }
    o << "  ],\n";

    // wires array
    o << "  \"wires\": [\n";
    for (int i = 0; i < (int)wires_.size(); ++i)
    {
      const auto &w = wires_[i];
      o << "    {"
        << "\"from\": " << w.fromNodeId << ", "
        << "\"to\": " << w.toNodeId << ", "
        << "\"port\": " << w.toPortIndex
        << "}";
      if (i + 1 < (int)wires_.size())
        o << ",";
      o << "\n";
    }
    o << "  ]\n}\n";
    return o.str();
  }

  std::string toVerilog() const
  {
    // Collect inputs/outputs
    std::vector<const CircuitNode *> inputs, outputs, gates;
    for (auto &n : nodes_)
    {
      if (n.type == CircuitNodeType::Input)
        inputs.push_back(&n);
      else if (n.type == CircuitNodeType::Output)
        outputs.push_back(&n);
      else
        gates.push_back(&n);
    }

    auto nodeName = [&](const CircuitNode *n) -> std::string
    {
      if (!n->label.empty())
        return n->label;
      std::string base = std::string(defaultName(n->type));
      // lowercase
      for (auto &c : base)
        c = tolower(c);
      return base + "_" + std::to_string(n->id);
    };

    std::ostringstream o;
    o << "module circuit(\n";

    // ports
    for (int i = 0; i < (int)inputs.size(); ++i)
      o << "    input  wire " << nodeName(inputs[i])
        << (i + 1 < (int)inputs.size() || !outputs.empty() ? "," : "") << "\n";
    for (int i = 0; i < (int)outputs.size(); ++i)
      o << "    output wire " << nodeName(outputs[i])
        << (i + 1 < (int)outputs.size() ? "," : "") << "\n";
    o << ");\n\n";

    // internal wires for gate outputs
    for (auto *g : gates)
      o << "wire " << nodeName(g) << ";\n";
    if (!gates.empty())
      o << "\n";

    // build input-map: toNodeId+port -> driving signal name
    auto driverOf = [&](int nodeId, int port) -> std::string
    {
      for (auto &w : wires_)
      {
        if (w.toNodeId == nodeId && w.toPortIndex == port)
        {
          const CircuitNode *src = findNodeConst(w.fromNodeId);
          if (src)
            return nodeName(src);
        }
      }
      return "1'b0";
    };

    // gate assignments
    for (auto *g : gates)
    {
      std::string a = driverOf(g->id, 0);
      std::string b = driverOf(g->id, 1);
      std::string out = nodeName(g);
      switch (g->type)
      {
      case CircuitNodeType::AND:
        o << "assign " << out << " = " << a << " & " << b << ";\n";
        break;
      case CircuitNodeType::OR:
        o << "assign " << out << " = " << a << " | " << b << ";\n";
        break;
      case CircuitNodeType::NOT:
        o << "assign " << out << " = ~" << a << ";\n";
        break;
      case CircuitNodeType::NAND:
        o << "assign " << out << " = ~(" << a << " & " << b << ");\n";
        break;
      case CircuitNodeType::NOR:
        o << "assign " << out << " = ~(" << a << " | " << b << ");\n";
        break;
      case CircuitNodeType::XOR:
        o << "assign " << out << " = " << a << " ^ " << b << ";\n";
        break;
      case CircuitNodeType::XNOR:
        o << "assign " << out << " = ~(" << a << " ^ " << b << ");\n";
        break;
      default:
        break;
      }
    }
    if (!gates.empty())
      o << "\n";

    // output assignments
    for (auto *out : outputs)
      o << "assign " << nodeName(out) << " = " << driverOf(out->id, 0) << ";\n";

    o << "\nendmodule\n";
    return o.str();
  }

  std::string toVHDL() const
  {
    std::vector<const CircuitNode *> inputs, outputs, gates;
    for (auto &n : nodes_)
    {
      if (n.type == CircuitNodeType::Input)
        inputs.push_back(&n);
      else if (n.type == CircuitNodeType::Output)
        outputs.push_back(&n);
      else
        gates.push_back(&n);
    }

    auto nodeName = [&](const CircuitNode *n) -> std::string
    {
      if (!n->label.empty())
        return n->label;
      std::string base = std::string(defaultName(n->type));
      for (auto &c : base)
        c = tolower(c);
      return base + "_" + std::to_string(n->id);
    };

    auto driverOf = [&](int nodeId, int port) -> std::string
    {
      for (auto &w : wires_)
      {
        if (w.toNodeId == nodeId && w.toPortIndex == port)
        {
          const CircuitNode *src = findNodeConst(w.fromNodeId);
          if (src)
            return nodeName(src);
        }
      }
      return "'0'";
    };

    std::ostringstream o;
    o << "library IEEE;\nuse IEEE.STD_LOGIC_1164.ALL;\n\n";
    o << "entity circuit is\n  port(\n";
    for (int i = 0; i < (int)inputs.size(); ++i)
      o << "    " << nodeName(inputs[i]) << " : in  std_logic"
        << (i + 1 < (int)inputs.size() || !outputs.empty() ? ";" : "") << "\n";
    for (int i = 0; i < (int)outputs.size(); ++i)
      o << "    " << nodeName(outputs[i]) << " : out std_logic"
        << (i + 1 < (int)outputs.size() ? ";" : "") << "\n";
    o << "  );\nend circuit;\n\n";

    o << "architecture Behavioral of circuit is\n";
    for (auto *g : gates)
      o << "  signal " << nodeName(g) << " : std_logic;\n";
    o << "begin\n";

    for (auto *g : gates)
    {
      std::string a = driverOf(g->id, 0);
      std::string b = driverOf(g->id, 1);
      std::string sig = nodeName(g);
      switch (g->type)
      {
      case CircuitNodeType::AND:
        o << "  " << sig << " <= " << a << " and " << b << ";\n";
        break;
      case CircuitNodeType::OR:
        o << "  " << sig << " <= " << a << " or " << b << ";\n";
        break;
      case CircuitNodeType::NOT:
        o << "  " << sig << " <= not " << a << ";\n";
        break;
      case CircuitNodeType::NAND:
        o << "  " << sig << " <= not (" << a << " and " << b << ");\n";
        break;
      case CircuitNodeType::NOR:
        o << "  " << sig << " <= not (" << a << " or " << b << ");\n";
        break;
      case CircuitNodeType::XOR:
        o << "  " << sig << " <= " << a << " xor " << b << ";\n";
        break;
      case CircuitNodeType::XNOR:
        o << "  " << sig << " <= not (" << a << " xor " << b << ");\n";
        break;
      default:
        break;
      }
    }
    for (auto *out : outputs)
      o << "  " << nodeName(out) << " <= " << driverOf(out->id, 0) << ";\n";

    o << "end Behavioral;\n";
    return o.str();
  }

  // Renders the entire circuit bounding box to an RGBA pixel buffer.
  // Returns false if there are no nodes.
  bool renderToPixels(std::vector<uint8_t> &outPixels,
                      int &outW, int &outH,
                      float padding = 80.f) const
  {
    if (nodes_.empty())
      return false;

    // Compute bounding box
    float x0 = nodes_[0].x, y0 = nodes_[0].y;
    float x1 = x0 + nodes_[0].w, y1 = y0 + nodes_[0].h;
    for (auto &n : nodes_)
    {
      x0 = std::min(x0, n.x);
      y0 = std::min(y0, n.y);
      x1 = std::max(x1, n.x + n.w);
      y1 = std::max(y1, n.y + n.h);
    }
    x0 -= padding;
    y0 -= padding;
    x1 += padding;
    y1 += padding;

    outW = int(x1 - x0);
    outH = int(y1 - y0);
    if (outW < 1 || outH < 1)
      return false;

    // Read current framebuffer pixels that correspond to this region,
    // translated by the current viewport transform.
    // We'll use glReadPixels after rendering into an FBO at fixed scale.
    outPixels.resize(size_t(outW) * outH * 4);

    // Build ortho MVP for the bounding box region
    float mvp[16];
    // Column-major ortho: maps [x0,x1] -> [-1,1], [y0,y1] -> [1,-1]
    float rw = 1.f / (x1 - x0);
    float rh = 1.f / (y1 - y0);
    memset(mvp, 0, sizeof(mvp));
    mvp[0] = 2.f * rw;
    mvp[5] = -2.f * rh;
    mvp[10] = 1.f;
    mvp[12] = -1.f - 2.f * x0 * rw;
    mvp[13] = 1.f + 2.f * y0 * rh;
    mvp[15] = 1.f;

    // Create FBO
    GLuint fbo = 0, tex = 0, rbo = 0;
    glGenFramebuffers(1, &fbo);
    glGenTextures(1, &tex);
    glGenRenderbuffers(1, &rbo);

    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, outW, outH,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glBindRenderbuffer(GL_RENDERBUFFER, rbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, outW, outH);

    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, tex, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                              GL_RENDERBUFFER, rbo);

    glViewport(0, 0, outW, outH);
    glClearColor(15.f / 255.f, 15.f / 255.f, 20.f / 255.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Re-render into FBO
    if (canvasGL_)
    {
      Canvas2D ctx(canvasGL_, outW, outH, mvp);
      // Draw grid + nodes + wires  (reuse existing render logic)
      const_cast<CircuitSurface *>(this)->render(ctx);
    }

    // Read pixels (FBO origin is bottom-left, flip vertically)
    glReadPixels(0, 0, outW, outH, GL_RGBA, GL_UNSIGNED_BYTE,
                 outPixels.data());

    // Flip Y
    std::vector<uint8_t> row(size_t(outW) * 4);
    for (int y = 0; y < outH / 2; ++y)
    {
      uint8_t *a = outPixels.data() + size_t(y) * outW * 4;
      uint8_t *b = outPixels.data() + size_t(outH - 1 - y) * outW * 4;
      memcpy(row.data(), a, outW * 4);
      memcpy(a, b, outW * 4);
      memcpy(b, row.data(), outW * 4);
    }

    // Cleanup FBO (texture ownership stays until we're done)
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &fbo);
    glDeleteTextures(1, &tex);
    glDeleteRenderbuffers(1, &rbo);

    return true;
  }

  bool exportPNG(const std::string &path) const
  {
    std::vector<uint8_t> pixels;
    int w = 0, h = 0;
    if (!renderToPixels(pixels, w, h))
      return false;

    // stb_image_write is already available via flux_canvas2d_gl.cpp
    return stbi_write_png(path.c_str(), w, h, 4,
                          pixels.data(), w * 4) != 0;
  }

  std::string toSVG() const
  {
    if (nodes_.empty())
      return "";

    float pad = 80.f;
    float x0 = nodes_[0].x, y0 = nodes_[0].y;
    float x1 = x0 + nodes_[0].w, y1 = y0 + nodes_[0].h;
    for (auto &n : nodes_)
    {
      x0 = std::min(x0, n.x);
      y0 = std::min(y0, n.y);
      x1 = std::max(x1, n.x + n.w);
      y1 = std::max(y1, n.y + n.h);
    }
    x0 -= pad;
    y0 -= pad;
    x1 += pad;
    y1 += pad;
    float W = x1 - x0, H = y1 - y0;

    // Coordinate helper: shift world -> SVG origin
    auto sx = [&](float x)
    { return x - x0; };
    auto sy = [&](float y)
    { return y - y0; };

    std::ostringstream o;
    o << "<svg xmlns='http://www.w3.org/2000/svg' "
      << "width='" << int(W) << "' height='" << int(H) << "' "
      << "style='background:#0f0f14'>\n";

    // ── Wires ─────────────────────────────────────────────────────────
    for (auto &w : wires_)
    {
      const CircuitNode *src = findNodeConst(w.fromNodeId);
      const CircuitNode *dst = findNodeConst(w.toNodeId);
      if (!src || !dst)
        continue;

      Pt p0 = outputPortPos(*src);
      Pt p1 = inputPortPos(*dst, w.toPortIndex);
      float stub = 24.f;
      float ax = p0.x + stub, bx = p1.x - stub;

      std::string color = src->value ? "#3cc878" : "#646478";
      o << "  <path d='M" << sx(p0.x) << "," << sy(p0.y);

      if (ax <= bx)
      {
        float mx = (ax + bx) * 0.5f;
        o << " L" << sx(ax) << "," << sy(p0.y)
          << " L" << sx(mx) << "," << sy(p0.y)
          << " L" << sx(mx) << "," << sy(p1.y)
          << " L" << sx(bx) << "," << sy(p1.y);
      }
      else
      {
        float my = (p0.y + p1.y) * 0.5f;
        o << " L" << sx(ax) << "," << sy(p0.y)
          << " L" << sx(ax) << "," << sy(my)
          << " L" << sx(bx) << "," << sy(my)
          << " L" << sx(bx) << "," << sy(p1.y);
      }
      o << " L" << sx(p1.x) << "," << sy(p1.y) << "'"
        << " stroke='" << color << "' stroke-width='2'"
        << " fill='none' stroke-linecap='round'/>\n";
    }

    // ── Nodes ─────────────────────────────────────────────────────────
    for (auto &n : nodes_)
    {
      float nx = sx(n.x), ny = sy(n.y);
      std::string stroke = n.selected ? "#50a0ff" : "#a0a0b4";
      std::string fill = "#16161e";

      // Body shape
      if (n.type == CircuitNodeType::AND || n.type == CircuitNodeType::NAND)
      {
        float cx = nx + n.w * 0.48f;
        o << "  <path d='M" << nx << "," << ny
          << " L" << cx << "," << ny
          << " C" << (nx + n.w) << "," << ny << " "
          << (nx + n.w) << "," << (ny + n.h) << " "
          << cx << "," << (ny + n.h)
          << " L" << nx << "," << (ny + n.h) << " Z'"
          << " fill='" << fill << "' stroke='" << stroke << "' stroke-width='1.5'/>\n";
        if (n.type == CircuitNodeType::NAND)
        {
          float bx = nx + n.w + 5.f, by = ny + n.h * 0.5f;
          o << "  <circle cx='" << bx << "' cy='" << by
            << "' r='5' fill='" << fill
            << "' stroke='" << stroke << "' stroke-width='1.5'/>\n";
        }
      }
      else if (n.type == CircuitNodeType::OR || n.type == CircuitNodeType::NOR ||
               n.type == CircuitNodeType::XOR || n.type == CircuitNodeType::XNOR)
      {
        float mx = nx + n.w * 0.5f, my = ny + n.h * 0.5f;
        o << "  <path d='M" << nx << "," << ny
          << " Q" << mx << "," << ny << " " << (nx + n.w) << "," << my
          << " Q" << mx << "," << (ny + n.h) << " " << nx << "," << (ny + n.h)
          << " Q" << (nx + n.w * 0.22f) << "," << my << " " << nx << "," << ny << " Z'"
          << " fill='" << fill << "' stroke='" << stroke << "' stroke-width='1.5'/>\n";
        if (n.type == CircuitNodeType::XOR || n.type == CircuitNodeType::XNOR)
        {
          o << "  <path d='M" << (nx - 8) << "," << ny
            << " Q" << (nx - 8 + n.w * 0.22f) << "," << (ny + n.h * 0.5f)
            << " " << (nx - 8) << "," << (ny + n.h) << "'"
            << " fill='none' stroke='" << stroke << "' stroke-width='1.5'/>\n";
        }
        if (n.type == CircuitNodeType::NOR || n.type == CircuitNodeType::XNOR)
        {
          float bx = nx + n.w + 5.f, by = ny + n.h * 0.5f;
          o << "  <circle cx='" << bx << "' cy='" << by
            << "' r='5' fill='" << fill
            << "' stroke='" << stroke << "' stroke-width='1.5'/>\n";
        }
      }
      else if (n.type == CircuitNodeType::NOT)
      {
        float tipX = nx + n.w - 8.f;
        o << "  <path d='M" << nx << "," << ny
          << " L" << tipX << "," << (ny + n.h * 0.5f)
          << " L" << nx << "," << (ny + n.h) << " Z'"
          << " fill='" << fill << "' stroke='" << stroke << "' stroke-width='1.5'/>\n";
        o << "  <circle cx='" << (tipX + 5) << "' cy='" << (ny + n.h * 0.5f)
          << "' r='5' fill='" << fill
          << "' stroke='" << stroke << "' stroke-width='1.5'/>\n";
      }
      else
      { // Input / Output
        std::string boxFill = n.value ? "#141c1c" : "#121218";
        std::string boxStroke = n.value ? "#28c864" : "#505060";
        o << "  <rect x='" << nx << "' y='" << ny
          << "' width='" << n.w << "' height='" << n.h
          << "' rx='4' fill='" << boxFill
          << "' stroke='" << boxStroke << "' stroke-width='1.5'/>\n";
        // LED dot
        std::string dotFill = n.value ? "#28d26e" : "#2d2d37";
        o << "  <circle cx='" << (nx + n.w * 0.5f)
          << "' cy='" << (ny + n.h * 0.5f)
          << "' r='" << (n.h * 0.22f)
          << "' fill='" << dotFill << "'/>\n";
      }

      // Port dots
      for (int i = 0; i < n.inputCount(); ++i)
      {
        Pt p = inputPortPos(n, i);
        o << "  <circle cx='" << sx(p.x) << "' cy='" << sy(p.y)
          << "' r='4' fill='#328cdc'/>\n";
      }
      if (n.outputCount() > 0)
      {
        Pt p = outputPortPos(n);
        o << "  <circle cx='" << sx(p.x) << "' cy='" << sy(p.y)
          << "' r='4' fill='#1eb478'/>\n";
      }

      // Label
      const std::string &lbl = n.label.empty()
                                   ? std::string(defaultName(n.type))
                                   : n.label;
      bool isIO = (n.type == CircuitNodeType::Input ||
                   n.type == CircuitNodeType::Output);
      o << "  <text x='" << (nx + n.w * 0.5f)
        << "' y='" << (isIO ? (ny + n.h + 14.f) : (ny + n.h + 16.f))
        << "' text-anchor='middle'"
        << " font-family='sans-serif' font-size='14'"
        << " fill='#8c8ca0'>" << lbl << "</text>\n";
    }

    o << "</svg>\n";
    return o.str();
  }

  bool exportSVG(const std::string &path) const
  {
    std::string svg = toSVG();
    if (svg.empty())
      return false;
    std::ofstream f(path, std::ios::out | std::ios::trunc);
    if (!f)
      return false;
    f << svg;
    return f.good();
  }

  // Load a circuit from a JSON string. Returns false and leaves state
  // unchanged on parse error.
  bool loadFromJson(const std::string &json)
  {
    auto strToType = [](const std::string &s) -> CircuitNodeType
    {
      if (s == "OR")
        return CircuitNodeType::OR;
      if (s == "NOT")
        return CircuitNodeType::NOT;
      if (s == "NAND")
        return CircuitNodeType::NAND;
      if (s == "NOR")
        return CircuitNodeType::NOR;
      if (s == "XOR")
        return CircuitNodeType::XOR;
      if (s == "XNOR")
        return CircuitNodeType::XNOR;
      if (s == "Input")
        return CircuitNodeType::Input;
      if (s == "Output")
        return CircuitNodeType::Output;
      return CircuitNodeType::AND;
    };

    JsonValue root;
    if (!JsonParser::tryParse(json, root) || !root.isObject())
      return false;

    std::vector<CircuitNode> newNodes;
    std::vector<CircuitWire> newWires;
    int newNextId = 1;

    // nextId
    if (auto *v = root.get("nextId"))
      newNextId = v->getInt(1);

    // nodes
    if (auto *arr = root.get("nodes"))
    {
      if (!arr->isArray())
        return false;
      for (size_t i = 0; i < arr->size(); ++i)
      {
        const JsonValue &jn = (*arr)[i];
        if (!jn.isObject())
          return false;

        CircuitNode n;
        if (auto *v = jn.get("id"))
          n.id = v->getInt();
        if (auto *v = jn.get("type"))
          n.type = strToType(v->getString());
        if (auto *v = jn.get("x"))
          n.x = (float)v->getNumber();
        if (auto *v = jn.get("y"))
          n.y = (float)v->getNumber();
        if (auto *v = jn.get("value"))
          n.value = v->getBool();
        if (auto *v = jn.get("label"))
          n.label = v->getString();

        nodeDims(n.type, n.w, n.h);
        n.x = snapX(n.x, n.w); //  snap legacy positions
        n.y = snapY(n.y, n.h); //  snap legacy positions
        n.inputVals.assign(n.inputCount(), false);
        newNodes.push_back(std::move(n));
      }
    }

    // wires
    if (auto *arr = root.get("wires"))
    {
      if (!arr->isArray())
        return false;
      for (size_t i = 0; i < arr->size(); ++i)
      {
        const JsonValue &jw = (*arr)[i];
        if (!jw.isObject())
          return false;

        CircuitWire w;
        if (auto *v = jw.get("from"))
          w.fromNodeId = v->getInt();
        if (auto *v = jw.get("to"))
          w.toNodeId = v->getInt();
        if (auto *v = jw.get("port"))
          w.toPortIndex = v->getInt();
        newWires.push_back(w);
      }
    }

    // Commit — only if parsing fully succeeded
    pushUndo();
    nodes_ = std::move(newNodes);
    wires_ = std::move(newWires);
    nextId_ = newNextId;
    evaluate();
    notify();
    return true;
  }

  // ── File I/O ──────────────────────────────────────────────────────────
  bool saveToFile(const std::string &path) const
  {
    std::ofstream f(path, std::ios::out | std::ios::trunc);
    if (!f)
      return false;
    f << toJson();
    return f.good();
  }

  bool loadFromFile(const std::string &path)
  {
    std::ifstream f(path);
    if (!f)
      return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    return loadFromJson(ss.str());
  }

  // ── Duplicate (Ctrl+D) ────────────────────────────────────────────────
  void duplicateSelected()
  {
    // ── Snapshot selected nodes BY VALUE before any mutation ─────────
    std::vector<CircuitNode> sel;
    for (auto &n : nodes_)
      if (n.selected)
        sel.push_back(n); // full value copy
    if (sel.empty())
      return;

    pushUndo();

    std::unordered_map<int, int> idMap;
    std::unordered_set<int> selIds;
    int clipCounter = 1;
    for (auto &n : sel)
    {
      idMap[n.id] = clipCounter++;
      selIds.insert(n.id);
    }

    const float offset = kSnapGrid * 2.f;
    deselectAll();

    std::unordered_map<int, int> clipToReal;

    // Create duplicates — nodes_ may reallocate here, but sel is a
    // separate vector so its contents are never invalidated
    for (auto &src : sel)
    {
      CircuitNode n;
      n.id = nextId_++;
      n.type = src.type;
      n.w = src.w;
      n.h = src.h;
      n.x = snapX(src.x + offset, n.w);
      n.y = snapY(src.y + offset, n.h);
      n.value = src.value;
      n.label = src.label;
      n.inputVals.assign(n.inputCount(), false);
      n.selected = true;
      clipToReal[idMap[src.id]] = n.id;
      nodes_.push_back(std::move(n));
    }

    // ── Snapshot wire count so we never iterate our own appended wires
    const int wireCountSnapshot = (int)wires_.size();
    for (int i = 0; i < wireCountSnapshot; ++i)
    {
      const CircuitWire &w = wires_[i];
      if (!selIds.count(w.fromNodeId) || !selIds.count(w.toNodeId))
        continue;
      CircuitWire nw;
      nw.fromNodeId = clipToReal[idMap[w.fromNodeId]];
      nw.toNodeId = clipToReal[idMap[w.toNodeId]];
      nw.toPortIndex = w.toPortIndex;
      wires_.push_back(nw);
    }

    evaluate();
    notify();
  }

  // ── Copy / Paste ──────────────────────────────────────────────────────
  bool hasClipboard() const { return !clipboard_.empty(); }

  void copySelected()
  {
    // Collect selected nodes
    std::vector<const CircuitNode *> sel;
    for (auto &n : nodes_)
      if (n.selected)
        sel.push_back(&n);
    if (sel.empty())
      return;

    // Compute centroid so paste lands near the same area
    float cx = 0, cy = 0;
    for (auto *n : sel)
    {
      cx += n->x + n->w * 0.5f;
      cy += n->y + n->h * 0.5f;
    }
    cx /= float(sel.size());
    cy /= float(sel.size());

    // Build a set of selected IDs for wire filtering
    std::unordered_set<int> selIds;
    for (auto *n : sel)
      selIds.insert(n->id);

    // Assign stable clip-IDs (1-based, independent of nextId_)
    // Map: real node id -> clipId
    std::unordered_map<int, int> idMap;
    int clipCounter = 1;
    for (auto *n : sel)
      idMap[n->id] = clipCounter++;

    clipboard_.clear();
    clipboardWires_.clear();
    pasteOffsetAccum_ = 0.f; // reset stagger on fresh copy

    for (auto *n : sel)
    {
      ClipboardEntry e;
      e.type = n->type;
      e.x = (n->x + n->w * 0.5f) - cx; // offset from centroid
      e.y = (n->y + n->h * 0.5f) - cy;
      e.value = n->value;
      e.label = n->label;
      e.clipId = idMap[n->id];
      clipboard_.push_back(e);
    }

    // Only keep wires entirely within the selection
    for (auto &w : wires_)
    {
      if (selIds.count(w.fromNodeId) && selIds.count(w.toNodeId))
      {
        ClipboardWire cw;
        cw.fromClipId = idMap[w.fromNodeId];
        cw.toClipId = idMap[w.toNodeId];
        cw.toPortIndex = w.toPortIndex;
        clipboardWires_.push_back(cw);
      }
    }
  }

  void pasteClipboard()
  {
    if (clipboard_.empty())
      return;
    pushUndo();

    // Each successive paste of the same copy shifts by another 40 units
    pasteOffsetAccum_ += 40.f;
    float ox = viewOffsetX_ + viewW_ / (currentZoom_ * 2.f) + pasteOffsetAccum_;
    float oy = viewOffsetY_ + viewH_ / (currentZoom_ * 2.f) + pasteOffsetAccum_;

    // Map clipId -> newly allocated real node id
    std::unordered_map<int, int> clipToReal;

    // Deselect everything first
    deselectAll();

    // Create new nodes
    for (auto &e : clipboard_)
    {
      CircuitNode n;
      n.id = nextId_++;
      n.type = e.type;
      float nw, nh;
      nodeDims(e.type, nw, nh);
      n.w = nw;
      n.h = nh;

      n.x = snapX(ox + e.x - nw * 0.5f, nw); //  snap
      n.y = snapY(oy + e.y - nh * 0.5f, nh); //  snap
      n.value = e.value;
      n.label = e.label;
      n.inputVals.assign(n.inputCount(), false);
      n.selected = true; // paste selects the new nodes
      clipToReal[e.clipId] = n.id;
      nodes_.push_back(std::move(n));
    }

    // Recreate internal wires with new IDs
    for (auto &cw : clipboardWires_)
    {
      auto itF = clipToReal.find(cw.fromClipId);
      auto itT = clipToReal.find(cw.toClipId);
      if (itF == clipToReal.end() || itT == clipToReal.end())
        continue;
      CircuitWire w;
      w.fromNodeId = itF->second;
      w.toNodeId = itT->second;
      w.toPortIndex = cw.toPortIndex;
      wires_.push_back(w);
    }

    evaluate();
    notify();
  }

  void clear()
  {
    nodes_.clear();
    wires_.clear();
    nextId_ = 1;
    wireStart_ = {};
    dragging_ = false;
    notify();
  }

  void deleteSelected()
  {
    // collect node ids to remove
    std::vector<int> dead;
    for (auto &n : nodes_)
      if (n.selected)
        dead.push_back(n.id);

    for (int id : dead)
    {
      nodes_.erase(std::remove_if(nodes_.begin(), nodes_.end(),
                                  [id](const CircuitNode &n)
                                  { return n.id == id; }),
                   nodes_.end());
      wires_.erase(std::remove_if(wires_.begin(), wires_.end(),
                                  [id](const CircuitWire &w)
                                  { return w.fromNodeId == id || w.toNodeId == id; }),
                   wires_.end());
    }

    // also delete selected wires
    wires_.erase(std::remove_if(wires_.begin(), wires_.end(),
                                [](const CircuitWire &w)
                                { return w.selected; }),
                 wires_.end());

    evaluate();
    notify();
  }

  void selectAll()
  {
    for (auto &n : nodes_)
      n.selected = true;
    for (auto &w : wires_)
      w.selected = true;
  }
  void deselectAll()
  {
    for (auto &n : nodes_)
      n.selected = false;
    for (auto &w : wires_)
      w.selected = false;
  }

  int selectedCount() const
  {
    return int(std::count_if(nodes_.begin(), nodes_.end(),
                             [](const CircuitNode &n)
                             { return n.selected; }));
  }

  // ── Place a new node at world position ────────────────────────────────
  CircuitNode *placeNode(CircuitNodeType type, float wx, float wy)
  {
    CircuitNode n;
    n.id = nextId_++;
    n.type = type;
    n.x = wx;
    n.y = wy;
    nodeDims(type, n.w, n.h);
    n.x = snapX(wx, n.w);
    n.y = snapY(wy, n.h);
    n.inputVals.assign(n.inputCount(), false);
    nodes_.push_back(std::move(n));
    evaluate();
    notify();
    return &nodes_.back();
  }

  // ── Undo / redo (50 levels) ───────────────────────────────────────────
  bool canUndo() const { return !undoNodes_.empty(); }
  bool canRedo() const { return !redoNodes_.empty(); }

  void undo()
  {
    if (undoNodes_.empty())
      return;
    redoNodes_.push_back({nodes_, wires_});
    auto &s = undoNodes_.back();
    nodes_ = s.first;
    wires_ = s.second;
    undoNodes_.pop_back();
    evaluate();
    notify();
  }
  void redo()
  {
    if (redoNodes_.empty())
      return;
    undoNodes_.push_back({nodes_, wires_});
    auto &s = redoNodes_.back();
    nodes_ = s.first;
    wires_ = s.second;
    redoNodes_.pop_back();
    evaluate();
    notify();
  }

  void commitTextEdit()
  {
    if (!textEditing_)
      return;
    textEditing_ = false;
    CircuitNode *n = findNode(textEdit_.nodeId);
    if (n)
    {
      pushUndo();
      n->label = textEdit_.text; // empty string = revert to default gate name
    }
    textEdit_ = {};
    notify();
  }

  void startTextEdit(CircuitNode *n)
  {
    if (textEditing_)
      commitTextEdit();
    textEditing_ = true;
    textEdit_.nodeId = n->id;
    // pre-populate with existing label (or default name if none set)
    textEdit_.text = n->label.empty() ? defaultName(n->type) : n->label;
    textEdit_.cursorVisible = true;
    textEdit_.blinkTimer = 0.0;
  }

  static const char *defaultName(CircuitNodeType t)
  {
    switch (t)
    {
    case CircuitNodeType::AND:
      return "AND";
    case CircuitNodeType::OR:
      return "OR";
    case CircuitNodeType::NOT:
      return "NOT";
    case CircuitNodeType::NAND:
      return "NAND";
    case CircuitNodeType::NOR:
      return "NOR";
    case CircuitNodeType::XOR:
      return "XOR";
    case CircuitNodeType::XNOR:
      return "XNOR";
    case CircuitNodeType::Input:
      return "IN";
    case CircuitNodeType::Output:
      return "OUT";
    }
    return "";
  }

  // ── RenderSurface ─────────────────────────────────────────────────────
  void initialize(int w, int h) override
  {
    w_ = w;
    h_ = h;
  }
  void resize(int w, int h) override
  {
    w_ = w;
    h_ = h;
  }
  void destroy() override
  {
    nodes_.clear();
    wires_.clear();
    undoNodes_.clear();
    redoNodes_.clear();
  }
  void update(double dt) override
  {
    if (!textEditing_)
      return;
    textEdit_.blinkTimer += dt;
    if (textEdit_.blinkTimer >= 0.53)
    {
      textEdit_.blinkTimer = 0.0;
      textEdit_.cursorVisible = !textEdit_.cursorVisible;
    }
  }
  bool needsContinuousRedraw() const override { return textEditing_; }

  // ── Mouse ─────────────────────────────────────────────────────────────
  void onMouseDown(float x, float y) override
  {

    if (textEditing_)
      commitTextEdit();

    // ── Context menu intercept ─────────────────────────────────────────
    if (ctxMenu_.open)
    {
      int item = -1;
      bool inMenu = menuHitTest(x, y, item);
      if (inMenu)
      {
        if (item >= 0 && !ctxMenu_.items[item].separator && ctxMenu_.items[item].action)
          ctxMenu_.items[item].action();
        closeContextMenu();
        notify();
        return;
      }
      closeContextMenu();
      notify();
    }

    mouseDownX_ = x;
    mouseDownY_ = y;
    curX_ = x;
    curY_ = y;

    if (mode_ == CircuitMode::Delete)
    {
      pushUndo();
      // Try to delete a node first
      CircuitNode *n = hitNode(x, y);
      if (n)
      {
        int id = n->id;
        nodes_.erase(std::remove_if(nodes_.begin(), nodes_.end(),
                                    [id](const CircuitNode &nn)
                                    { return nn.id == id; }),
                     nodes_.end());
        wires_.erase(std::remove_if(wires_.begin(), wires_.end(),
                                    [id](const CircuitWire &w)
                                    { return w.fromNodeId == id || w.toNodeId == id; }),
                     wires_.end());
        evaluate();
        notify();
        return;
      }
      // Try to delete a wire
      int wireIdx = hitWire(x, y);
      if (wireIdx >= 0)
      {
        wires_.erase(wires_.begin() + wireIdx);
        evaluate();
        notify();
      }
      return;
    }

    if (mode_ == CircuitMode::Wire)
    {
      PortRef port = hitPort(x, y);
      if (!port.valid())
        return;

      if (!wireStart_.valid())
      {
        wireStart_ = port;
      }
      else
      {
        PortRef from, to;
        if (wireStart_.isOutput && !port.isOutput)
        {
          from = wireStart_;
          to = port;
        }
        else if (!wireStart_.isOutput && port.isOutput)
        {
          from = port;
          to = wireStart_;
        }
        else
        {
          wireStart_ = port;
          return;
        }

        if (from.nodeId == to.nodeId)
        {
          wireStart_ = {};
          return;
        }

        pushUndo();
        wires_.erase(std::remove_if(wires_.begin(), wires_.end(),
                                    [&to](const CircuitWire &w)
                                    { return w.toNodeId == to.nodeId && w.toPortIndex == to.portIdx; }),
                     wires_.end());

        CircuitWire w;
        w.fromNodeId = from.nodeId;
        w.toNodeId = to.nodeId;
        w.toPortIndex = to.portIdx;
        wires_.push_back(w);

        wireStart_ = {};
        evaluate();
        notify();
      }
      return;
    }

    // ── Select mode ───────────────────────────────────────────────────
    bool shift = Key::isShiftDown();
    CircuitNode *n = hitNode(x, y);

    if (n)
    {
      if (!shift && !n->selected)
        deselectAll();
      n->selected = true;
      dragging_ = true;
      dragStartX_ = x;
      dragStartY_ = y;
      for (auto &nn : nodes_)
      {
        nn.dragOX = nn.x;
        nn.dragOY = nn.y;
      }
      return;
    }

    // Try to select a wire
    int wireIdx = hitWire(x, y);
    if (wireIdx >= 0)
    {
      if (!shift)
        deselectAll();
      wires_[wireIdx].selected = !wires_[wireIdx].selected;
      notify();
      return;
    }

    // Rubber band
    if (!shift)
      deselectAll();
    rubberBand_ = true;
    rubberX_ = x;
    rubberY_ = y;
    rubberW_ = 0;
    rubberH_ = 0;
  }

  void onMouseMove(float x, float y) override
  {
    curX_ = x;
    curY_ = y;
    if (onCursorMoved)
      onCursorMoved(x, y);

    if (ctxMenu_.open)
    {
      int item = -1;
      menuHitTest(x, y, item);
      if (item != ctxMenu_.hoveredItem)
      {
        ctxMenu_.hoveredItem = item;
        notify();
      }
    }

    if (dragging_)
    {
      float dx = x - dragStartX_, dy = y - dragStartY_;
      for (auto &n : nodes_)
      {
        if (!n.selected)
          continue;
        float rawX = n.dragOX + dx;
        float rawY = n.dragOY + dy;
        n.x = snapX(rawX, n.w);
        n.y = snapY(rawY, n.h);
      }
    }
    else if (rubberBand_)
    {
      rubberW_ = x - rubberX_;
      rubberH_ = y - rubberY_;
    }
  }

  void onMouseUp(float x, float y) override
  {
    if (dragging_)
    {
      dragging_ = false;
      float dx = x - mouseDownX_, dy = y - mouseDownY_;
      if (std::abs(dx) < 4.f && std::abs(dy) < 4.f)
      {
        CircuitNode *n = hitNode(x, y);
        if (n)
        {
          uint32_t now = platformTickCount();
          bool isDoubleClick = (now - lastClickTime_ < 350 &&
                                lastClickNodeId_ == n->id);
          lastClickTime_ = now;
          lastClickNodeId_ = n->id;

          if (isDoubleClick)
          {
            // Double-click on ANY node => rename
            startTextEdit(n);
          }
          else
          {
            // Single click on Input => toggle value
            if (n->type == CircuitNodeType::Input)
            {
              pushUndo();
              n->value = !n->value;
              evaluate();
              notify();
            }
            // Single click on any other node => no action (just select)
          }
        }
        else
        {
          // Clicked empty space — reset double-click tracking
          lastClickTime_ = 0;
          lastClickNodeId_ = -1;
        }
      }
      else
      {
        // Was a drag, not a click — reset double-click tracking
        lastClickTime_ = 0;
        lastClickNodeId_ = -1;
        notify();
      }
    }
    else if (rubberBand_)
    {
      rubberBand_ = false;
      float rx0 = rubberW_ < 0 ? rubberX_ + rubberW_ : rubberX_;
      float ry0 = rubberH_ < 0 ? rubberY_ + rubberH_ : rubberY_;
      float rx1 = rx0 + std::abs(rubberW_);
      float ry1 = ry0 + std::abs(rubberH_);
      for (auto &n : nodes_)
      {
        if (n.x >= rx0 && n.y >= ry0 && n.x + n.w <= rx1 && n.y + n.h <= ry1)
          n.selected = true;
      }

      for (auto &w : wires_)
      {
        const CircuitNode *src = findNodeConst(w.fromNodeId);
        const CircuitNode *dst = findNodeConst(w.toNodeId);
        if (src && dst)
        {
          Pt p0 = outputPortPos(*src);
          Pt p1 = inputPortPos(*dst, w.toPortIndex);
          if (p0.x >= rx0 && p0.x <= rx1 && p0.y >= ry0 && p0.y <= ry1 &&
              p1.x >= rx0 && p1.x <= rx1 && p1.y >= ry0 && p1.y <= ry1)
            w.selected = true;
        }
      }
    }
  }

  void onRightMouseDown(float x, float y) override
  {
    if (textEditing_)
      commitTextEdit();

    // If a menu is already open, close it
    if (ctxMenu_.open)
    {
      closeContextMenu();
      notify();
      return;
    }

    CircuitNode *n = hitNode(x, y);
    int wireIdx = hitWire(x, y);

    std::vector<ContextMenuItem> items;

    if (n)
    {
      // Node context menu
      CircuitNode *target = n;
      items.push_back({"Rename", [this, target]
                       { startTextEdit(target); notify(); }});
      items.push_back({"", {}, true}); // separator
      if (target->type == CircuitNodeType::Input)
        items.push_back({"Toggle", [this, target]
                         { pushUndo(); target->value = !target->value; evaluate(); notify(); }});
      items.push_back({"Duplicate", [this, target]
                       { deselectAll(); target->selected = true; duplicateSelected(); }});
      items.push_back({"", {}, true});
      items.push_back({"Delete", [this, target]
                       {
                         pushUndo();
                         int id = target->id;
                         nodes_.erase(std::remove_if(nodes_.begin(), nodes_.end(), [id](const CircuitNode &n)
                                                     { return n.id == id; }),
                                      nodes_.end());
                         wires_.erase(std::remove_if(wires_.begin(), wires_.end(), [id](const CircuitWire &w)
                                                     { return w.fromNodeId == id || w.toNodeId == id; }),
                                      wires_.end());
                         evaluate();
                         notify();
                       }});
    }
    else if (wireIdx >= 0)
    {
      // Wire context menu
      int idx = wireIdx;
      items.push_back({"Delete wire", [this, idx]
                       { pushUndo(); wires_.erase(wires_.begin() + idx); evaluate(); notify(); }});
    }
    else
    {
      // Canvas context menu
      float cx = x, cy = y;
      items.push_back({"Select all", [this]
                       { selectAll(); notify(); }});
      items.push_back({"Deselect all", [this]
                       { deselectAll(); notify(); }});
      items.push_back({"", {}, true});
      items.push_back({"Clear canvas", [this]
                       { pushUndo(); clear(); }});
    }

    if (!items.empty())
      openContextMenu(x, y, std::move(items));
    notify();
  }

  void onKeyDown(const KeyEvent &e) override
  {

    if (textEditing_)
    {
      if (e.codepoint >= 32 && e.codepoint != 127)
      {
        textEdit_.text += char(e.codepoint);
        textEdit_.cursorVisible = true;
        textEdit_.blinkTimer = 0.0;
        notify();
      }
      else
        switch (e.virtualKey)
        {
        case Key::Backspace:
          if (!textEdit_.text.empty())
            textEdit_.text.pop_back();
          textEdit_.cursorVisible = true;
          textEdit_.blinkTimer = 0.0;
          notify();
          break;
        case Key::Return:
          commitTextEdit();
          break;
        case Key::Escape:
          textEditing_ = false;
          textEdit_ = {};
          notify();
          break;
        default:
          break;
        }
      return; // swallow all keys while editing
    }

    if (e.virtualKey == Key::Delete || e.virtualKey == Key::Backspace)
    {
      pushUndo();
      deleteSelected();
      return;
    }
    if (e.ctrl && e.virtualKey == 'A')
    {
      selectAll();
      notify();
      return;
    }
    if (e.ctrl && e.virtualKey == 'Z')
    {
      e.shift ? redo() : undo();
      return;
    }

    if (e.ctrl && e.virtualKey == 'C')
    {
      copySelected();
      return;
    }
    if (e.ctrl && e.virtualKey == 'V')
    {
      pasteClipboard();
      return;
    }
    if (e.ctrl && e.virtualKey == 'D')
    {
      duplicateSelected();
      return;
    }
  }
  void onKeyUp(const KeyEvent &) override {}

  // ── Render ────────────────────────────────────────────────────────────
  void render(Canvas2D &ctx) override
  {
    float vx = viewOffsetX_, vy = viewOffsetY_;
    float vw = viewW_ / currentZoom_, vh = viewH_ / currentZoom_;

    ctx.setFillColor(Color::fromRGB(15, 15, 20));
    ctx.fillRect(vx, vy, vw, vh);

    drawGrid(ctx, vx, vy, vw, vh);
    drawAxes(ctx, vx, vy, vw, vh);

    for (auto &w : wires_)
      drawWire(ctx, w);

    if (wireStart_.valid() && mode_ == CircuitMode::Wire)
      drawWirePreview(ctx);

    for (auto &n : nodes_)
      drawNode(ctx, n);

    if (rubberBand_)
      drawRubberBand(ctx);

    if (ctxMenu_.open)
      drawContextMenu(ctx);
    drawMinimap(ctx);
  }

private:
  // Set by CircuitApp after surface creation
  Canvas2DGL *canvasGL_ = nullptr;

  // ── Context menu ──────────────────────────────────────────────────────
  struct ContextMenuItem
  {
    std::string label;
    std::function<void()> action;
    bool separator = false; // if true, draw a divider line instead
  };
  struct ContextMenu
  {
    float x = 0, y = 0; // world-space position
    std::vector<ContextMenuItem> items;
    int hoveredItem = -1;
    bool open = false;
  };
  ContextMenu ctxMenu_;

  static constexpr float kMenuItemH = 22.f; // screen pixels
  static constexpr float kMenuW = 140.f;
  static constexpr float kMenuPadding = 6.f;

  // ── Minimap ───────────────────────────────────────────────────────────
  static constexpr float kMiniW = 160.f; // screen pixels
  static constexpr float kMiniH = 100.f;
  static constexpr float kMiniMargin = 12.f;

  struct TextEditState
  {
    int nodeId = -1;  // which node is being renamed
    std::string text; // current edit buffer
    double blinkTimer = 0.0;
    bool cursorVisible = true;
  };
  bool textEditing_ = false;
  TextEditState textEdit_;

  uint32_t lastClickTime_ = 0;
  int lastClickNodeId_ = -1;

  void openContextMenu(float wx, float wy, std::vector<ContextMenuItem> items)
  {
    ctxMenu_.x = wx;
    ctxMenu_.y = wy;
    ctxMenu_.items = std::move(items);
    ctxMenu_.hoveredItem = -1;
    ctxMenu_.open = true;
  }

  void closeContextMenu()
  {
    ctxMenu_.open = false;
    ctxMenu_.items.clear();
  }

  // Returns true if (wx,wy) is inside the menu, and fills outItem with the
  // item index under the cursor (-1 if on separator/outside).
  bool menuHitTest(float wx, float wy, int &outItem) const
  {
    if (!ctxMenu_.open)
      return false;
    float mw = kMenuW / currentZoom_;
    float itemH = kMenuItemH / currentZoom_;
    float pad = kMenuPadding / currentZoom_;

    // Compute total height
    float totalH = pad;
    for (auto &it : ctxMenu_.items)
      totalH += it.separator ? (4.f / currentZoom_) : itemH;
    totalH += pad;

    float mx = ctxMenu_.x, my = ctxMenu_.y;
    if (wx < mx || wx > mx + mw || wy < my || wy > my + totalH)
    {
      outItem = -1;
      return false;
    }

    float cy = my + pad;
    for (int i = 0; i < (int)ctxMenu_.items.size(); ++i)
    {
      auto &it = ctxMenu_.items[i];
      if (it.separator)
      {
        cy += 4.f / currentZoom_;
        continue;
      }
      if (wy >= cy && wy < cy + itemH)
      {
        outItem = i;
        return true;
      }
      cy += itemH;
    }
    outItem = -1;
    return true; // inside menu but on separator
  }

  void drawContextMenu(Canvas2D &ctx) const
  {
    float mw = kMenuW / currentZoom_;
    float itemH = kMenuItemH / currentZoom_;
    float pad = kMenuPadding / currentZoom_;
    float sepH = 4.f / currentZoom_;
    float fs = 14.f / currentZoom_;
    float radius = 4.f / currentZoom_;

    // Total height
    float totalH = pad * 2.f;
    for (auto &it : ctxMenu_.items)
      totalH += it.separator ? sepH : itemH;

    float mx = ctxMenu_.x, my = ctxMenu_.y;

    // Shadow
    ctx.setFillColor(Color::fromRGBA(0, 0, 0, 60));
    ctx.fillRoundedRect(mx + 2.f / currentZoom_, my + 2.f / currentZoom_,
                        mw, totalH, radius);

    // Background
    ctx.setFillColor(Color::fromRGB(24, 24, 32));
    ctx.fillRoundedRect(mx, my, mw, totalH, radius);

    // Border
    ctx.setStrokeColor(Color::fromRGBA(70, 70, 90, 255));
    ctx.setLineWidth(1.f / currentZoom_);
    ctx.beginPath();
    ctx.rect(mx, my, mw, totalH);
    ctx.stroke();

    // Items
    char font[32];
    snprintf(font, sizeof(font), "%.0fpx sans", fs);
    ctx.setFont(font);
    ctx.setTextBaseline(TextBaseline::Middle);

    float cy = my + pad;
    for (int i = 0; i < (int)ctxMenu_.items.size(); ++i)
    {
      auto &it = ctxMenu_.items[i];
      if (it.separator)
      {
        float sy = cy + sepH * 0.5f;
        ctx.setStrokeColor(Color::fromRGBA(60, 60, 80, 200));
        ctx.setLineWidth(0.5f / currentZoom_);
        ctx.beginPath();
        ctx.moveTo(mx + pad, sy);
        ctx.lineTo(mx + mw - pad, sy);
        ctx.stroke();
        cy += sepH;
        continue;
      }

      // Hover highlight
      if (i == ctxMenu_.hoveredItem)
      {
        ctx.setFillColor(Color::fromRGBA(50, 100, 200, 160));
        ctx.fillRoundedRect(mx + 2.f / currentZoom_,
                            cy + 1.f / currentZoom_,
                            mw - 4.f / currentZoom_,
                            itemH - 2.f / currentZoom_,
                            2.f / currentZoom_);
      }

      // Label
      ctx.setFillColor(i == ctxMenu_.hoveredItem
                           ? Color::fromRGB(220, 230, 255)
                           : Color::fromRGB(180, 180, 200));
      ctx.fillText(it.label.c_str(),
                   mx + pad * 2.f,
                   cy + itemH * 0.5f);

      cy += itemH;
    }
  }

  void drawMinimap(Canvas2D &ctx) const
  {
    if (nodes_.empty())
      return;

    // ── World bounds of all nodes ─────────────────────────────────────
    float wxMin = nodes_[0].x, wxMax = nodes_[0].x + nodes_[0].w;
    float wyMin = nodes_[0].y, wyMax = nodes_[0].y + nodes_[0].h;
    for (auto &n : nodes_)
    {
      wxMin = std::min(wxMin, n.x);
      wxMax = std::max(wxMax, n.x + n.w);
      wyMin = std::min(wyMin, n.y);
      wyMax = std::max(wyMax, n.y + n.h);
    }

    // Add padding around content
    float pad = 500.f;
    wxMin -= pad;
    wxMax += pad;
    wyMin -= pad;
    wyMax += pad;
    float worldW = wxMax - wxMin;
    float worldH = wyMax - wyMin;
    if (worldW < 1.f || worldH < 1.f)
      return;

    // ── Minimap screen rect (bottom-right corner, in world-space coords)
    float sx = viewOffsetX_ + viewW_ / currentZoom_ - kMiniW / currentZoom_ - kMiniMargin / currentZoom_;
    float sy = viewOffsetY_ + viewH_ / currentZoom_ - kMiniH / currentZoom_ - kMiniMargin / currentZoom_;
    float sw = kMiniW / currentZoom_;
    float sh = kMiniH / currentZoom_;

    // Scale factors: world → minimap
    float scaleX = sw / worldW;
    float scaleY = sh / worldH;

    auto toMiniX = [&](float wx)
    { return sx + (wx - wxMin) * scaleX; };
    auto toMiniY = [&](float wy)
    { return sy + (wy - wyMin) * scaleY; };

    // ── Background ────────────────────────────────────────────────────
    ctx.setFillColor(Color::fromRGBA(12, 12, 18, 210));
    ctx.fillRoundedRect(sx, sy, sw, sh, 3.f / currentZoom_);

    // ── Border ────────────────────────────────────────────────────────
    ctx.setStrokeColor(Color::fromRGBA(60, 60, 80, 255));
    ctx.setLineWidth(1.f / currentZoom_);
    ctx.beginPath();
    ctx.rect(sx, sy, sw, sh);
    ctx.stroke();

    // ── Nodes as tiny colored rects ───────────────────────────────────
    for (auto &n : nodes_)
    {
      float nx = toMiniX(n.x);
      float ny = toMiniY(n.y);
      float nw = std::max(1.5f / currentZoom_, n.w * scaleX);
      float nh = std::max(1.5f / currentZoom_, n.h * scaleY);

      Color fill;
      if (n.type == CircuitNodeType::Input)
        fill = n.value ? Color::fromRGB(30, 160, 80) : Color::fromRGB(50, 50, 70);
      else if (n.type == CircuitNodeType::Output)
        fill = n.value ? Color::fromRGB(30, 180, 90) : Color::fromRGB(60, 60, 80);
      else
        fill = n.selected ? Color::fromRGB(60, 120, 200) : Color::fromRGB(100, 100, 120);

      ctx.setFillColor(fill);
      ctx.fillRect(nx, ny, nw, nh);
    }

    // ── Viewport rect ─────────────────────────────────────────────────
    float vpX = toMiniX(viewOffsetX_);
    float vpY = toMiniY(viewOffsetY_);
    float vpW = (viewW_ / currentZoom_) * scaleX;
    float vpH = (viewH_ / currentZoom_) * scaleY;

    // Clamp to minimap bounds
    vpX = std::max(sx, std::min(vpX, sx + sw));
    vpY = std::max(sy, std::min(vpY, sy + sh));
    vpW = std::min(vpW, sw - (vpX - sx));
    vpH = std::min(vpH, sh - (vpY - sy));

    ctx.setFillColor(Color::fromRGBA(80, 160, 255, 25));
    ctx.fillRect(vpX, vpY, vpW, vpH);

    ctx.setStrokeColor(Color::fromRGBA(80, 160, 255, 180));
    ctx.setLineWidth(1.f / currentZoom_);
    ctx.beginPath();
    ctx.rect(vpX, vpY, vpW, vpH);
    ctx.stroke();
  }

  // ── Rounded-corner polyline helper ────────────────────────────────────

  void roundedPolyline(Canvas2D &ctx,
                       const std::vector<std::pair<float, float>> &pts,
                       float r) const
  {
    // pts[0] is already the moveTo point — start from index 1
    for (int i = 1; i < (int)pts.size(); ++i)
    {
      float x = pts[i].first, y = pts[i].second;
      float px = pts[i - 1].first, py = pts[i - 1].second;

      bool isLast = (i == (int)pts.size() - 1);

      if (isLast)
      {
        // Last point: just line to it — no rounding needed at endpoint
        ctx.lineTo(x, y);
      }
      else
      {
        float nx = pts[i + 1].first, ny = pts[i + 1].second;

        // Incoming direction (from prev to current)
        float idx = x - px, idy = y - py;
        float ilen = std::hypot(idx, idy);

        // Outgoing direction (from current to next)
        float odx = nx - x, ody = ny - y;
        float olen = std::hypot(odx, ody);

        if (ilen < 1e-4f || olen < 1e-4f)
        {
          ctx.lineTo(x, y);
          continue;
        }

        // Clamp radius so it never exceeds half a segment
        float cr = std::min(r, std::min(ilen, olen) * 0.5f);

        // Point just before the corner (on the incoming segment)
        float bx = x - (idx / ilen) * cr;
        float by = y - (idy / ilen) * cr;

        // Point just after the corner (on the outgoing segment)
        float ax = x + (odx / olen) * cr;
        float ay = y + (ody / olen) * cr;

        ctx.lineTo(bx, by);
        ctx.quadraticCurveTo(x, y, ax, ay);
      }
    }
  }

  // ── Grid snapping ─────────────────────────────────────────────────────
  static constexpr float kSnapGrid = 64.f;

  static float snap(float v)
  {
    return std::round(v / kSnapGrid) * kSnapGrid;
  }
  static float snapX(float x, float w)
  {
    // Snap the left edge so the centre lands on a grid crossing
    return snap(x + w * 0.5f) - w * 0.5f;
  }
  static float snapY(float y, float h)
  {
    return snap(y + h * 0.5f) - h * 0.5f;
  }

  // ── Node dimensions ───────────────────────────────────────────────────
  static void nodeDims(CircuitNodeType type, float &w, float &h)
  {
    switch (type)
    {
    case CircuitNodeType::AND:
    case CircuitNodeType::NAND:
      w = 1120;
      h = 1180;
      break;
    case CircuitNodeType::OR:
    case CircuitNodeType::NOR:
    case CircuitNodeType::XOR:
    case CircuitNodeType::XNOR:
      w = 1220;
      h = 1180;
      break;
    case CircuitNodeType::NOT:
      w = 1196;
      h = 1172;
      break;
    case CircuitNodeType::Input:
    case CircuitNodeType::Output:
      w = 1100;
      h = 1160;
      break;
    }
  }

  // ── Port world positions ──────────────────────────────────────────────
  static float inputPortY(const CircuitNode &n, int idx)
  {
    int cnt = n.inputCount();
    float step = n.h / float(cnt + 1);
    return n.y + step * float(idx + 1);
  }
  static float outputPortY(const CircuitNode &n)
  {
    return n.y + n.h * 0.5f;
  }

  struct Pt
  {
    float x, y;
  };
  static Pt inputPortPos(const CircuitNode &n, int idx)
  {
    return {n.x, inputPortY(n, idx)};
  }
  static Pt outputPortPos(const CircuitNode &n)
  {
    return {n.x + n.w, outputPortY(n)};
  }

  // ── Wire hit testing (sample bezier) ──────────────────────────────────
  int hitWire(float mx, float my) const
  {
    const float thresh = 6.f / currentZoom_;
    const float stub = 24.f / currentZoom_;

    for (int i = int(wires_.size()) - 1; i >= 0; --i)
    {
      const CircuitWire &w = wires_[i];
      const CircuitNode *src = findNodeConst(w.fromNodeId);
      const CircuitNode *dst = findNodeConst(w.toNodeId);
      if (!src || !dst)
        continue;

      Pt p0 = outputPortPos(*src);
      Pt p1 = inputPortPos(*dst, w.toPortIndex);

      float x0 = p0.x, y0 = p0.y;
      float x1 = p1.x, y1 = p1.y;
      float ax = x0 + stub;
      float bx = x1 - stub;

      // Build the polyline segments and test each one
      // segTest: returns true if point (mx,my) is within thresh of segment (sx0,sy0)-(sx1,sy1)
      auto segTest = [&](float sx0, float sy0, float sx1, float sy1) -> bool
      {
        float dx = sx1 - sx0, dy = sy1 - sy0;
        float len2 = dx * dx + dy * dy;
        if (len2 < 1e-6f)
          return std::hypot(mx - sx0, my - sy0) < thresh;
        float t = ((mx - sx0) * dx + (my - sy0) * dy) / len2;
        t = std::max(0.f, std::min(1.f, t));
        float px = sx0 + t * dx, py = sy0 + t * dy;
        return std::hypot(mx - px, my - py) < thresh;
      };

      bool hit = false;
      if (ax <= bx)
      {
        float midX = (ax + bx) * 0.5f;
        hit = segTest(x0, y0, ax, y0) ||
              segTest(ax, y0, midX, y0) ||
              segTest(midX, y0, midX, y1) ||
              segTest(midX, y1, bx, y1) ||
              segTest(bx, y1, x1, y1);
      }
      else
      {
        float midY = (y0 + y1) * 0.5f;
        hit = segTest(x0, y0, ax, y0) ||
              segTest(ax, y0, ax, midY) ||
              segTest(ax, midY, bx, midY) ||
              segTest(bx, midY, bx, y1) ||
              segTest(bx, y1, x1, y1);
      }
      if (hit)
        return i;
    }
    return -1;
  }
  // ── Logic evaluation ──────────────────────────────────────────────────
  void evaluate()
  {
    for (auto &n : nodes_)
      std::fill(n.inputVals.begin(), n.inputVals.end(), false);

    for (int iter = 0; iter < 30; ++iter)
    {
      bool changed = false;
      for (auto &w : wires_)
      {
        CircuitNode *src = findNode(w.fromNodeId);
        CircuitNode *dst = findNode(w.toNodeId);
        if (!src || !dst)
          continue;
        if (w.toPortIndex < int(dst->inputVals.size()))
        {
          bool v = src->value;
          if (dst->inputVals[w.toPortIndex] != v)
          {
            dst->inputVals[w.toPortIndex] = v;
            changed = true;
          }
        }
      }
      for (auto &n : nodes_)
      {
        bool prev = n.value;
        switch (n.type)
        {
        case CircuitNodeType::AND:
          n.value = n.inputVals.size() >= 2 && n.inputVals[0] && n.inputVals[1];
          break;
        case CircuitNodeType::OR:
          n.value = n.inputVals.size() >= 2 && (n.inputVals[0] || n.inputVals[1]);
          break;
        case CircuitNodeType::NOT:
          n.value = n.inputVals.size() >= 1 && !n.inputVals[0];
          break;
        case CircuitNodeType::NAND:
          n.value = !(n.inputVals.size() >= 2 && n.inputVals[0] && n.inputVals[1]);
          break;
        case CircuitNodeType::NOR:
          n.value = !(n.inputVals.size() >= 2 && (n.inputVals[0] || n.inputVals[1]));
          break;
        case CircuitNodeType::XOR:
          n.value = n.inputVals.size() >= 2 && (n.inputVals[0] != n.inputVals[1]);
          break;
        case CircuitNodeType::XNOR:
          n.value = n.inputVals.size() >= 2 && (n.inputVals[0] == n.inputVals[1]);
          break;
        case CircuitNodeType::Input:
          break;
        case CircuitNodeType::Output:
          n.value = n.inputVals.size() >= 1 && n.inputVals[0];
          break;
        }
        if (prev != n.value)
          changed = true;
      }
      if (!changed)
        break;
    }
  }

  // ── Hit testing ───────────────────────────────────────────────────────
  CircuitNode *hitNode(float x, float y)
  {
    for (int i = int(nodes_.size()) - 1; i >= 0; --i)
    {
      auto &n = nodes_[i];
      if (x >= n.x && x <= n.x + n.w && y >= n.y && y <= n.y + n.h)
        return &n;
    }
    return nullptr;
  }

  CircuitNode *findNode(int id)
  {
    for (auto &n : nodes_)
      if (n.id == id)
        return &n;
    return nullptr;
  }

  const CircuitNode *findNodeConst(int id) const
  {
    for (auto &n : nodes_)
      if (n.id == id)
        return &n;
    return nullptr;
  }

  PortRef hitPort(float x, float y)
  {
    const float R = 10.f / currentZoom_;
    for (auto &n : nodes_)
    {
      if (n.outputCount() > 0)
      {
        Pt p = outputPortPos(n);
        if (std::hypot(x - p.x, y - p.y) < R)
          return {n.id, true, 0};
      }
      for (int i = 0; i < n.inputCount(); ++i)
      {
        Pt p = inputPortPos(n, i);
        if (std::hypot(x - p.x, y - p.y) < R)
          return {n.id, false, i};
      }
    }
    return {};
  }

  // ── Drawing helpers ───────────────────────────────────────────────────
  void drawGrid(Canvas2D &ctx, float vx, float vy, float vw, float vh) const
  {
    float targetLines = 6.f;
    float rawStep = vw / targetLines;
    float base = 64.f;
    float exp2 = std::floor(std::log2f(rawStep / base));
    float step = base * std::powf(2.f, exp2);
    float subStep = step / 4.f;
    float lw = 1.f / currentZoom_;

    if (subStep * currentZoom_ >= 8.f)
    {
      ctx.setStrokeColor(Color::fromRGBA(35, 35, 48, 255));
      ctx.setLineWidth(lw);
      for (float gx = std::floorf(vx / subStep) * subStep; gx <= vx + vw + subStep; gx += subStep)
      {
        ctx.beginPath();
        ctx.moveTo(gx, vy);
        ctx.lineTo(gx, vy + vh);
        ctx.stroke();
      }
      for (float gy = std::floorf(vy / subStep) * subStep; gy <= vy + vh + subStep; gy += subStep)
      {
        ctx.beginPath();
        ctx.moveTo(vx, gy);
        ctx.lineTo(vx + vw, gy);
        ctx.stroke();
      }
    }

    ctx.setStrokeColor(Color::fromRGBA(50, 50, 65, 255));
    ctx.setLineWidth(lw);
    for (float gx = std::floorf(vx / step) * step; gx <= vx + vw + step; gx += step)
    {
      ctx.beginPath();
      ctx.moveTo(gx, vy);
      ctx.lineTo(gx, vy + vh);
      ctx.stroke();
    }
    for (float gy = std::floorf(vy / step) * step; gy <= vy + vh + step; gy += step)
    {
      ctx.beginPath();
      ctx.moveTo(vx, gy);
      ctx.lineTo(vx + vw, gy);
      ctx.stroke();
    }
  }

  void drawAxes(Canvas2D &ctx, float vx, float vy, float vw, float vh) const
  {
    float lw = 1.f / currentZoom_;
    if (vy <= 0.f && 0.f <= vy + vh)
    {
      ctx.setStrokeColor(Color::fromRGBA(180, 60, 60, 160));
      ctx.setLineWidth(lw);
      ctx.beginPath();
      ctx.moveTo(vx, 0.f);
      ctx.lineTo(vx + vw, 0.f);
      ctx.stroke();
    }
    if (vx <= 0.f && 0.f <= vx + vw)
    {
      ctx.setStrokeColor(Color::fromRGBA(60, 180, 60, 160));
      ctx.setLineWidth(lw);
      ctx.beginPath();
      ctx.moveTo(0.f, vy);
      ctx.lineTo(0.f, vy + vh);
      ctx.stroke();
    }
  }

  void drawRubberBand(Canvas2D &ctx) const
  {
    float rx0 = rubberW_ < 0 ? rubberX_ + rubberW_ : rubberX_;
    float ry0 = rubberH_ < 0 ? rubberY_ + rubberH_ : rubberY_;
    float rw = std::abs(rubberW_), rh = std::abs(rubberH_);
    float lw = 1.f / currentZoom_;
    ctx.setFillColor(Color::fromRGBA(50, 120, 220, 20));
    ctx.fillRect(rx0, ry0, rw, rh);
    ctx.setStrokeColor(Color::fromRGBA(80, 150, 255, 180));
    ctx.setLineWidth(lw);
    ctx.strokeRect(rx0, ry0, rw, rh);
  }

  void drawNodeTextEdit(Canvas2D &ctx, const CircuitNode &n) const
  {
    float fs = 14.f / currentZoom_;
    char font[32];
    snprintf(font, sizeof(font), "%.0fpx sans", fs);
    ctx.setFont(font);
    ctx.setTextBaseline(TextBaseline::Top);

    float ty = n.y + n.h + 3.f / currentZoom_;
    float tw = ctx.measureText(textEdit_.text.c_str());

    // background pill
    float pad = 3.f / currentZoom_;
    ctx.setFillColor(Color::fromRGBA(20, 30, 55, 220));
    ctx.fillRect(n.x + n.w * 0.5f - tw * 0.5f - pad,
                 ty - pad,
                 tw + pad * 2.f + fs,
                 fs + pad * 2.f);

    // text
    ctx.setFillColor(Color::fromRGB(180, 210, 255));
    ctx.fillText(textEdit_.text.c_str(),
                 n.x + n.w * 0.5f - tw * 0.5f, ty);

    // blinking cursor
    if (textEdit_.cursorVisible)
    {
      ctx.setFillColor(Color::fromRGB(100, 180, 255));
      ctx.fillRect(n.x + n.w * 0.5f - tw * 0.5f + tw + 1.f / currentZoom_,
                   ty,
                   1.5f / currentZoom_,
                   fs);
    }

    // underline
    ctx.setFillColor(Color::fromRGBA(80, 160, 255, 120));
    ctx.fillRect(n.x + n.w * 0.5f - tw * 0.5f - pad,
                 ty + fs + 1.f / currentZoom_,
                 tw + pad * 2.f + fs,
                 1.f / currentZoom_);
  }

  // ── Gate color helpers ────────────────────────────────────────────────
  static Color gateBodyColor() { return Color::fromRGB(22, 22, 30); }
  static Color gateStrokeColor() { return Color::fromRGB(160, 160, 180); }
  static Color selStrokeColor() { return Color::fromRGB(80, 160, 255); }
  static Color portInColor() { return Color::fromRGB(50, 140, 220); }
  static Color portOutColor() { return Color::fromRGB(30, 180, 120); }
  static Color portHoverColor() { return Color::fromRGB(240, 160, 40); }
  static Color wireColor() { return Color::fromRGBA(100, 100, 120, 200); }
  static Color wireActiveColor() { return Color::fromRGB(60, 200, 120); }
  static Color wireSelColor() { return Color::fromRGB(80, 160, 255); }
  static Color labelColor() { return Color::fromRGBA(140, 140, 160, 200); }

  void drawNode(Canvas2D &ctx, const CircuitNode &n) const
  {
    float lw = (n.selected ? 2.f : 1.2f) / currentZoom_;
    Color sc = n.selected ? selStrokeColor() : gateStrokeColor();
    float fs = 16.f / currentZoom_;

    ctx.setFillColor(gateBodyColor());
    ctx.setStrokeColor(sc);
    ctx.setLineWidth(lw);

    switch (n.type)
    {
    case CircuitNodeType::AND:
      drawANDShape(ctx, n);
      break;
    case CircuitNodeType::OR:
      drawORShape(ctx, n);
      break;
    case CircuitNodeType::NOT:
      drawNOTShape(ctx, n);
      break;
    case CircuitNodeType::NAND:
      drawNANDShape(ctx, n);
      break;
    case CircuitNodeType::NOR:
      drawNORShape(ctx, n);
      break;
    case CircuitNodeType::XOR:
      drawXORShape(ctx, n);
      break;
    case CircuitNodeType::XNOR:
      drawXNORShape(ctx, n);
      break;
    case CircuitNodeType::Input:
      drawInputShape(ctx, n);
      break;
    case CircuitNodeType::Output:
      drawOutputShape(ctx, n);
      break;
    }

    // Gate label (below body)
    {
      const char *gateName = defaultName(n.type);
      // show custom label if set, otherwise the gate type name
      const std::string &displayLabel = n.label.empty()
                                            ? std::string(gateName ? gateName : "")
                                            : n.label;

      // skip for Input/Output (they have their own "IN"/"OUT" rendering)
      bool hasBuiltinLabel = (n.type == CircuitNodeType::Input ||
                              n.type == CircuitNodeType::Output);
      if (!displayLabel.empty() && !hasBuiltinLabel)
      {
        char font[32];
        snprintf(font, sizeof(font), "%.0fpx sans", fs);
        ctx.setFont(font);
        ctx.setTextBaseline(TextBaseline::Top);
        // highlight label area if this node is being edited
        bool editing = textEditing_ && textEdit_.nodeId == n.id;
        ctx.setFillColor(editing
                             ? Color::fromRGBA(80, 160, 255, 220)
                             : labelColor());
        float tw = ctx.measureText(displayLabel.c_str());
        ctx.fillText(displayLabel.c_str(),
                     n.x + n.w * 0.5f - tw * 0.5f,
                     n.y + n.h + 3.f / currentZoom_);
      }

      // draw live text edit cursor + text when editing
      if (textEditing_ && textEdit_.nodeId == n.id)
        drawNodeTextEdit(ctx, n);
    }

    // Input stubs + ports
    for (int i = 0; i < n.inputCount(); ++i)
    {
      Pt p = inputPortPos(n, i);
      ctx.setStrokeColor(gateStrokeColor());
      ctx.setLineWidth(1.f / currentZoom_);
      ctx.beginPath();
      ctx.moveTo(p.x - 6.f / currentZoom_, p.y);
      ctx.lineTo(p.x, p.y);
      ctx.stroke();
      float pr = 4.f / currentZoom_;
      bool hov = (mode_ == CircuitMode::Wire &&
                  std::hypot(curX_ - p.x, curY_ - p.y) < 10.f / currentZoom_);
      ctx.setFillColor(hov ? portHoverColor() : portInColor());
      ctx.fillCircle(p.x, p.y, pr);
    }
    // Output stub + port
    if (n.outputCount() > 0)
    {
      Pt p = outputPortPos(n);
      ctx.setStrokeColor(gateStrokeColor());
      ctx.setLineWidth(1.f / currentZoom_);
      ctx.beginPath();
      ctx.moveTo(p.x, p.y);
      ctx.lineTo(p.x + 6.f / currentZoom_, p.y);
      ctx.stroke();
      float pr = 4.f / currentZoom_;
      bool hov = (mode_ == CircuitMode::Wire &&
                  std::hypot(curX_ - p.x, curY_ - p.y) < 10.f / currentZoom_);
      ctx.setFillColor(hov ? portHoverColor() : portOutColor());
      ctx.fillCircle(p.x, p.y, pr);
    }
  }

  // ── Gate shapes ───────────────────────────────────────────────────────
  void drawANDShape(Canvas2D &ctx, const CircuitNode &n) const
  {
    float x = n.x, y = n.y, w = n.w, h = n.h;
    float cx = x + w * 0.48f;
    ctx.beginPath();
    ctx.moveTo(x, y);
    ctx.lineTo(cx, y);
    ctx.bezierCurveTo(x + w, y, x + w, y + h, cx, y + h);
    ctx.lineTo(x, y + h);
    ctx.closePath();
    ctx.fill();
    ctx.stroke();
  }

  void drawORShape(Canvas2D &ctx, const CircuitNode &n) const
  {
    float x = n.x, y = n.y, w = n.w, h = n.h;
    ctx.beginPath();
    ctx.moveTo(x, y);
    ctx.quadraticCurveTo(x + w * 0.5f, y, x + w, y + h * 0.5f);
    ctx.quadraticCurveTo(x + w * 0.5f, y + h, x, y + h);
    ctx.quadraticCurveTo(x + w * 0.22f, y + h * 0.5f, x, y);
    ctx.closePath();
    ctx.fill();
    ctx.stroke();
  }

  void drawNOTShape(Canvas2D &ctx, const CircuitNode &n) const
  {
    float x = n.x, y = n.y, w = n.w, h = n.h;
    float bubR = 4.f / currentZoom_;
    float tipX = x + w - bubR * 2.f;
    ctx.beginPath();
    ctx.moveTo(x, y);
    ctx.lineTo(tipX, y + h * 0.5f);
    ctx.lineTo(x, y + h);
    ctx.closePath();
    ctx.fill();
    ctx.stroke();
    ctx.beginPath();
    ctx.arc(tipX + bubR, y + h * 0.5f, bubR, 0.f, float(M_PI * 2));
    ctx.fill();
    ctx.stroke();
  }

  // NAND = AND body + bubble on output
  void drawNANDShape(Canvas2D &ctx, const CircuitNode &n) const
  {
    float x = n.x, y = n.y, w = n.w, h = n.h;
    float bubR = 4.f / currentZoom_;
    // Draw AND body slightly narrower to leave room for bubble
    float bodyW = w - bubR * 2.f;
    float cx = x + bodyW * 0.48f;
    ctx.beginPath();
    ctx.moveTo(x, y);
    ctx.lineTo(cx, y);
    ctx.bezierCurveTo(x + bodyW, y, x + bodyW, y + h, cx, y + h);
    ctx.lineTo(x, y + h);
    ctx.closePath();
    ctx.fill();
    ctx.stroke();
    // bubble
    ctx.beginPath();
    ctx.arc(x + bodyW + bubR, y + h * 0.5f, bubR, 0.f, float(M_PI * 2));
    ctx.fill();
    ctx.stroke();
  }

  // NOR = OR body + bubble on output
  void drawNORShape(Canvas2D &ctx, const CircuitNode &n) const
  {
    float x = n.x, y = n.y, w = n.w, h = n.h;
    float bubR = 4.f / currentZoom_;
    float bodyW = w - bubR * 2.f;
    ctx.beginPath();
    ctx.moveTo(x, y);
    ctx.quadraticCurveTo(x + bodyW * 0.5f, y, x + bodyW, y + h * 0.5f);
    ctx.quadraticCurveTo(x + bodyW * 0.5f, y + h, x, y + h);
    ctx.quadraticCurveTo(x + bodyW * 0.22f, y + h * 0.5f, x, y);
    ctx.closePath();
    ctx.fill();
    ctx.stroke();
    // bubble
    ctx.beginPath();
    ctx.arc(x + bodyW + bubR, y + h * 0.5f, bubR, 0.f, float(M_PI * 2));
    ctx.fill();
    ctx.stroke();
  }

  // XOR = OR body with an extra curved line on the input side
  void drawXORShape(Canvas2D &ctx, const CircuitNode &n) const
  {
    float x = n.x, y = n.y, w = n.w, h = n.h;
    float indent = w * 0.10f;
    // Main OR body shifted right by indent
    ctx.beginPath();
    ctx.moveTo(x + indent, y);
    ctx.quadraticCurveTo(x + indent + w * 0.5f, y, x + w, y + h * 0.5f);
    ctx.quadraticCurveTo(x + indent + w * 0.5f, y + h, x + indent, y + h);
    ctx.quadraticCurveTo(x + indent + w * 0.22f, y + h * 0.5f, x + indent, y);
    ctx.closePath();
    ctx.fill();
    ctx.stroke();
    // Extra arc (XOR stripe)
    ctx.beginPath();
    ctx.moveTo(x, y);
    ctx.quadraticCurveTo(x + w * 0.22f, y + h * 0.5f, x, y + h);
    ctx.stroke();
  }

  // XNOR = XOR body + bubble on output
  void drawXNORShape(Canvas2D &ctx, const CircuitNode &n) const
  {
    float x = n.x, y = n.y, w = n.w, h = n.h;
    float bubR = 4.f / currentZoom_;
    float indent = w * 0.10f;
    float bodyW = w - bubR * 2.f;
    // Main OR body
    ctx.beginPath();
    ctx.moveTo(x + indent, y);
    ctx.quadraticCurveTo(x + indent + bodyW * 0.5f, y, x + bodyW, y + h * 0.5f);
    ctx.quadraticCurveTo(x + indent + bodyW * 0.5f, y + h, x + indent, y + h);
    ctx.quadraticCurveTo(x + indent + bodyW * 0.22f, y + h * 0.5f, x + indent, y);
    ctx.closePath();
    ctx.fill();
    ctx.stroke();
    // XOR stripe
    ctx.beginPath();
    ctx.moveTo(x, y);
    ctx.quadraticCurveTo(x + bodyW * 0.22f, y + h * 0.5f, x, y + h);
    ctx.stroke();
    // bubble
    ctx.beginPath();
    ctx.arc(x + bodyW + bubR, y + h * 0.5f, bubR, 0.f, float(M_PI * 2));
    ctx.fill();
    ctx.stroke();
  }

  void drawInputShape(Canvas2D &ctx, const CircuitNode &n) const
  {
    float x = n.x, y = n.y, w = n.w, h = n.h;
    float r = 4.f / currentZoom_;
    ctx.setFillColor(n.value ? Color::fromRGBA(20, 40, 28, 255) : Color::fromRGBA(18, 18, 24, 255));
    ctx.setStrokeColor(n.selected ? selStrokeColor()
                       : n.value  ? Color::fromRGB(40, 200, 100)
                                  : Color::fromRGBA(80, 80, 95, 255));
    ctx.setLineWidth((n.selected ? 2.f : 1.5f) / currentZoom_);
    ctx.fillRoundedRect(x, y, w, h, r);
    ctx.strokeRoundedRect(x, y, w, h, r);
    float dotR = h * 0.22f;
    ctx.setFillColor(n.value ? Color::fromRGB(40, 210, 110) : Color::fromRGB(45, 45, 55));
    ctx.fillCircle(x + w * 0.5f, y + h * 0.5f, dotR);
    float fs = 12.f / currentZoom_;
    char font[32];
    snprintf(font, sizeof(font), "bold %.0fpx sans", fs);
    ctx.setFont(font);
    ctx.setTextBaseline(TextBaseline::Top);
    ctx.setFillColor(n.value ? Color::fromRGBA(40, 210, 110, 180) : Color::fromRGBA(80, 80, 95, 200));
    float tw = ctx.measureText("IN");
    ctx.fillText("IN", x + w * 0.5f - tw * 0.5f, y + h + 2.f / currentZoom_);
  }

  void drawOutputShape(Canvas2D &ctx, const CircuitNode &n) const
  {
    float x = n.x, y = n.y, w = n.w, h = n.h;
    float r = 4.f / currentZoom_;
    ctx.setFillColor(n.value ? Color::fromRGBA(20, 40, 28, 255) : Color::fromRGBA(18, 18, 24, 255));
    ctx.setStrokeColor(n.selected ? selStrokeColor()
                       : n.value  ? Color::fromRGB(40, 200, 100)
                                  : Color::fromRGBA(80, 80, 95, 255));
    ctx.setLineWidth((n.selected ? 2.f : 1.5f) / currentZoom_);
    ctx.fillRoundedRect(x, y, w, h, r);
    ctx.strokeRoundedRect(x, y, w, h, r);
    float dotR = h * 0.22f;
    ctx.setFillColor(n.value ? Color::fromRGB(40, 210, 110) : Color::fromRGB(45, 45, 55));
    ctx.fillCircle(x + w * 0.5f, y + h * 0.5f, dotR);
    float fs = 12.f / currentZoom_;
    char font[32];
    snprintf(font, sizeof(font), "bold %.0fpx sans", fs);
    ctx.setFont(font);
    ctx.setTextBaseline(TextBaseline::Top);
    ctx.setFillColor(n.value ? Color::fromRGBA(40, 210, 110, 180) : Color::fromRGBA(80, 80, 95, 200));
    float tw = ctx.measureText("OUT");
    ctx.fillText("OUT", x + w * 0.5f - tw * 0.5f, y + h + 2.f / currentZoom_);
  }

  // ── Wire drawing ──────────────────────────────────────────────────────
  void drawWire(Canvas2D &ctx, const CircuitWire &w) const
  {
    const CircuitNode *src = findNodeConst(w.fromNodeId);
    const CircuitNode *dst = findNodeConst(w.toNodeId);
    if (!src || !dst)
      return;

    Pt p0 = outputPortPos(*src);
    Pt p1 = inputPortPos(*dst, w.toPortIndex);
    drawOrthogonalWire(ctx, p0.x, p0.y, p1.x, p1.y, src->value, false, w.selected);
  }

  void drawWirePreview(Canvas2D &ctx) const
  {
    const CircuitNode *sn = nullptr;
    for (auto &n : nodes_)
      if (n.id == wireStart_.nodeId)
      {
        sn = &n;
        break;
      }
    if (!sn)
      return;

    float x0, y0;
    if (wireStart_.isOutput)
    {
      Pt p = outputPortPos(*sn);
      x0 = p.x;
      y0 = p.y;
    }
    else
    {
      Pt p = inputPortPos(*sn, wireStart_.portIdx);
      x0 = p.x;
      y0 = p.y;
    }
    drawOrthogonalWire(ctx, x0, y0, curX_, curY_, false, true, false);
  }

  void drawOrthogonalWire(Canvas2D &ctx,
                          float x0, float y0, float x1, float y1,
                          bool active, bool preview, bool selected) const
  {
    float lw;
    Color c;
    if (selected)
    {
      lw = 2.5f / currentZoom_;
      c = wireSelColor();
    }
    else if (preview)
    {
      lw = 1.5f / currentZoom_;
      c = Color::fromRGBA(220, 160, 40, 200);
    }
    else if (active)
    {
      lw = 2.f / currentZoom_;
      c = wireActiveColor();
    }
    else
    {
      lw = 1.5f / currentZoom_;
      c = wireColor();
    }

    ctx.setStrokeColor(c);
    ctx.setLineWidth(lw);

    const float stub = 24.f / currentZoom_;
    const float cr = 6.f / currentZoom_; // bend radius — feels right at all zooms

    float ax = x0 + stub;
    float bx = x1 - stub;

    // Build the polyline points
    std::vector<std::pair<float, float>> pts;
    pts.reserve(6);

    if (ax <= bx)
    {
      float midX = (ax + bx) * 0.5f;
      pts = {{x0, y0}, {ax, y0}, {midX, y0}, {midX, y1}, {bx, y1}, {x1, y1}};
    }
    else
    {
      float midY = (y0 + y1) * 0.5f;
      pts = {{x0, y0}, {ax, y0}, {ax, midY}, {bx, midY}, {bx, y1}, {x1, y1}};
    }

    ctx.beginPath();
    ctx.moveTo(pts[0].first, pts[0].second);
    roundedPolyline(ctx, pts, cr);
    ctx.stroke();

    // Selection halo — same path, thicker + translucent
    if (selected)
    {
      ctx.setStrokeColor(Color::fromRGBA(80, 160, 255, 40));
      ctx.setLineWidth(8.f / currentZoom_);
      ctx.beginPath();
      ctx.moveTo(pts[0].first, pts[0].second);
      roundedPolyline(ctx, pts, cr);
      ctx.stroke();
    }
  }
  // ── Undo helpers ──────────────────────────────────────────────────────
  void pushUndo()
  {
    undoNodes_.push_back({nodes_, wires_});
    if (undoNodes_.size() > 50)
      undoNodes_.erase(undoNodes_.begin());
    redoNodes_.clear();
  }
  void notify()
  {
    if (onChanged)
      onChanged();
  }

  // ── State ─────────────────────────────────────────────────────────────
  std::vector<CircuitNode> nodes_;
  std::vector<CircuitWire> wires_;
  int nextId_ = 1;

  using Snapshot = std::pair<std::vector<CircuitNode>, std::vector<CircuitWire>>;
  std::vector<Snapshot> undoNodes_, redoNodes_;

  struct ClipboardEntry
  {
    CircuitNodeType type;
    float x, y; // relative to clipboard centroid
    bool value;
    std::string label;
    int clipId; // stable ID used only inside the clipboard
                // to match wire endpoints
  };
  struct ClipboardWire
  {
    int fromClipId;
    int toClipId;
    int toPortIndex;
  };

  std::vector<ClipboardEntry> clipboard_;
  std::vector<ClipboardWire> clipboardWires_;
  float pasteOffsetAccum_ = 0.f; // grows by 40 each paste, resets on new copy

  float mouseDownX_ = 0, mouseDownY_ = 0;
  float curX_ = 0, curY_ = 0;
  bool dragging_ = false;
  float dragStartX_ = 0, dragStartY_ = 0;
  bool rubberBand_ = false;
  float rubberX_ = 0, rubberY_ = 0, rubberW_ = 0, rubberH_ = 0;

  PortRef wireStart_;

  int w_ = 1280, h_ = 800;
};

// ============================================================================
// CircuitApp
// ============================================================================

class CircuitApp : public Widget
{
  std::shared_ptr<CanvasWidget> canvas_;
  std::shared_ptr<CircuitSurface> surface_;

  State<std::string> zoomLabel_{"100%"};
  State<std::string> cursorLabel_{"0.0,  0.0"};
  State<std::string> modeLabel_{"Select"};

  State<std::string> selLabel_{""};

  State<bool> ttVisible_{false};
  State<std::string> ttContent_{""};

  std::vector<std::shared_ptr<ButtonWidget>> modeBtns_;

  static constexpr Color kActiveBg = {40, 100, 220, 255};
  static constexpr Color kInactiveBg = {30, 30, 38, 255};
  static constexpr Color kSidebarBg = {18, 18, 24, 255};
  static constexpr Color kToolbarBg = {14, 14, 20, 255};

  void rebuildTruthTable()
  {
    if (!surface_)
      return;
    auto tt = surface_->computeTruthTable();

    if (tt.inputLabels.empty() || tt.outputLabels.empty())
    {
      ttContent_.set("Add IN and OUT\nnodes to generate\na truth table.");
      return;
    }

    std::ostringstream o;

    // Column widths — at least 2 chars, padded to label width
    auto colW = [](const std::string &lbl)
    {
      return std::max(2, (int)lbl.size());
    };

    // Header
    for (auto &lbl : tt.inputLabels)
    {
      int w = colW(lbl);
      o << std::left << std::setw(w) << lbl << " ";
    }
    o << "| ";
    for (auto &lbl : tt.outputLabels)
    {
      int w = colW(lbl);
      o << std::left << std::setw(w) << lbl << " ";
    }
    o << "\n";

    // Separator
    for (auto &lbl : tt.inputLabels)
      for (int i = 0; i <= colW(lbl); ++i)
        o << "-";
    o << "+-";
    for (auto &lbl : tt.outputLabels)
      for (int i = 0; i <= colW(lbl); ++i)
        o << "-";
    o << "\n";

    // Rows
    int ni = (int)tt.inputLabels.size();
    for (auto &row : tt.rows)
    {
      for (int i = 0; i < ni; ++i)
      {
        int w = colW(tt.inputLabels[i]);
        o << std::left << std::setw(w) << (row[i] ? "1" : "0") << " ";
      }
      o << "| ";
      for (int i = 0; i < (int)tt.outputLabels.size(); ++i)
      {
        int w = colW(tt.outputLabels[i]);
        o << std::left << std::setw(w)
          << (row[ni + i] ? "1" : "0") << " ";
      }
      o << "\n";
    }

    ttContent_.set(o.str());
  }

  void setMode(CircuitMode m, int btnIdx)
  {
    if (surface_)
      surface_->mode_ = m;
    for (int i = 0; i < int(modeBtns_.size()); i++)
      if (modeBtns_[i])
        modeBtns_[i]->setBackgroundColor(i == btnIdx ? kActiveBg : kInactiveBg);
    const char *labels[] = {"Select", "Wire", "Delete"};
    modeLabel_.set(labels[btnIdx]);
    if (canvas_)
      canvas_->redraw();
  }

  struct GateDef
  {
    const char *label;
    CircuitNodeType type;
    bool divider = false;
  };

public:
  WidgetPtr build() override
  {
    canvas_ = std::make_shared<CanvasWidget>();
    canvas_->setViewportEnabled(true);
    canvas_->setScrollbarsEnabled(false);
    static constexpr int kWorldSize = 1 << 24;
    canvas_->setCanvasSize(kWorldSize, kWorldSize);

    canvas_->onGLResize = [this](int w, int h)
    {
      if (surface_)
      {
        surface_->viewW_ = float(w);
        surface_->viewH_ = float(h);
        // Pass GL context to surface for export
        if (canvas_->canvasGL_)
          surface_->setCanvasGL(canvas_->canvasGL_);
      }
    };
    canvas_->onViewportChanged = [this](float zoom)
    {
      if (surface_)
      {
        surface_->currentZoom_ = zoom;
        surface_->viewOffsetX_ = canvas_->viewport().offsetX();
        surface_->viewOffsetY_ = canvas_->viewport().offsetY();
      }
      char buf[16];
      snprintf(buf, sizeof(buf), "%.0f%%", zoom * 100.f);
      zoomLabel_.set(buf);
      canvas_->redraw();
    };

    surface_ = canvas_->setSurface<CircuitSurface>();

    std::weak_ptr<CircuitSurface> ws = surface_;
    std::weak_ptr<CanvasWidget> wc = canvas_;
    std::weak_ptr<CircuitSurface> wsSave = surface_;
    std::weak_ptr<CircuitSurface> wsOpen = surface_;

    std::weak_ptr<CircuitSurface> wsVerilog = surface_;
    std::weak_ptr<CircuitSurface> wsVHDL = surface_;

    std::weak_ptr<CircuitSurface> wsPNG = surface_;
    std::weak_ptr<CircuitSurface> wsSVG = surface_;

    surface_->onChanged = [this]
    {
      canvas_->redraw();
      // Update selection count label
      int cnt = surface_->selectedCount();
      selLabel_.set(cnt > 0 ? std::to_string(cnt) + " selected" : "");
      if (ttVisible_.get())
        rebuildTruthTable();
    };
    surface_->onCursorMoved = [this](float wx, float wy)
    {
      if (surface_)
      {
        surface_->viewOffsetX_ = canvas_->viewport().offsetX();
        surface_->viewOffsetY_ = canvas_->viewport().offsetY();
      }
      char buf[48];
      snprintf(buf, sizeof(buf), "%.1f,  %.1f", wx, wy);
      cursorLabel_.set(buf);
      canvas_->redraw();
    };

    // ── Sidebar: gate / signal placers ────────────────────────────────
    const std::vector<GateDef> gateDefs = {
        {"AND", CircuitNodeType::AND},
        {"OR", CircuitNodeType::OR},
        {"NOT", CircuitNodeType::NOT},
        {"", CircuitNodeType::AND, true}, // divider
        {"NAND", CircuitNodeType::NAND},
        {"NOR", CircuitNodeType::NOR},
        {"XOR", CircuitNodeType::XOR},
        {"XNOR", CircuitNodeType::XNOR},
        {"", CircuitNodeType::AND, true}, // divider
        {"IN", CircuitNodeType::Input},
        {"OUT", CircuitNodeType::Output},
    };

    auto sideCol = Column({});
    sideCol->setSpacing(3)->setPadding(6);

    for (auto &gd : gateDefs)
    {
      if (gd.divider)
      {
        sideCol->addChild(Container()->setWidth(86)->setHeight(1)->setBackgroundColor(Color::fromRGBA(60, 60, 75, 255)));
        sideCol->addChild(SizedBox(0, 3));
        continue;
      }

      CircuitNodeType t = gd.type;
      const char *lb = gd.label;

      auto btn = Button(lb)
                     ->setHeight(34)
                     ->setWidth(68)
                     ->setBackgroundColor(kInactiveBg)
                     ->setOnClick([this, ws, wc, t]()
                                  {
                       if (auto s = ws.lock())
                       {
                         float vx = s->viewOffsetX_ + s->viewW_ / (s->currentZoom_ * 2.f);
                         float vy = s->viewOffsetY_ + s->viewH_ / (s->currentZoom_ * 2.f);
                         static int placeCtr = 0; ++placeCtr;
                         float jx = float((placeCtr % 7) * 18 - 54);
                         float jy = float((placeCtr % 5) * 18 - 36);
                         s->placeNode(t, vx + jx - 40.f, vy + jy - 20.f);
                       }
                       if (auto c = wc.lock()) c->redraw(); });
      sideCol->addChild(btn);
    }

    auto sidebar = Container(Column({Container(sideCol)->setHeight(420)}))->setWidth(86)->setBackgroundColor(kSidebarBg);

    auto verilogPicker = FilePicker("Verilog")
                             ->setMode(FilePickerMode::Save)
                             ->setTitle("Export Verilog")
                             ->setDefaultFilename("circuit.v")
                             ->setDefaultExtension("v")
                             ->addFilter("Verilog", {"*.v"})
                             ->setShowPath(false)
                             ->setHeight(26)
                             ->setOnChanged([wsVerilog](const std::string &path)
                                            {
        if (auto s = wsVerilog.lock()) {
            std::ofstream f(path, std::ios::out | std::ios::trunc);
            if (f) f << s->toVerilog();
        } });

    auto vhdlPicker = FilePicker("VHDL")
                          ->setMode(FilePickerMode::Save)
                          ->setTitle("Export VHDL")
                          ->setDefaultFilename("circuit.vhd")
                          ->setDefaultExtension("vhd")
                          ->addFilter("VHDL", {"*.vhd", "*.vhdl"})
                          ->setShowPath(false)
                          ->setHeight(26)
                          ->setOnChanged([wsVHDL](const std::string &path)
                                         {
        if (auto s = wsVHDL.lock()) {
            std::ofstream f(path, std::ios::out | std::ios::trunc);
            if (f) f << s->toVHDL();
        } });

    // Save button — opens native "Save As" dialog
    auto savePicker = FilePicker("Save")
                          ->setMode(FilePickerMode::Save)
                          ->setTitle("Save Circuit")
                          ->setDefaultFilename("circuit.json")
                          ->setDefaultExtension("json")
                          ->addFilter("Circuit JSON", {"*.json"})
                          ->addFilter("All Files", {"*.*"})
                          ->setShowPath(false)
                          ->setHeight(26)
                          ->setOnChanged([wsSave](const std::string &path)
                                         {
        if (auto s = wsSave.lock())
            s->saveToFile(path); });

    // Open button — opens native "Open" dialog
    auto openPicker = FilePicker("Open")
                          ->setMode(FilePickerMode::Open)
                          ->setTitle("Open Circuit")
                          ->setDefaultExtension("json")
                          ->addFilter("Circuit JSON", {"*.json"})
                          ->addFilter("All Files", {"*.*"})
                          ->setShowPath(false)
                          ->setHeight(26)
                          ->setOnChanged([wsOpen, wc](const std::string &path)
                                         {
        if (auto s = wsOpen.lock())
            s->loadFromFile(path);
        if (auto c = wc.lock())
            c->redraw(); });

    auto pngPicker = FilePicker("PNG")
                         ->setMode(FilePickerMode::Save)
                         ->setTitle("Export PNG")
                         ->setDefaultFilename("circuit.png")
                         ->setDefaultExtension("png")
                         ->addFilter("PNG Image", {"*.png"})
                         ->setShowPath(false)
                         ->setHeight(26)
                         ->setOnChanged([wsPNG](const std::string &path)
                                        {
        if (auto s = wsPNG.lock()) s->exportPNG(path); });

    auto svgPicker = FilePicker("SVG")
                         ->setMode(FilePickerMode::Save)
                         ->setTitle("Export SVG")
                         ->setDefaultFilename("circuit.svg")
                         ->setDefaultExtension("svg")
                         ->addFilter("SVG Vector", {"*.svg"})
                         ->setShowPath(false)
                         ->setHeight(26)
                         ->setOnChanged([wsSVG](const std::string &path)
                                        {
        if (auto s = wsSVG.lock()) s->exportSVG(path); });

    auto dupBtn = Button("Duplicate")
                      ->setHeight(26)
                      ->setOnClick([ws, wc]
                                   {
        if (auto s = ws.lock()) s->duplicateSelected();
        if (auto c = wc.lock()) c->redraw(); });

    auto copyBtn = Button("Copy")
                       ->setHeight(26)
                       ->setOnClick([ws, wc]
                                    {
        if (auto s = ws.lock()) s->copySelected(); });

    auto pasteBtn = Button("Paste")
                        ->setHeight(26)
                        ->setOnClick([ws, wc]
                                     {
        if (auto s = ws.lock()) s->pasteClipboard();
        if (auto c = wc.lock()) c->redraw(); });

    // ── Mode buttons ──────────────────────────────────────────────────
    struct ModeInfo
    {
      const char *lbl;
      CircuitMode m;
    };
    const std::vector<ModeInfo> modes = {
        {"Select", CircuitMode::Select},
        {"Wire", CircuitMode::Wire},
        {"Delete", CircuitMode::Delete},
    };
    modeBtns_.resize(modes.size());
    auto modeRow = Row({});
    modeRow->setSpacing(2)->setCrossAxisAlignment(CrossAxisAlignment::Center);
    for (int i = 0; i < int(modes.size()); i++)
    {
      auto &mi = modes[i];
      auto btn = Button(mi.lbl)
                     ->setHeight(26)
                     ->setBackgroundColor(i == 0 ? kActiveBg : kInactiveBg)
                     ->setOnClick([this, i, m = mi.m]
                                  { setMode(m, i); });
      modeBtns_[i] = btn;
      modeRow->addChild(btn);
    }

    // ── Toolbar ───────────────────────────────────────────────────────
    auto undoBtn = Button("↩")->setHeight(26)->setOnClick([this, ws, wc]
                                                          { if (auto s = ws.lock()) s->undo(); if (auto c = wc.lock()) c->redraw(); });
    auto redoBtn = Button("↪")->setHeight(26)->setOnClick([this, ws, wc]
                                                          { if (auto s = ws.lock()) s->redo(); if (auto c = wc.lock()) c->redraw(); });
    auto clrBtn = Button("Clear")->setHeight(26)->setOnClick([this, ws, wc]
                                                             { if (auto s = ws.lock()) s->clear(); if (auto c = wc.lock()) c->redraw(); });
    auto fitBtn = Button("Fit")->setHeight(26)->setOnClick([wc]
                                                           { if (auto c = wc.lock()) { c->viewport().fitToView(); c->redraw(); } });
    auto rstBtn = Button("1:1")->setHeight(26)->setOnClick([wc]

                                                           { if (auto c = wc.lock()) { c->viewport().resetZoom(); c->redraw(); } });

    auto ttBtn = Button("Truth Table")
                     ->setHeight(26)
                     ->setOnClick([this]
                                  {
        bool show = !ttVisible_.get();
        ttVisible_.set(show);
        if (show) rebuildTruthTable(); });

    auto toolbar = Container(
                       Row({
                               Text("Circuit")->setFontSize(14)->setTextColor(Color::fromRGB(160, 160, 180)),
                               SizedBox(8, 0),
                               savePicker,
                               SizedBox(2, 0),
                               openPicker,
                               SizedBox(2, 0),
                               verilogPicker,
                               SizedBox(2, 0),
                               vhdlPicker,
                               SizedBox(2, 0),
                               pngPicker,
                               SizedBox(2, 0),
                               svgPicker,
                               SizedBox(10, 0),
                               undoBtn,
                               SizedBox(2, 0),
                               redoBtn,
                               SizedBox(8, 0),
                               copyBtn,
                               SizedBox(2, 0),
                               pasteBtn,
                               SizedBox(2, 0),
                               dupBtn,
                               SizedBox(2, 0),
                               clrBtn,
                               SizedBox(8, 0),
                               fitBtn,
                               SizedBox(2, 0),
                               rstBtn,
                               SizedBox(8, 0),
                               ttBtn,
                               SizedBox(12, 0),
                               Container(modeRow)->setHeight(30),
                           })
                           ->setPadding(7)
                           ->setSpacing(3)
                           ->setCrossAxisAlignment(CrossAxisAlignment::Center))
                       ->setHeight(40)
                       ->setBackgroundColor(kToolbarBg);

    // ── Truth table panel (shown/hidden with Conditional) ─────────────────
    auto ttRefreshBtn = Button("Refresh")
                            ->setHeight(22)
                            ->setOnClick([this]
                                         { rebuildTruthTable(); });

    auto ttPanel =
        Container(
            Column({
                // Header bar
                Container(
                    Row({
                            Text("Truth Table")
                                ->setFontSize(11)
                                ->setTextColor(Color::fromRGB(160, 160, 180)),
                            SizedBox(6, 0),
                            ttRefreshBtn,
                        })
                        ->setPadding(6)
                        ->setSpacing(2)
                        ->setCrossAxisAlignment(CrossAxisAlignment::Center))
                    ->setHeight(32)
                    ->setBackgroundColor(Color::fromRGB(14, 14, 20)),

                // Divider
                Container()
                    ->setHeight(1)
                    ->setBackgroundColor(Color::fromRGBA(60, 60, 75, 255)),

                // Content — monospaced text, scrollable
                Expanded(
                    ListView({Text(ttContent_, [](const std::string &s)
                                   { return s; })
                                  ->setFontSize(11)
                                  ->setFontFamily("Consolas")
                                  ->setTextColor(Color::fromRGB(160, 210, 160))
                                  ->setPadding(8)})),
            }))
            ->setWidth(210)
            ->setBackgroundColor(Color::fromRGB(13, 13, 18))
            ->setBorderColor(Color::fromRGBA(60, 60, 75, 255))
            ->setBorderWidth(1);

    // Conditional wraps the panel — collapses to zero width when hidden
    auto ttConditional = Conditional(ttVisible_)
                             ->Then([ttPanel]()
                                    { return ttPanel; })
                             ->Else([]()
                                    { return Container()->setHeight(0)->setWidth(0); });

    // ── Status bar ────────────────────────────────────────────────────
    auto statusBar = Container(
                         Row({
                                 Text(modeLabel_, [](const std::string &s)
                                      { return "Mode: " + s; })
                                     ->setFontSize(11)
                                     ->setTextColor(Color::fromRGB(120, 120, 140))
                                     ->setMinWidth(90),
                                 SizedBox(14, 0),
                                 Text(selLabel_, [](const std::string &s)
                                      { return s; })
                                     ->setFontSize(11)
                                     ->setTextColor(Color::fromRGB(80, 160, 255))
                                     ->setMinWidth(80),
                                 SizedBox(14, 0),
                                 Text(cursorLabel_, [](const std::string &s)
                                      { return "XY " + s; })
                                     ->setFontSize(11)
                                     ->setTextColor(Color::fromRGB(70, 190, 110)),
                                 SizedBox(14, 0),
                                 Text(zoomLabel_, [](const std::string &s)
                                      { return "Zoom " + s; })
                                     ->setFontSize(11)
                                     ->setTextColor(Color::fromRGB(120, 120, 140)),
                                 SizedBox(14, 0),
                                 Text("Mid/Space: pan · Ctrl+scroll: zoom · Click IN: toggle · Select+Delete: remove wire")
                                     ->setFontSize(10)
                                     ->setTextColor(Color::fromRGB(60, 60, 75)),
                             })
                             ->setPadding(5)
                             ->setSpacing(0)
                             ->setCrossAxisAlignment(CrossAxisAlignment::Center))
                         ->setHeight(24)
                         ->setBackgroundColor(Color::fromRGB(10, 10, 14));

    return Scaffold(nullptr,
                    Expanded(Column({
                        toolbar,
                        Expanded(Row({sidebar, Expanded(canvas_), ttConditional})),
                        statusBar,
                    })),
                    nullptr, nullptr);
  }
};

// ── Entry point ───────────────────────────────────────────────────────────────

WidgetPtr createApp(FluxUI *app)
{
  return FluxApp(
      "Circuit Designer",
      std::make_shared<CircuitApp>(),
      AppTheme::dark(),
      false,
      1280, 800,
      false, true);
}