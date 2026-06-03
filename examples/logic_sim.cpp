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
#include <set>

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
  Output,
  Clock,
  SevenSegment
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

  int inputCount_ = 2;       // user-configurable, default 2
  float clockHz_ = 1.f;      // toggle frequency in Hz
  double clockAccum_ = 0.0;  // accumulated time since last toggle
  bool clockRunning_ = true; // can be paused per-node

  // per-input driven values (size == inputCount())
  std::vector<bool> inputVals;

  std::string label;

  // drag helpers
  float dragOX = 0, dragOY = 0;

  int inputCount() const
  {
    switch (type)
    {
    case CircuitNodeType::NOT:
      return 1;
    case CircuitNodeType::Input:
      return 0;
    case CircuitNodeType::Output:
      return 1;
    case CircuitNodeType::Clock:
      return 0;
    case CircuitNodeType::SevenSegment:
      return 8;
    // Multi-input capable gates:
    case CircuitNodeType::AND:
    case CircuitNodeType::OR:
    case CircuitNodeType::NAND:
    case CircuitNodeType::NOR:
    case CircuitNodeType::XOR:
    case CircuitNodeType::XNOR:
      return inputCount_;
    }
    return 2;
  }
  int outputCount() const
  {
    // Output and SevenSegment are pure sinks
    return (type == CircuitNodeType::Output ||
            type == CircuitNodeType::SevenSegment)
               ? 0
               : 1;
  }
};

// ============================================================================
// Wire
// ============================================================================

struct CircuitWire
{
  int id = 0;
  int fromNodeId = 0;  // source node  (output port)
  int toNodeId = 0;    // destination node (input port)
  int toPortIndex = 0; // which input on the destination

  // Sub-circuit instance endpoints (used when fromNodeId / toNodeId == -1)
  int fromInstanceId = -1; // source instance
  int fromPortIdx = 0;     // which output port of that instance

  int toInstanceId = -1;  // dest instance
  int toInstancePort = 0; // which input port of that instance
  bool selected = false;
};

struct BusWire
{
  int id = 0;
  int bitWidth = 2;         // number of bits (2–32)
  std::vector<int> wireIds; // which CircuitWire ids belong to this bus
  bool selected = false;
  // Visual path — same two endpoints as the member wires' midpoints
  int fromNodeId = 0;
  int toNodeId = 0;
};

// ============================================================================
// Port reference (used during interactive wiring)
// ============================================================================

struct PortRef
{
  int nodeId = -1;
  bool isOutput = false;
  int portIdx = 0;
  bool instancePort = false;

  // Valid if it references a real node (nodeId >= 0)
  // OR an instance port (instancePort == true).
  bool valid() const { return nodeId >= 0 || instancePort; }
};

// ============================================================================
// Sub-circuit port mapping
// ============================================================================

struct SubCircuitPort
{
  std::string name;        // user-visible port name
  int internalNodeId = -1; // Input/Output node inside the def
  bool isOutput = false;   // false = input port, true = output port
  int portIndex = 0;       // ordering index on the black-box face
  int internalPortIdx = 0;
};

// ============================================================================
// SubCircuitDef  —  the reusable definition (stored once, shared by instances)
// ============================================================================

struct SubCircuitDef
{
  int defId = 0;
  std::string name; // e.g. "Half Adder"

  std::vector<CircuitNode> nodes;    // internal node copies
  std::vector<CircuitWire> wires;    // internal wire copies
  std::vector<SubCircuitPort> ports; // ordered: inputs first, then outputs

  // Convenience helpers
  int inputPortCount() const
  {
    int n = 0;
    for (auto &p : ports)
      if (!p.isOutput)
        ++n;
    return n;
  }
  int outputPortCount() const
  {
    int n = 0;
    for (auto &p : ports)
      if (p.isOutput)
        ++n;
    return n;
  }
};

// ============================================================================
// SubCircuitInstance  —  one placed copy on the canvas
// ============================================================================

struct SubCircuitInstance
{
  int instanceId = 0;
  int defId = 0; // which SubCircuitDef this refers to

  float x = 0, y = 0;
  float w = 0, h = 0;

  bool selected = false;
  std::string label; // defaults to def.name

  // Runtime values — sized to match def.ports
  std::vector<bool> inputVals;  // driven by incoming wires
  std::vector<bool> outputVals; // computed by internal eval

  float dragOX = 0, dragOY = 0;

  int inputCount() const { return (int)inputVals.size(); }
  int outputCount() const { return (int)outputVals.size(); }
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
      case CircuitNodeType::SevenSegment:
        return "7SEG";
      case CircuitNodeType::Clock:
        return "Clock";
      }
      return "AND";
    };

    auto writeNode = [&](std::ostringstream &o, const CircuitNode &n)
    {
      o << "{"
        << "\"id\":" << n.id << ","
        << "\"type\":\"" << typeStr(n.type) << "\","
        << "\"x\":" << n.x << ","
        << "\"y\":" << n.y << ","
        << "\"value\":" << (n.value ? "true" : "false") << ","
        << "\"label\":\"" << n.label << "\","
        << "\"inputs\":" << n.inputCount_ << ","
        << "\"clockHz\":" << n.clockHz_ << ","
        << "\"clockRunning\":" << (n.clockRunning_ ? "true" : "false")
        << "}";
    };

    auto writeWire = [](std::ostringstream &o, const CircuitWire &w)
    {
      o << "{"
        << "\"id\":" << w.id << ","
        << "\"from\":" << w.fromNodeId << ","
        << "\"to\":" << w.toNodeId << ","
        << "\"port\":" << w.toPortIndex << ","
        << "\"fromInst\":" << w.fromInstanceId << ","
        << "\"fromPort\":" << w.fromPortIdx << ","
        << "\"toInst\":" << w.toInstanceId << ","
        << "\"toInstPort\":" << w.toInstancePort
        << "}";
    };

    std::ostringstream o;
    o << "{\n"
      << "\"nextId\":" << nextId_ << ","
      << "\"nextDefId\":" << nextDefId_ << ","
      << "\"nextInstId\":" << nextInstanceId_ << ","
      << "\"nextBusId\":" << nextBusId_ << ",\n";

    // ── nodes ─────────────────────────────────────────────────────────
    o << "\"nodes\":[\n";
    for (int i = 0; i < (int)nodes_.size(); ++i)
    {
      o << "  ";
      writeNode(o, nodes_[i]);
      if (i + 1 < (int)nodes_.size())
        o << ",";
      o << "\n";
    }
    o << "],\n";

    // ── wires ─────────────────────────────────────────────────────────
    o << "\"wires\":[\n";
    for (int i = 0; i < (int)wires_.size(); ++i)
    {
      o << "  ";
      writeWire(o, wires_[i]);
      if (i + 1 < (int)wires_.size())
        o << ",";
      o << "\n";
    }
    o << "],\n";

    // ── buses ─────────────────────────────────────────────────────────
    o << "\"buses\":[\n";
    for (int i = 0; i < (int)buses_.size(); ++i)
    {
      const auto &b = buses_[i];
      o << "  {\"id\":" << b.id
        << ",\"bits\":" << b.bitWidth
        << ",\"from\":" << b.fromNodeId
        << ",\"to\":" << b.toNodeId
        << ",\"wires\":[";
      for (int j = 0; j < (int)b.wireIds.size(); ++j)
        o << b.wireIds[j] << (j + 1 < (int)b.wireIds.size() ? "," : "");
      o << "]}";
      if (i + 1 < (int)buses_.size())
        o << ",";
      o << "\n";
    }
    o << "],\n";

    // ── defs ──────────────────────────────────────────────────────────
    o << "\"defs\":[\n";
    for (int di = 0; di < (int)defs_.size(); ++di)
    {
      const auto &def = defs_[di];
      o << "  {\"defId\":" << def.defId
        << ",\"name\":\"" << def.name << "\","
        << "\"nodes\":[\n";
      for (int i = 0; i < (int)def.nodes.size(); ++i)
      {
        o << "    ";
        writeNode(o, def.nodes[i]);
        if (i + 1 < (int)def.nodes.size())
          o << ",";
        o << "\n";
      }
      o << "  ],\"wires\":[\n";
      for (int i = 0; i < (int)def.wires.size(); ++i)
      {
        o << "    ";
        writeWire(o, def.wires[i]);
        if (i + 1 < (int)def.wires.size())
          o << ",";
        o << "\n";
      }
      o << "  ],\"ports\":[\n";
      for (int i = 0; i < (int)def.ports.size(); ++i)
      {
        const auto &p = def.ports[i];
        o << "    {"
          << "\"name\":\"" << p.name << "\","
          << "\"internalNodeId\":" << p.internalNodeId << ","
          << "\"isOutput\":" << (p.isOutput ? "true" : "false") << ","
          << "\"portIndex\":" << p.portIndex
          << "}";
        if (i + 1 < (int)def.ports.size())
          o << ",";
        o << "\n";
      }
      o << "  ]}";
      if (di + 1 < (int)defs_.size())
        o << ",";
      o << "\n";
    }
    o << "],\n";

    // ── instances ─────────────────────────────────────────────────────
    o << "\"instances\":[\n";
    for (int i = 0; i < (int)instances_.size(); ++i)
    {
      const auto &inst = instances_[i];
      o << "  {"
        << "\"instanceId\":" << inst.instanceId << ","
        << "\"defId\":" << inst.defId << ","
        << "\"x\":" << inst.x << ","
        << "\"y\":" << inst.y << ","
        << "\"label\":\"" << inst.label << "\""
        << "}";
      if (i + 1 < (int)instances_.size())
        o << ",";
      o << "\n";
    }
    o << "]\n}\n";

    return o.str();
  }

  std::string toVerilog() const
  {
    std::vector<const CircuitNode *> inputs, outputs, gates;
    for (auto &n : nodes_)
    {
      if (n.type == CircuitNodeType::Input ||
          n.type == CircuitNodeType::Clock)
        inputs.push_back(&n);
      else if (n.type == CircuitNodeType::Output ||
               n.type == CircuitNodeType::SevenSegment)
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
        c = (char)tolower((unsigned char)c);
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
      return "1'b0";
    };

    std::ostringstream o;
    o << "// Generated by Circuit Designer\n";
    o << "module circuit(\n";

    for (int i = 0; i < (int)inputs.size(); ++i)
    {
      bool isClock = (inputs[i]->type == CircuitNodeType::Clock);
      o << "    input  wire " << (isClock ? "/* clock */ " : "")
        << nodeName(inputs[i])
        << (i + 1 < (int)inputs.size() || !outputs.empty() ? "," : "") << "\n";
    }
    for (int i = 0; i < (int)outputs.size(); ++i)
      o << "    output wire " << nodeName(outputs[i])
        << (i + 1 < (int)outputs.size() ? "," : "") << "\n";
    o << ");\n\n";

    // Individual gate wires
    for (auto *g : gates)
      o << "wire " << nodeName(g) << ";\n";
    if (!gates.empty())
      o << "\n";

    // Bus declarations  [N-1:0] busname_bus
    for (auto &b : buses_)
    {
      const CircuitNode *src = findNodeConst(b.fromNodeId);
      if (!src)
        continue;
      o << "wire [" << (b.bitWidth - 1) << ":0] "
        << nodeName(src) << "_bus;  // "
        << b.bitWidth << "-bit bus\n";
    }
    if (!buses_.empty())
      o << "\n";

    // Bus bit assignments  assign src_bus[i] = wire_i_driver
    for (auto &b : buses_)
    {
      const CircuitNode *src = findNodeConst(b.fromNodeId);
      if (!src)
        continue;
      std::string busName = nodeName(src) + "_bus";

      for (int bit = 0; bit < (int)b.wireIds.size(); ++bit)
      {
        // Find the wire with this id
        int wId = b.wireIds[bit];
        std::string driver = "1'b0";
        for (auto &w : wires_)
        {
          if (w.id == wId)
          {
            const CircuitNode *wsrc = findNodeConst(w.fromNodeId);
            if (wsrc)
              driver = nodeName(wsrc);
            break;
          }
        }
        o << "assign " << busName << "[" << bit << "] = " << driver << ";\n";
      }
      o << "\n";
    }

    // Gate logic assignments
    for (auto *g : gates)
    {
      std::string out = nodeName(g);
      int nc = g->inputCount();

      std::vector<std::string> drivers;
      for (int i = 0; i < nc; ++i)
        drivers.push_back(driverOf(g->id, i));

      // Check if any driver is a bus bit
      for (int i = 0; i < nc; ++i)
      {
        for (auto &b : buses_)
        {
          const CircuitNode *bsrc = findNodeConst(b.fromNodeId);
          if (!bsrc)
            continue;
          for (int bit = 0; bit < (int)b.wireIds.size(); ++bit)
          {
            int wId = b.wireIds[bit];
            for (auto &w : wires_)
            {
              if (w.id == wId && w.toNodeId == g->id && w.toPortIndex == i)
              {
                drivers[i] = nodeName(bsrc) + "_bus[" + std::to_string(bit) + "]";
              }
            }
          }
        }
      }

      auto joinWith = [&](const std::string &op) -> std::string
      {
        if (drivers.empty())
          return "1'b0";
        std::string r = drivers[0];
        for (int i = 1; i < (int)drivers.size(); ++i)
          r += " " + op + " " + drivers[i];
        return r;
      };

      switch (g->type)
      {
      case CircuitNodeType::AND:
        o << "assign " << out << " = " << joinWith("&") << ";\n";
        break;
      case CircuitNodeType::OR:
        o << "assign " << out << " = " << joinWith("|") << ";\n";
        break;
      case CircuitNodeType::NOT:
        o << "assign " << out << " = ~" << drivers[0] << ";\n";
        break;
      case CircuitNodeType::NAND:
        o << "assign " << out << " = ~(" << joinWith("&") << ");\n";
        break;
      case CircuitNodeType::NOR:
        o << "assign " << out << " = ~(" << joinWith("|") << ");\n";
        break;
      case CircuitNodeType::XOR:
        o << "assign " << out << " = " << joinWith("^") << ";\n";
        break;
      case CircuitNodeType::XNOR:
        o << "assign " << out << " = ~(" << joinWith("^") << ");\n";
        break;
      default:
        break;
      }
    }
    if (!gates.empty())
      o << "\n";

    // Output assignments
    for (auto *out : outputs)
    {
      if (out->type == CircuitNodeType::SevenSegment)
      {
        // Emit one wire per segment
        static const char *segNames[] = {"a", "b", "c", "d", "e", "f", "g", "dp"};
        std::string base = nodeName(out);
        o << "// 7-segment display: " << base << "\n";
        for (int i = 0; i < 8; ++i)
          o << "assign " << base << "_seg_" << segNames[i]
            << " = " << driverOf(out->id, i) << ";\n";
      }
      else
      {
        o << "assign " << nodeName(out)
          << " = " << driverOf(out->id, 0) << ";\n";
      }
    }

    o << "\nendmodule\n";
    return o.str();
  }

  std::string toVHDL() const
  {
    std::vector<const CircuitNode *> inputs, outputs, gates;
    for (auto &n : nodes_)
    {
      if (n.type == CircuitNodeType::Input ||
          n.type == CircuitNodeType::Clock)
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
        c = (char)tolower((unsigned char)c);
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
    o << "-- Generated by Circuit Designer\n";
    o << "library IEEE;\n";
    o << "use IEEE.STD_LOGIC_1164.ALL;\n\n";

    o << "entity circuit is\n  port(\n";
    for (int i = 0; i < (int)inputs.size(); ++i)
    {
      bool isClock = (inputs[i]->type == CircuitNodeType::Clock);
      o << "    " << nodeName(inputs[i])
        << " : in  " << (isClock ? "std_logic" : "std_logic")
        << (isClock ? ";  -- clock" : "")
        << (i + 1 < (int)inputs.size() || !outputs.empty() ? ";" : "") << "\n";
    }
    for (int i = 0; i < (int)outputs.size(); ++i)
      o << "    " << nodeName(outputs[i]) << " : out std_logic"
        << (i + 1 < (int)outputs.size() ? ";" : "") << "\n";
    o << "  );\nend circuit;\n\n";

    o << "architecture Behavioral of circuit is\n";

    // Individual gate signals
    for (auto *g : gates)
      o << "  signal " << nodeName(g) << " : std_logic;\n";

    // Bus signals  std_logic_vector(N-1 downto 0)
    for (auto &b : buses_)
    {
      const CircuitNode *src = findNodeConst(b.fromNodeId);
      if (!src)
        continue;
      o << "  signal " << nodeName(src) << "_bus"
        << " : std_logic_vector(" << (b.bitWidth - 1) << " downto 0);"
        << "  -- " << b.bitWidth << "-bit bus\n";
    }

    o << "begin\n";

    // Bus bit assignments
    for (auto &b : buses_)
    {
      const CircuitNode *src = findNodeConst(b.fromNodeId);
      if (!src)
        continue;
      std::string busName = nodeName(src) + "_bus";

      for (int bit = 0; bit < (int)b.wireIds.size(); ++bit)
      {
        int wId = b.wireIds[bit];
        std::string driver = "'0'";
        for (auto &w : wires_)
        {
          if (w.id == wId)
          {
            const CircuitNode *wsrc = findNodeConst(w.fromNodeId);
            if (wsrc)
              driver = nodeName(wsrc);
            break;
          }
        }
        o << "  " << busName << "(" << bit << ") <= " << driver << ";\n";
      }
      o << "\n";
    }

    // Gate logic
    for (auto *g : gates)
    {
      int nc = g->inputCount();
      std::string sig = nodeName(g);

      // Collect drivers, resolving bus-bit references
      std::vector<std::string> drivers;
      for (int i = 0; i < nc; ++i)
        drivers.push_back(driverOf(g->id, i));

      for (int i = 0; i < nc; ++i)
      {
        for (auto &b : buses_)
        {
          const CircuitNode *bsrc = findNodeConst(b.fromNodeId);
          if (!bsrc)
            continue;
          for (int bit = 0; bit < (int)b.wireIds.size(); ++bit)
          {
            int wId = b.wireIds[bit];
            for (auto &w : wires_)
            {
              if (w.id == wId && w.toNodeId == g->id && w.toPortIndex == i)
              {
                drivers[i] = nodeName(bsrc) + "_bus(" + std::to_string(bit) + ")";
              }
            }
          }
        }
      }

      auto joinWith = [&](const std::string &op) -> std::string
      {
        if (drivers.empty())
          return "'0'";
        std::string r = drivers[0];
        for (int i = 1; i < (int)drivers.size(); ++i)
          r += " " + op + " " + drivers[i];
        return r;
      };

      switch (g->type)
      {
      case CircuitNodeType::AND:
        o << "  " << sig << " <= " << joinWith("and") << ";\n";
        break;
      case CircuitNodeType::OR:
        o << "  " << sig << " <= " << joinWith("or") << ";\n";
        break;
      case CircuitNodeType::NOT:
        o << "  " << sig << " <= not " << drivers[0] << ";\n";
        break;
      case CircuitNodeType::NAND:
        o << "  " << sig << " <= not (" << joinWith("and") << ");\n";
        break;
      case CircuitNodeType::NOR:
        o << "  " << sig << " <= not (" << joinWith("or") << ");\n";
        break;
      case CircuitNodeType::XOR:
        o << "  " << sig << " <= " << joinWith("xor") << ";\n";
        break;
      case CircuitNodeType::XNOR:
        o << "  " << sig << " <= not (" << joinWith("xor") << ");\n";
        break;
      default:
        break;
      }
    }

    // Output assignments
    for (auto *out : outputs)
      o << "  " << nodeName(out)
        << " <= " << driverOf(out->id, 0) << ";\n";

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
      else if (n.type == CircuitNodeType::SevenSegment)
      {
        // Bezel
        o << "  <rect x='" << nx << "' y='" << ny
          << "' width='" << n.w << "' height='" << n.h
          << "' rx='6' fill='#0c0e0a' stroke='#3c4637' stroke-width='1.5'/>\n";

        float faceL = nx + n.w * 0.42f;
        float faceR = nx + n.w * 0.92f;
        float faceT = ny + n.h * 0.08f;
        float faceB = ny + n.h * 0.88f;
        float faceW = faceR - faceL;
        float faceH = faceB - faceT;
        float thick = faceW * 0.13f;
        float gap = thick * 0.18f;

        bool segs[8] = {};
        for (int i = 0; i < 8 && i < (int)n.inputVals.size(); ++i)
          segs[i] = n.inputVals[i];

        auto litCol = [](bool on) -> const char *
        {
          return on ? "#32e650" : "#1e321c";
        };

        float digitCX = (faceL + faceR) * 0.5f;
        float yTop = faceT, yMid = faceT + faceH * 0.5f, yBot = faceB;
        float halfH = faceH * 0.5f;

        // Horizontal segment helper (outputs SVG polygon)
        auto svgH = [&](float cx, float cy, bool on)
        {
          float hw = faceW * 0.5f - thick * 0.5f - gap;
          float hh = thick * 0.5f;
          float cap = hh * 0.7f;
          o << "  <polygon points='"
            << (cx - hw + cap) << "," << (cy - hh) << " "
            << (cx + hw - cap) << "," << (cy - hh) << " "
            << (cx + hw) << "," << cy << " "
            << (cx + hw - cap) << "," << (cy + hh) << " "
            << (cx - hw + cap) << "," << (cy + hh) << " "
            << (cx - hw) << "," << cy
            << "' fill='" << litCol(on) << "'/>\n";
        };
        auto svgV = [&](float cx, float cy, float segH, bool on)
        {
          float hw = thick * 0.5f;
          float hh = segH * 0.5f - thick * 0.5f - gap;
          float cap = hw * 0.7f;
          o << "  <polygon points='"
            << (cx - hw) << "," << (cy - hh + cap) << " "
            << cx << "," << (cy - hh) << " "
            << (cx + hw) << "," << (cy - hh + cap) << " "
            << (cx + hw) << "," << (cy + hh - cap) << " "
            << cx << "," << (cy + hh) << " "
            << (cx - hw) << "," << (cy + hh - cap)
            << "' fill='" << litCol(on) << "'/>\n";
        };

        svgH(digitCX, yTop + thick * 0.5f, segs[0]);                     // a
        svgH(digitCX, yBot - thick * 0.5f, segs[3]);                     // d
        svgH(digitCX, yMid, segs[6]);                                    // g
        svgV(faceL + thick * 0.5f, yTop + halfH * 0.5f, halfH, segs[5]); // f
        svgV(faceR - thick * 0.5f, yTop + halfH * 0.5f, halfH, segs[1]); // b
        svgV(faceL + thick * 0.5f, yBot - halfH * 0.5f, halfH, segs[4]); // e
        svgV(faceR - thick * 0.5f, yBot - halfH * 0.5f, halfH, segs[2]); // c

        // dp
        float dpCX = faceR + thick * 1.1f;
        float dpCY = yBot - thick * 0.5f;
        float dpR = thick * 0.45f;
        o << "  <circle cx='" << dpCX << "' cy='" << dpCY
          << "' r='" << dpR << "' fill='" << litCol(segs[7]) << "'/>\n";
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
      if (s == "7SEG")
        return CircuitNodeType::SevenSegment;
      if (s == "Clock")
        return CircuitNodeType::Clock;
      return CircuitNodeType::AND;
    };

    JsonValue root;
    if (!JsonParser::tryParse(json, root) || !root.isObject())
      return false;

    // ── helpers ───────────────────────────────────────────────────────
    auto readNode = [&](const JsonValue &jn) -> CircuitNode
    {
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
      if (auto *v = jn.get("inputs"))
        n.inputCount_ = std::max(1, v->getInt(2));
      if (auto *v = jn.get("clockHz"))
        n.clockHz_ = std::max(0.1f, (float)v->getNumber());
      if (auto *v = jn.get("clockRunning"))
        n.clockRunning_ = v->getBool();
      nodeDims(n.type, n.w, n.h, n.inputCount_);
      n.x = snapX(n.x, n.w);
      n.y = snapY(n.y, n.h);
      n.inputVals.assign(n.inputCount(), false);
      return n;
    };

    auto readWire = [](const JsonValue &jw) -> CircuitWire
    {
      CircuitWire w;
      if (auto *v = jw.get("id"))
        w.id = v->getInt();
      if (auto *v = jw.get("from"))
        w.fromNodeId = v->getInt();
      if (auto *v = jw.get("to"))
        w.toNodeId = v->getInt();
      if (auto *v = jw.get("port"))
        w.toPortIndex = v->getInt();
      if (auto *v = jw.get("fromInst"))
        w.fromInstanceId = v->getInt();
      if (auto *v = jw.get("fromPort"))
        w.fromPortIdx = v->getInt();
      if (auto *v = jw.get("toInst"))
        w.toInstanceId = v->getInt();
      if (auto *v = jw.get("toInstPort"))
        w.toInstancePort = v->getInt();
      return w;
    };

    // ── parse into temporaries ────────────────────────────────────────
    std::vector<CircuitNode> newNodes;
    std::vector<CircuitWire> newWires;
    std::vector<BusWire> newBuses;
    std::vector<SubCircuitDef> newDefs;
    std::vector<SubCircuitInstance> newInsts;

    int newNextId = 1;
    int newNextDefId = 1;
    int newNextInstId = 1;
    int newNextBusId = 1;

    if (auto *v = root.get("nextId"))
      newNextId = v->getInt(1);
    if (auto *v = root.get("nextDefId"))
      newNextDefId = v->getInt(1);
    if (auto *v = root.get("nextInstId"))
      newNextInstId = v->getInt(1);
    if (auto *v = root.get("nextBusId"))
      newNextBusId = v->getInt(1);

    // nodes
    if (auto *arr = root.get("nodes"))
      for (size_t i = 0; i < arr->size(); ++i)
        newNodes.push_back(readNode((*arr)[i]));

    // wires
    if (auto *arr = root.get("wires"))
      for (size_t i = 0; i < arr->size(); ++i)
        newWires.push_back(readWire((*arr)[i]));

    // buses
    if (auto *arr = root.get("buses"))
    {
      for (size_t i = 0; i < arr->size(); ++i)
      {
        const JsonValue &jb = (*arr)[i];
        BusWire b;
        if (auto *v = jb.get("id"))
          b.id = v->getInt();
        if (auto *v = jb.get("bits"))
          b.bitWidth = v->getInt(2);
        if (auto *v = jb.get("from"))
          b.fromNodeId = v->getInt();
        if (auto *v = jb.get("to"))
          b.toNodeId = v->getInt();
        if (auto *arr2 = jb.get("wires"))
          for (size_t j = 0; j < arr2->size(); ++j)
            b.wireIds.push_back((*arr2)[j].getInt());
        newNextBusId = std::max(newNextBusId, b.id + 1);
        newBuses.push_back(b);
      }
    }

    // defs
    if (auto *arr = root.get("defs"))
    {
      for (size_t i = 0; i < arr->size(); ++i)
      {
        const JsonValue &jd = (*arr)[i];
        SubCircuitDef def;
        if (auto *v = jd.get("defId"))
          def.defId = v->getInt();
        if (auto *v = jd.get("name"))
          def.name = v->getString();

        if (auto *na = jd.get("nodes"))
          for (size_t j = 0; j < na->size(); ++j)
            def.nodes.push_back(readNode((*na)[j]));

        if (auto *wa = jd.get("wires"))
          for (size_t j = 0; j < wa->size(); ++j)
            def.wires.push_back(readWire((*wa)[j]));

        if (auto *pa = jd.get("ports"))
        {
          for (size_t j = 0; j < pa->size(); ++j)
          {
            const JsonValue &jp = (*pa)[j];
            SubCircuitPort p;
            if (auto *v = jp.get("name"))
              p.name = v->getString();
            if (auto *v = jp.get("internalNodeId"))
              p.internalNodeId = v->getInt();
            if (auto *v = jp.get("isOutput"))
              p.isOutput = v->getBool();
            if (auto *v = jp.get("portIndex"))
              p.portIndex = v->getInt();
            def.ports.push_back(p);
          }
        }
        newNextDefId = std::max(newNextDefId, def.defId + 1);
        newDefs.push_back(def);
      }
    }

    // instances
    if (auto *arr = root.get("instances"))
    {
      for (size_t i = 0; i < arr->size(); ++i)
      {
        const JsonValue &ji = (*arr)[i];
        SubCircuitInstance inst;
        if (auto *v = ji.get("instanceId"))
          inst.instanceId = v->getInt();
        if (auto *v = ji.get("defId"))
          inst.defId = v->getInt();
        if (auto *v = ji.get("x"))
          inst.x = (float)v->getNumber();
        if (auto *v = ji.get("y"))
          inst.y = (float)v->getNumber();
        if (auto *v = ji.get("label"))
          inst.label = v->getString();

        // Restore dimensions and runtime arrays from the def
        for (auto &def : newDefs)
        {
          if (def.defId == inst.defId)
          {
            instanceDims(def, inst.w, inst.h);
            inst.x = snapX(inst.x, inst.w);
            inst.y = snapY(inst.y, inst.h);
            inst.inputVals.assign(def.inputPortCount(), false);
            inst.outputVals.assign(def.outputPortCount(), false);
            break;
          }
        }
        newNextInstId = std::max(newNextInstId, inst.instanceId + 1);
        newInsts.push_back(inst);
      }
    }

    // ── Commit ────────────────────────────────────────────────────────
    pushUndo();
    nodes_ = std::move(newNodes);
    wires_ = std::move(newWires);
    buses_ = std::move(newBuses);
    defs_ = std::move(newDefs);
    instances_ = std::move(newInsts);
    nextId_ = newNextId;
    nextDefId_ = newNextDefId;
    nextInstanceId_ = newNextInstId;
    nextBusId_ = newNextBusId;

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
      e.inputCount_ = n->inputCount_;
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
      nodeDims(e.type, nw, nh, e.inputCount_);
      n.w = nw;
      n.h = nh;

      n.x = snapX(ox + e.x - nw * 0.5f, nw); //  snap
      n.y = snapY(oy + e.y - nh * 0.5f, nh); //  snap
      n.value = e.value;
      n.label = e.label;
      n.inputCount_ = e.inputCount_;
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
    buses_.clear();
    defs_.clear();
    instances_.clear();
    nextBusId_ = 1;
    nextId_ = 1;
    nextDefId_ = 1;
    nextInstanceId_ = 1;
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

    std::vector<int> deadInst;
    for (auto &inst : instances_)
      if (inst.selected)
        deadInst.push_back(inst.instanceId);

    for (int iid : deadInst)
    {
      wires_.erase(
          std::remove_if(wires_.begin(), wires_.end(),
                         [iid](const CircuitWire &w)
                         {
                           return w.fromInstanceId == iid || w.toInstanceId == iid;
                         }),
          wires_.end());
      instances_.erase(
          std::remove_if(instances_.begin(), instances_.end(),
                         [iid](const SubCircuitInstance &i)
                         {
                           return i.instanceId == iid;
                         }),
          instances_.end());
    }

    evaluate();
    notify();
  }

  // Groups all selected wires that share the same fromNode into a bus.
  // Returns false if fewer than 2 selected wires qualify.
  bool groupSelectedIntoBus()
  {
    // Collect selected wire indices
    std::vector<int> selIdx;
    for (int i = 0; i < (int)wires_.size(); ++i)
      if (wires_[i].selected)
        selIdx.push_back(i);
    if (selIdx.size() < 2)
      return false;

    // All selected wires must share the same source node
    int srcId = wires_[selIdx[0]].fromNodeId;
    for (int i : selIdx)
      if (wires_[i].fromNodeId != srcId)
        return false;

    pushUndo();

    BusWire bus;
    bus.id = nextBusId_++;
    bus.bitWidth = (int)selIdx.size();
    bus.fromNodeId = srcId;
    bus.toNodeId = wires_[selIdx[0]].toNodeId; // visual endpoint (first wire)
    for (int i : selIdx)
      bus.wireIds.push_back(wires_[i].id);

    buses_.push_back(bus);
    notify();
    return true;
  }

  // ============================================================================
  // Sub-circuit grouping
  // ============================================================================

  bool groupSelectionIntoSubCircuit(const std::string &name = "")
  {
    // ── 1. Collect selected nodes ─────────────────────────────────────
    std::vector<int> selIds;
    for (auto &n : nodes_)
      if (n.selected)
        selIds.push_back(n.id);

    if (selIds.empty())
      return false;

    std::unordered_set<int> selSet(selIds.begin(), selIds.end());

    // ── 2. Compute centroid ───────────────────────────────────────────
    float cx = 0.f, cy = 0.f;
    for (int id : selIds)
    {
      const CircuitNode *n = findNodeConst(id);
      if (n)
      {
        cx += n->x + n->w * 0.5f;
        cy += n->y + n->h * 0.5f;
      }
    }
    cx /= float(selIds.size());
    cy /= float(selIds.size());

    // ── 3. Detect boundary wires ──────────────────────────────────────
    struct BoundaryWire
    {
      int wireIdx;
      bool isInput;
      int externalNodeId;
      int externalPortIdx;
      int internalNodeId;
      int internalPortIdx;
    };
    std::vector<BoundaryWire> boundaries;

    // Also track which internal (nodeId, portIdx) pairs are already
    // driven/read by a crossing wire — used for auto-promotion below.
    using PortKey = std::pair<int, int>;
    std::set<PortKey> crossedInputPorts; // internal node inputs already covered
    std::set<int> crossedOutputNodes;    // internal nodes whose output is already covered

    for (int i = 0; i < (int)wires_.size(); ++i)
    {
      const CircuitWire &w = wires_[i];
      bool srcIn = selSet.count(w.fromNodeId) > 0;
      bool dstIn = selSet.count(w.toNodeId) > 0;

      if (srcIn == dstIn)
        continue; // both inside or both outside

      BoundaryWire bw;
      bw.wireIdx = i;

      if (!srcIn && dstIn)
      {
        bw.isInput = true;
        bw.externalNodeId = w.fromNodeId;
        bw.externalPortIdx = 0;
        bw.internalNodeId = w.toNodeId;
        bw.internalPortIdx = w.toPortIndex;
        crossedInputPorts.insert({w.toNodeId, w.toPortIndex});
      }
      else
      {
        bw.isInput = false;
        bw.externalNodeId = w.toNodeId;
        bw.externalPortIdx = w.toPortIndex;
        bw.internalNodeId = w.fromNodeId;
        bw.internalPortIdx = 0;
        crossedOutputNodes.insert(w.fromNodeId);
      }
      boundaries.push_back(bw);
    }

    // ── 4. Build SubCircuitDef ────────────────────────────────────────
    pushUndo();

    SubCircuitDef def;
    def.defId = nextDefId_++;
    def.name = name.empty()
                   ? ("Module_" + std::to_string(def.defId))
                   : name;

    std::unordered_map<int, int> oldToDefId;
    int defLocalId = 1;
    for (int id : selIds)
    {
      const CircuitNode *n = findNodeConst(id);
      if (!n)
        continue;
      CircuitNode copy = *n;
      copy.selected = false;
      copy.id = defLocalId;
      copy.x = n->x - (cx - n->w * 0.5f);
      copy.y = n->y - (cy - n->h * 0.5f);
      oldToDefId[id] = defLocalId++;
      def.nodes.push_back(copy);
    }

    for (auto &w : wires_)
    {
      if (selSet.count(w.fromNodeId) && selSet.count(w.toNodeId))
      {
        CircuitWire cw = w;
        cw.fromNodeId = oldToDefId[w.fromNodeId];
        cw.toNodeId = oldToDefId[w.toNodeId];
        def.wires.push_back(cw);
      }
    }

    // ── 5. Build ports ────────────────────────────────────────────────
    std::map<PortKey, int> inPortKey;
    std::map<PortKey, int> outPortKey;
    int inputIdx = 0;
    int outputIdx = 0;

    // 5a. From boundary wires (same as before)
    for (auto &bw : boundaries)
    {
      if (bw.isInput)
      {
        PortKey key{bw.internalNodeId, bw.internalPortIdx};
        if (inPortKey.count(key))
          continue;
        SubCircuitPort p;
        const CircuitNode *n = findNodeConst(bw.internalNodeId);
        p.name = (n && !n->label.empty()) ? n->label
                                          : ("in_" + std::to_string(inputIdx));
        p.internalNodeId = oldToDefId[bw.internalNodeId];
        p.isOutput = false;
        p.portIndex = inputIdx++;
        inPortKey[key] = p.portIndex;
        def.ports.push_back(p);
      }
      else
      {
        PortKey key{bw.internalNodeId, 0};
        if (outPortKey.count(key))
          continue;
        SubCircuitPort p;
        const CircuitNode *n = findNodeConst(bw.internalNodeId);
        p.name = (n && !n->label.empty()) ? n->label
                                          : ("out_" + std::to_string(outputIdx));
        p.internalNodeId = oldToDefId[bw.internalNodeId];
        p.isOutput = true;
        p.portIndex = outputIdx++;
        outPortKey[key] = p.portIndex;
        def.ports.push_back(p);
      }
    }

    // 5b. Promote explicit IN/Output nodes not yet covered
    for (int id : selIds)
    {
      const CircuitNode *n = findNodeConst(id);
      if (!n)
        continue;

      if (n->type == CircuitNodeType::Input)
      {
        PortKey key{id, 0};
        if (!inPortKey.count(key))
        {
          SubCircuitPort p;
          p.name = n->label.empty() ? ("in_" + std::to_string(inputIdx)) : n->label;
          p.internalNodeId = oldToDefId[id];
          p.isOutput = false;
          p.portIndex = inputIdx++;
          inPortKey[key] = p.portIndex;
          def.ports.push_back(p);
        }
      }
      else if (n->type == CircuitNodeType::Output)
      {
        PortKey key{id, 0};
        if (!outPortKey.count(key))
        {
          SubCircuitPort p;
          p.name = n->label.empty() ? ("out_" + std::to_string(outputIdx)) : n->label;
          p.internalNodeId = oldToDefId[id];
          p.isOutput = true;
          p.portIndex = outputIdx++;
          outPortKey[key] = p.portIndex;
          def.ports.push_back(p);
        }
      }
    }

    // 5c. NEW: Auto-promote gate ports that have no crossing wire
    //     — this is what makes connectors appear even without IN/OUT nodes.
    for (int id : selIds)
    {
      const CircuitNode *n = findNodeConst(id);
      if (!n)
        continue;
      // Skip dedicated IO nodes (already handled above)
      if (n->type == CircuitNodeType::Input ||
          n->type == CircuitNodeType::Output ||
          n->type == CircuitNodeType::Clock)
        continue;

      // Each input port that is NOT already driven by a crossing wire
      // becomes a sub-circuit INPUT port.
      for (int pi = 0; pi < n->inputCount(); ++pi)
      {
        PortKey key{id, pi};
        if (crossedInputPorts.count(key))
          continue; // already a port from step 5a
        if (inPortKey.count(key))
          continue; // already registered

        // Check: is this port driven by a wire from INSIDE the selection?
        // If so, it is purely internal and should NOT become a port.
        bool drivenInternally = false;
        for (auto &w : wires_)
        {
          if (w.toNodeId == id && w.toPortIndex == pi &&
              selSet.count(w.fromNodeId))
          {
            drivenInternally = true;
            break;
          }
        }
        if (drivenInternally)
          continue;

        SubCircuitPort p;
        std::string baseName = n->label.empty()
                                   ? std::string(defaultName(n->type))
                                   : n->label;
        // For multi-input gates, suffix with port index
        p.name = (n->inputCount() > 1)
                     ? (baseName + "_in" + std::to_string(pi))
                     : (baseName + "_in");
        p.internalNodeId = oldToDefId[id];
        p.isOutput = false;
        p.portIndex = inputIdx++;
        inPortKey[key] = p.portIndex;
        def.ports.push_back(p);
      }

      // The output port — if it has no outgoing wire to OUTSIDE
      // AND is not driven purely internally → becomes a sub-circuit OUTPUT port.
      if (n->outputCount() > 0)
      {
        PortKey key{id, 0};
        if (!outPortKey.count(key) && !crossedOutputNodes.count(id))
        {
          // Only promote if this node's output is not consumed entirely
          // inside the selection (i.e., it has at least one internal
          // consumer OR no consumer at all — both cases it's a useful port).
          SubCircuitPort p;
          std::string baseName = n->label.empty()
                                     ? std::string(defaultName(n->type))
                                     : n->label;
          p.name = baseName + "_out";
          p.internalNodeId = oldToDefId[id];
          p.isOutput = true;
          p.portIndex = outputIdx++;
          outPortKey[key] = p.portIndex;
          def.ports.push_back(p);
        }
      }
    }

    // Sort: inputs first, then outputs, each by portIndex
    std::sort(def.ports.begin(), def.ports.end(),
              [](const SubCircuitPort &a, const SubCircuitPort &b)
              {
                if (a.isOutput != b.isOutput)
                  return !a.isOutput;
                return a.portIndex < b.portIndex;
              });
    {
      int inC = 0, outC = 0;
      for (auto &p : def.ports)
        p.portIndex = p.isOutput ? outC++ : inC++;
    }

    defs_.push_back(def);

    // ── 6. Place instance at centroid ─────────────────────────────────
    SubCircuitInstance inst;
    inst.instanceId = nextInstanceId_++;
    inst.defId = def.defId;
    inst.label = def.name;
    inst.inputVals.assign(def.inputPortCount(), false);
    inst.outputVals.assign(def.outputPortCount(), false);
    instanceDims(def, inst.w, inst.h);
    inst.x = snapX(cx - inst.w * 0.5f, inst.w);
    inst.y = snapY(cy - inst.h * 0.5f, inst.h);
    inst.selected = true;
    instances_.push_back(inst);

    // ── 7. Reconnect boundary wires (staged to avoid index invalidation)
    std::vector<CircuitWire> newWires;
    std::vector<int> wiresToDelete;

    for (auto &bw : boundaries)
    {
      wiresToDelete.push_back(bw.wireIdx);

      if (bw.isInput)
      {
        PortKey key{bw.internalNodeId, bw.internalPortIdx};
        int portIdx = inPortKey[key];
        CircuitWire nw;
        nw.id = nextId_++;
        nw.fromNodeId = bw.externalNodeId;
        nw.toNodeId = -1;
        nw.toPortIndex = portIdx;
        nw.toInstanceId = inst.instanceId;
        nw.toInstancePort = portIdx;
        newWires.push_back(nw);
      }
      else
      {
        PortKey key{bw.internalNodeId, 0};
        int portIdx = outPortKey[key];
        CircuitWire nw;
        nw.id = nextId_++;
        nw.fromNodeId = -1;
        nw.fromInstanceId = inst.instanceId;
        nw.fromPortIdx = portIdx;
        nw.toNodeId = bw.externalNodeId;
        nw.toPortIndex = bw.externalPortIdx;
        newWires.push_back(nw);
      }
    }

    std::sort(wiresToDelete.begin(), wiresToDelete.end(), std::greater<int>());
    wiresToDelete.erase(std::unique(wiresToDelete.begin(), wiresToDelete.end()),
                        wiresToDelete.end());
    for (int idx : wiresToDelete)
      wires_.erase(wires_.begin() + idx);
    for (auto &nw : newWires)
      wires_.push_back(nw);

    // ── 8. Remove grouped nodes and their internal wires ──────────────
    wires_.erase(
        std::remove_if(wires_.begin(), wires_.end(),
                       [&selSet](const CircuitWire &w)
                       { return selSet.count(w.fromNodeId) || selSet.count(w.toNodeId); }),
        wires_.end());
    nodes_.erase(
        std::remove_if(nodes_.begin(), nodes_.end(),
                       [&selSet](const CircuitNode &n)
                       { return selSet.count(n.id); }),
        nodes_.end());

    evaluate();
    notify();
    return true;
  }

  // ── Instantiate a def by defId (e.g. from a "place again" action) ─────
  SubCircuitInstance *placeInstance(int defId, float wx, float wy)
  {
    const SubCircuitDef *def = findDef(defId);
    if (!def)
      return nullptr;

    SubCircuitInstance inst;
    inst.instanceId = nextInstanceId_++;
    inst.defId = defId;
    inst.label = def->name;
    inst.inputVals.assign(def->inputPortCount(), false);
    inst.outputVals.assign(def->outputPortCount(), false);
    instanceDims(*def, inst.w, inst.h);
    inst.x = snapX(wx - inst.w * 0.5f, inst.w);
    inst.y = snapY(wy - inst.h * 0.5f, inst.h);
    inst.selected = true;
    instances_.push_back(inst);

    evaluate();
    notify();
    return &instances_.back();
  }

  // ── Explode an instance back into its constituent nodes ───────────────
  bool explodeInstance(int instanceId)
  {
    auto it = std::find_if(instances_.begin(), instances_.end(),
                           [instanceId](const SubCircuitInstance &i)
                           { return i.instanceId == instanceId; });
    if (it == instances_.end())
      return false;

    const SubCircuitDef *def = findDef(it->defId);
    if (!def)
      return false;

    pushUndo();

    float instCX = it->x + it->w * 0.5f;
    float instCY = it->y + it->h * 0.5f;

    // Remap def-local IDs => fresh canvas IDs
    std::unordered_map<int, int> defToCanvas;

    for (auto &dn : def->nodes)
    {
      CircuitNode n = dn;
      n.id = nextId_++;
      defToCanvas[dn.id] = n.id;

      n.x = snapX(instCX + dn.x - n.w * 0.5f, n.w);
      n.y = snapY(instCY + dn.y - n.h * 0.5f, n.h);

      n.selected = true;
      nodes_.push_back(n);
    }

    // Restore internal wires with fresh IDs
    for (auto &dw : def->wires)
    {
      CircuitWire w = dw;
      w.id = nextId_++;
      w.fromNodeId = defToCanvas[dw.fromNodeId];
      w.toNodeId = defToCanvas[dw.toNodeId];
      w.fromInstanceId = -1;
      w.toInstanceId = -1;
      wires_.push_back(w);
    }

    // Re-attach external wires that were going to/from this instance
    int iid = it->instanceId;
    for (auto &w : wires_)
    {
      if (w.fromInstanceId == iid)
      {
        const SubCircuitPort *p = findOutputPort(*def, w.fromPortIdx);
        if (p && defToCanvas.count(p->internalNodeId))
        {
          w.fromNodeId = defToCanvas.at(p->internalNodeId);
          w.fromInstanceId = -1;
        }
      }
      if (w.toInstanceId == iid)
      {
        const SubCircuitPort *p = findInputPort(*def, w.toInstancePort);
        if (p && defToCanvas.count(p->internalNodeId))
        {
          w.toNodeId = defToCanvas.at(p->internalNodeId);
          w.toInstanceId = -1;
        }
      }
    }

    // Clean up any wires that still reference this instance (unconnected ports)
    wires_.erase(
        std::remove_if(wires_.begin(), wires_.end(),
                       [iid](const CircuitWire &w)
                       { return w.fromInstanceId == iid || w.toInstanceId == iid; }),
        wires_.end());

    instances_.erase(it);
    evaluate();
    notify();
    return true;
  }

  // ── Dimension helper ──────────────────────────────────────────────────
  static void instanceDims(const SubCircuitDef &def, float &w, float &h)
  {
    int ports = (int)def.ports.size();
    int inPorts = def.inputPortCount();
    int outPorts = def.outputPortCount();
    int tallSide = std::max(inPorts, outPorts);
    // Each port row needs ~80 units; minimum 2 rows, plus header
    h = float(std::max(2, tallSide)) * 580.f + 400.f;
    w = 1400.f; // fixed width for the black box
  }

  void ungroupBus(int busId)
  {
    pushUndo();
    buses_.erase(std::remove_if(buses_.begin(), buses_.end(),
                                [busId](const BusWire &b)
                                { return b.id == busId; }),
                 buses_.end());
    notify();
  }

  void selectAll()
  {
    for (auto &n : nodes_)
      n.selected = true;
    for (auto &w : wires_)
      w.selected = true;
    for (auto &i : instances_)
      i.selected = true;
  }
  void deselectAll()
  {
    for (auto &n : nodes_)
      n.selected = false;
    for (auto &w : wires_)
      w.selected = false;
    for (auto &i : instances_)
      i.selected = false;
  }

  void deselectAllInstances()
  {
    for (auto &i : instances_)
      i.selected = false;
  }

  int selectedCount() const
  {
    int n = int(std::count_if(nodes_.begin(), nodes_.end(),
                              [](const CircuitNode &n)
                              { return n.selected; }));
    int i = int(std::count_if(instances_.begin(), instances_.end(),
                              [](const SubCircuitInstance &i)
                              { return i.selected; }));
    return n + i;
  }

  // ── Place a new node at world position ────────────────────────────────
  CircuitNode *placeNode(CircuitNodeType type, float wx, float wy)
  {
    CircuitNode n;
    n.id = nextId_++;
    n.type = type;
    n.x = wx;
    n.y = wy;
    nodeDims(type, n.w, n.h, n.inputCount_);
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
    redoNodes_.push_back({nodes_, wires_, instances_});
    auto &s = undoNodes_.back();
    nodes_ = s.nodes;
    wires_ = s.wires;
    instances_ = s.instances;
    undoNodes_.pop_back();
    evaluate();
    notify();
  }
  void redo()
  {
    if (redoNodes_.empty())
      return;
    undoNodes_.push_back({nodes_, wires_, instances_});
    auto &s = redoNodes_.back();
    nodes_ = s.nodes;
    wires_ = s.wires;
    instances_ = s.instances;
    redoNodes_.pop_back();
    evaluate();
    notify();
  }

  void commitTextEdit()
  {
    if (!textEditing_)
      return;
    textEditing_ = false;

    if (textEdit_.nodeId < 0)
    {
      // Instance rename
      int iid = -(textEdit_.nodeId);
      SubCircuitInstance *inst = findInstance(iid);
      if (inst)
      {
        pushUndo();
        inst->label = textEdit_.text;
        // Also update the def name if this is the only instance
        SubCircuitDef *def = findDef(inst->defId);
        if (def)
          def->name = textEdit_.text;
      }
    }
    else
    {
      CircuitNode *n = findNode(textEdit_.nodeId);
      if (n)
      {
        pushUndo();
        n->label = textEdit_.text;
      }
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
    case CircuitNodeType::Clock:
      return "CLK";
    case CircuitNodeType::SevenSegment:
      return "7SEG";
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

    // Flush any pending instance rename (set by context menu)
    if (pendingInstanceRename_ >= 0)
    {
      SubCircuitInstance *inst = findInstance(pendingInstanceRename_);
      if (inst)
      {
        // Borrow the text-edit system using a fake node id trick:
        // we use nodeId = -(instanceId) to distinguish from real nodes
        textEditing_ = true;
        textEdit_.nodeId = -(inst->instanceId);
        textEdit_.text = inst->label;
        textEdit_.cursorVisible = true;
        textEdit_.blinkTimer = 0.0;
      }
      pendingInstanceRename_ = -1;
    }

    // Blink cursor
    if (textEditing_)
    {
      textEdit_.blinkTimer += dt;
      if (textEdit_.blinkTimer >= 0.53)
      {
        textEdit_.blinkTimer = 0.0;
        textEdit_.cursorVisible = !textEdit_.cursorVisible;
      }
    }

    // Tick all clock nodes
    bool anyToggled = false;
    for (auto &n : nodes_)
    {
      if (n.type != CircuitNodeType::Clock)
        continue;
      if (!n.clockRunning_)
        continue;

      n.clockAccum_ += dt;
      double period = 1.0 / double(n.clockHz_);
      while (n.clockAccum_ >= period)
      {
        n.clockAccum_ -= period;
        n.value = !n.value;
        anyToggled = true;
      }
    }
    if (anyToggled)
    {
      evaluate();
      notify();
    }
  }
  bool needsContinuousRedraw() const override
  {
    if (textEditing_)
      return true;
    for (auto &n : nodes_)
      if (n.type == CircuitNodeType::Clock && n.clockRunning_)
        return true;
    return false;
  }

  // ── Mouse ─────────────────────────────────────────────────────────────
  void onMouseDown(float x, float y) override
  {

    if (textEditing_)
      commitTextEdit();

    if (!probes_.empty())
    {
      float Z = currentZoom_;
      float panelTopWorld = viewOffsetY_ + (viewH_ - oscHeight_) / Z;

      // Resize handle (6px above panel top)
      if (std::abs(y - panelTopWorld) < 8.f / Z)
      {
        oscResizing_ = true;
        oscResizeStartY_ = y;
        oscResizeStartH_ = oscHeight_;
        return;
      }

      // Inside panel?
      if (y > panelTopWorld && y < viewOffsetY_ + viewH_ / Z)
      {
        float labelW = kLabelW / Z;
        float headerH = 20.f / Z;
        float panelX = viewOffsetX_;

        // ── Header button hit tests ───────────────────────────────
        float hdrBottom = panelTopWorld + headerH;
        if (y < hdrBottom)
        {
          float btnW = 20.f / Z, btnH = 14.f / Z;
          float btnY0 = panelTopWorld + (headerH - btnH) * 0.5f;
          float rightX = viewOffsetX_ + viewW_ / Z;
          float bx0 = rightX - 3.f * (btnW + 3.f / Z);
          if (y >= btnY0 && y <= btnY0 + btnH)
          {
            if (x >= bx0 && x < bx0 + btnW) // "–" zoom out
              oscCycleZoom_ = std::max(1, oscCycleZoom_ - 1);
            else if (x >= bx0 + btnW + 3.f / Z && x < bx0 + 2 * (btnW + 3.f / Z)) // "+" zoom in
              oscCycleZoom_ = std::min(8, oscCycleZoom_ + 1);
            else // "×" clear
              clearProbes();
            notify();
          }
          return;
        }

        // ── Row close buttons ─────────────────────────────────────
        float rowH = kRowH / Z;
        float rowTop = panelTopWorld + headerH;
        for (int ri = 0; ri < (int)probes_.size(); ++ri)
        {
          float ry = rowTop + ri * rowH;
          float xBtnX = panelX + labelW - 14.f / Z;
          float xBtnY = ry + (rowH - 10.f / Z) * 0.5f;
          if (x >= xBtnX && x <= xBtnX + 10.f / Z &&
              y >= xBtnY && y <= xBtnY + 10.f / Z)
          {
            removeProbe(probes_[ri].nodeId);
            return;
          }
        }

        // ── Cursor drag ───────────────────────────────────────────
        float sigX = panelX + labelW;
        if (x >= sigX)
        {
          float cycleW = kCycleW * oscCycleZoom_ / Z;
          oscCursorCycle_ = oscScrollX_ + (x - sigX) / cycleW;
          oscDraggingCursor_ = true;
          notify();
        }
        return; // swallow – don't interact with canvas while in panel
      }
    }

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

        // Two refs point to the "same thing" if:
        //   - both are regular nodes with equal nodeId, OR
        //   - both are instance ports with equal encoded nodeId AND portIdx
        bool sameEndpoint = false;
        if (!from.instancePort && !to.instancePort)
          sameEndpoint = (from.nodeId == to.nodeId);
        else if (from.instancePort && to.instancePort)
          sameEndpoint = (from.nodeId == to.nodeId && from.portIdx == to.portIdx);
        // from.instancePort XOR to.instancePort → never the same thing

        if (sameEndpoint)
        {
          wireStart_ = {};
          return;
        }

        pushUndo();

        CircuitWire w;
        w.id = nextId_++;

        // Source
        if (from.instancePort)
        {
          int iid, pidx;
          decodeInstancePortRef(from, iid, pidx);
          w.fromNodeId = -1;
          w.fromInstanceId = iid;
          w.fromPortIdx = pidx;
        }
        else
        {
          w.fromNodeId = from.nodeId;
        }

        // Destination
        if (to.instancePort)
        {
          int iid, pidx;
          decodeInstancePortRef(to, iid, pidx);
          w.toNodeId = -1;
          w.toInstanceId = iid;
          w.toInstancePort = pidx;
          w.toPortIndex = pidx;
        }
        else
        {
          w.toNodeId = to.nodeId;
          w.toPortIndex = to.portIdx;
        }

        // Remove any existing wire to the same destination port
        wires_.erase(
            std::remove_if(wires_.begin(), wires_.end(),
                           [&w](const CircuitWire &ew)
                           {
                             if (w.toInstanceId >= 0)
                               return ew.toInstanceId == w.toInstanceId &&
                                      ew.toInstancePort == w.toInstancePort;
                             return ew.toNodeId == w.toNodeId &&
                                    ew.toPortIndex == w.toPortIndex;
                           }),
            wires_.end());

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

    // ── Instance hit ──────────────────────────────────────────────────
    SubCircuitInstance *inst = hitInstance(x, y);
    if (inst)
    {
      if (!shift && !inst->selected)
      {
        deselectAll();
        deselectAllInstances();
      }
      inst->selected = true;
      dragging_ = true;
      dragStartX_ = x;
      dragStartY_ = y;
      for (auto &nn : nodes_)
      {
        nn.dragOX = nn.x;
        nn.dragOY = nn.y;
      }
      for (auto &ii : instances_)
      {
        ii.dragOX = ii.x;
        ii.dragOY = ii.y;
      }
      return;
    }

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

    if (oscResizing_)
    {
      float dy = (y - oscResizeStartY_) * currentZoom_;
      oscHeight_ = std::max(60.f, std::min(400.f, oscResizeStartH_ - dy));
      notify();
      return;
    }
    if (oscDraggingCursor_)
    {
      if (!probes_.empty())
      {
        float Z = currentZoom_;
        float sigX = viewOffsetX_ + kLabelW / Z;
        float cycleW = kCycleW * oscCycleZoom_ / Z;
        oscCursorCycle_ = oscScrollX_ + (x - sigX) / cycleW;
        oscCursorCycle_ = std::max(0.f, oscCursorCycle_);
        notify();
      }
      return;
    }

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

      for (auto &inst : instances_)
      {
        if (!inst.selected)
          continue;
        inst.x = snapX(inst.dragOX + dx, inst.w);
        inst.y = snapY(inst.dragOY + dy, inst.h);
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

    oscResizing_ = false;
    oscDraggingCursor_ = false;
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

    if (ctxMenu_.open)
    {
      closeContextMenu();
      notify();
      return;
    }

    CircuitNode *n = hitNode(x, y);
    SubCircuitInstance *instHit = hitInstance(x, y);
    int wireIdx = hitWire(x, y);
    int busIdx = hitBus(x, y);

    std::vector<ContextMenuItem> items;

    // ── Instance hit ──────────────────────────────────────────────────
    if (instHit && !n)
    {
      SubCircuitInstance *target = instHit;
      items.push_back({"Sub-circuit: " + target->label, {}, false});
      items.push_back({"", {}, true});

      items.push_back({"Rename", [this, target]
                       {
                         pendingInstanceRename_ = target->instanceId;
                         notify();
                       }});

      items.push_back({"Explode", [this, target]
                       {
                         explodeInstance(target->instanceId);
                       }});

      items.push_back({"Place another copy", [this, target]
                       {
                         const SubCircuitDef *def = findDefConst(target->defId);
                         if (def)
                           placeInstance(def->defId,
                                         target->x + target->w + 200.f,
                                         target->y);
                       }});

      items.push_back({"", {}, true});

      items.push_back({"Delete", [this, target]
                       {
                         pushUndo();
                         int iid = target->instanceId;
                         wires_.erase(
                             std::remove_if(wires_.begin(), wires_.end(),
                                            [iid](const CircuitWire &w)
                                            {
                                              return w.fromInstanceId == iid ||
                                                     w.toInstanceId == iid;
                                            }),
                             wires_.end());
                         instances_.erase(
                             std::remove_if(instances_.begin(), instances_.end(),
                                            [iid](const SubCircuitInstance &i)
                                            {
                                              return i.instanceId == iid;
                                            }),
                             instances_.end());
                         evaluate();
                         notify();
                       }});
    }
    // ── Regular node hit ──────────────────────────────────────────────
    else if (n)
    {
      CircuitNode *target = n;

      // Ensure the right-clicked node is part of the selection
      if (!target->selected)
      {
        deselectAll();
        target->selected = true;
      }
      // If multiple nodes are selected, offer grouping at the top
      if (selectedCount() >= 2)
      {
        items.push_back({"Group as Sub-circuit",
                         [this]()
                         { groupSelectionIntoSubCircuit(""); }});
        items.push_back({"", {}, true});
      }
      items.push_back({"Rename", [this, target]
                       { startTextEdit(target); notify(); }});
      items.push_back({"", {}, true});

      if (target->type == CircuitNodeType::Input)
        items.push_back({"Toggle", [this, target]
                         { pushUndo(); target->value = !target->value;
                               evaluate(); notify(); }});

      items.push_back({"Duplicate", [this, target]
                       { deselectAll(); target->selected = true;
                           duplicateSelected(); }});

      bool isMultiInput = (target->type == CircuitNodeType::AND ||
                           target->type == CircuitNodeType::OR ||
                           target->type == CircuitNodeType::NAND ||
                           target->type == CircuitNodeType::NOR ||
                           target->type == CircuitNodeType::XOR ||
                           target->type == CircuitNodeType::XNOR);

      if (target->type == CircuitNodeType::Clock)
      {
        items.push_back({"", {}, true});
        items.push_back({target->clockRunning_
                             ? "Pause clock"
                             : "Resume clock",
                         [this, target]
                         {
                           pushUndo();
                           target->clockRunning_ = !target->clockRunning_;
                           notify();
                         }});

        const float presets[] = {0.5f, 1.f, 2.f, 5.f, 10.f, 20.f, 50.f, 100.f};
        for (float hz : presets)
        {
          char buf[32];
          if (hz < 1.f)
            snprintf(buf, sizeof(buf), "Set %.1f Hz", hz);
          else
            snprintf(buf, sizeof(buf), "Set %.0f Hz", hz);
          items.push_back({buf, [this, target, hz]
                           {
                             pushUndo();
                             target->clockHz_ = hz;
                             target->clockAccum_ = 0.0;
                             notify();
                           }});
        }
        items.push_back({"", {}, true});
        items.push_back({"Speed x2", [this, target]
                         {
                           pushUndo();
                           target->clockHz_ = std::min(target->clockHz_ * 2.f, 1000.f);
                           notify();
                         }});
        items.push_back({"Speed /2", [this, target]
                         {
                           pushUndo();
                           target->clockHz_ = std::max(target->clockHz_ * 0.5f, 0.1f);
                           notify();
                         }});
      }

      if (isMultiInput)
      {
        items.push_back({"", {}, true});
        items.push_back({"Add input (+)", [this, target]
                         {
                           pushUndo();
                           target->inputCount_ = std::min(target->inputCount_ + 1, 8);
                           target->inputVals.assign(target->inputCount(), false);
                           nodeDims(target->type, target->w, target->h,
                                    target->inputCount_);
                           evaluate();
                           notify();
                         }});
        items.push_back({"Remove input (-)", [this, target]
                         {
                           if (target->inputCount_ <= 2)
                             return;
                           pushUndo();
                           int oldCount = target->inputCount_;
                           target->inputCount_ = oldCount - 1;
                           target->inputVals.assign(target->inputCount(), false);
                           nodeDims(target->type, target->w, target->h,
                                    target->inputCount_);
                           int removedPort = oldCount - 1;
                           wires_.erase(
                               std::remove_if(wires_.begin(), wires_.end(),
                                              [&](const CircuitWire &w)
                                              {
                                                return w.toNodeId == target->id &&
                                                       w.toPortIndex == removedPort;
                                              }),
                               wires_.end());
                           evaluate();
                           notify();
                         }});
      }

      items.push_back({"", {}, true});
      if (isProbed(target->id))
        items.push_back({"Remove probe", [this, target]
                         { removeProbe(target->id); }});
      else
        items.push_back({"Add probe", [this, target]
                         { addProbe(target->id); }});

      items.push_back({"", {}, true});
      items.push_back({"Delete", [this, target]
                       {
                         pushUndo();
                         int id = target->id;
                         buses_.erase(
                             std::remove_if(buses_.begin(), buses_.end(),
                                            [id](const BusWire &b)
                                            {
                                              return b.fromNodeId == id || b.toNodeId == id;
                                            }),
                             buses_.end());
                         nodes_.erase(
                             std::remove_if(nodes_.begin(), nodes_.end(),
                                            [id](const CircuitNode &nn)
                                            {
                                              return nn.id == id;
                                            }),
                             nodes_.end());
                         wires_.erase(
                             std::remove_if(wires_.begin(), wires_.end(),
                                            [id](const CircuitWire &w)
                                            {
                                              return w.fromNodeId == id || w.toNodeId == id;
                                            }),
                             wires_.end());
                         evaluate();
                         notify();
                       }});
    }
    // ── Bus hit ───────────────────────────────────────────────────────
    else if (busIdx >= 0 && wireIdx < 0)
    {
      int bid = buses_[busIdx].id;
      int bwidth = buses_[busIdx].bitWidth;

      items.push_back({"Bus (" + std::to_string(bwidth) + "-bit)",
                       {},
                       false});
      items.push_back({"", {}, true});
      items.push_back({"Ungroup Bus", [this, bid]
                       { ungroupBus(bid); }});
      items.push_back({"", {}, true});
      items.push_back({"Add bit (+)", [this, bid]
                       {
                         pushUndo();
                         for (auto &b : buses_)
                           if (b.id == bid)
                           {
                             b.bitWidth = std::min(b.bitWidth + 1, 32);
                             break;
                           }
                         notify();
                       }});
      items.push_back({"Remove bit (-)", [this, bid]
                       {
                         pushUndo();
                         for (auto &b : buses_)
                           if (b.id == bid)
                           {
                             b.bitWidth = std::max(b.bitWidth - 1, 2);
                             break;
                           }
                         notify();
                       }});
    }
    // ── Wire hit ──────────────────────────────────────────────────────
    else if (wireIdx >= 0)
    {
      int idx = wireIdx;
      items.push_back({"Delete wire", [this, idx]
                       { pushUndo();
                           wires_.erase(wires_.begin() + idx);
                           evaluate(); notify(); }});
      items.push_back({"", {}, true});

      if (!wires_[idx].selected)
      {
        deselectAll();
        wires_[idx].selected = true;
      }

      int selCount = 0;
      int srcId = wires_[idx].fromNodeId;
      bool allSameSrc = true;
      for (auto &w : wires_)
      {
        if (w.selected)
        {
          ++selCount;
          if (w.fromNodeId != srcId)
            allSameSrc = false;
        }
      }

      if (selCount >= 2 && allSameSrc)
      {
        items.push_back({"Group as Bus", [this]
                         { groupSelectedIntoBus(); }});
      }
      else
      {
        items.push_back({"Select more wires\nfrom same source\nto group as bus",
                         {},
                         false});
      }
    }
    // ── Canvas (empty space) ──────────────────────────────────────────
    else
    {
      // Group selection into sub-circuit if anything is selected
      if (selectedCount() >= 1)
      {
        items.push_back({"Group as Sub-circuit",
                         [this]()
                         {
                           groupSelectionIntoSubCircuit("");
                         }});
        items.push_back({"", {}, true});
      }

      // List available defs for re-placement
      if (!defs_.empty())
      {
        items.push_back({"Place module...", {}, false});
        for (auto &def : defs_)
        {
          items.push_back({def.name,
                           [this, &def, x, y]()
                           {
                             placeInstance(def.defId, x, y);
                           }});
        }
        items.push_back({"", {}, true});
      }

      items.push_back({"Select all", [this]
                       { selectAll();   notify(); }});
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
    if (e.ctrl && e.virtualKey == 'P')
    {
      for (auto &n : nodes_)
        if (n.selected)
          isProbed(n.id) ? removeProbe(n.id) : addProbe(n.id);
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

    for (auto &b : buses_)
      drawBusWire(ctx, b);

    if (wireStart_.valid() && mode_ == CircuitMode::Wire)
      drawWirePreview(ctx);

    for (auto &n : nodes_)
      drawNode(ctx, n);

    for (auto &inst : instances_)
      drawSubCircuitInstance(ctx, inst);

    if (rubberBand_)
      drawRubberBand(ctx);

    if (ctxMenu_.open)
      drawContextMenu(ctx);
    drawMinimap(ctx);
    if (!probes_.empty())
      drawOscilloscope(ctx);
  }

private:
  // Set by CircuitApp after surface creation
  Canvas2DGL *canvasGL_ = nullptr;

  // ── Oscilloscope / probe ──────────────────────────────────────────
  struct ProbedSignal
  {
    int nodeId = -1;
    std::string label;         // cached at probe-time
    Color color;               // assigned round-robin
    std::vector<bool> samples; // ring: [0..maxSamples)
  };

  static constexpr int kMaxSamples = 256;
  static constexpr float kProbeH = 160.f; // default panel height (screen px)
  static constexpr float kRowH = 28.f;    // screen px per signal row
  static constexpr float kLabelW = 64.f;  // screen px for label column
  static constexpr float kCycleW = 18.f;  // screen px per cycle column (at zoom=1)

  int pendingInstanceRename_ = -1;

  std::vector<ProbedSignal> probes_;
  uint64_t tickCount_ = 0;    // incremented each recordSample call
  int oscCycleZoom_ = 1;      // 1 = kCycleW px/cycle, 2 = 2× wider etc.
  float oscScrollX_ = 0.f;    // horizontal scroll offset in cycles
  float oscHeight_ = kProbeH; // panel height in screen px – drag to resize
  bool oscResizing_ = false;
  float oscResizeStartY_ = 0.f, oscResizeStartH_ = 0.f;
  float oscCursorCycle_ = -1.f; // -1 = hidden
  bool oscDraggingCursor_ = false;

  // Colour palette for probed signals
  static Color probeColor(int idx)
  {
    static const Color kPalette[] = {
        Color::fromRGB(140, 100, 240), // purple  – CLK
        Color::fromRGB(60, 200, 120),  // green   – logic high/IN
        Color::fromRGB(90, 168, 224),  // blue    – output
        Color::fromRGB(240, 160, 40),  // amber
        Color::fromRGB(224, 80, 80),   // red
        Color::fromRGB(80, 200, 200),  // teal
        Color::fromRGB(200, 200, 80),  // yellow
    };
    constexpr int N = sizeof(kPalette) / sizeof(kPalette[0]);
    return kPalette[idx % N];
  }

  // ── probe management ──────────────────────────────────────────────
  bool isProbed(int nodeId) const
  {
    for (auto &p : probes_)
      if (p.nodeId == nodeId)
        return true;
    return false;
  }

  void addProbe(int nodeId)
  {
    if (isProbed(nodeId))
      return;
    const CircuitNode *n = findNodeConst(nodeId);
    if (!n)
      return;

    ProbedSignal ps;
    ps.nodeId = nodeId;
    ps.label = n->label.empty() ? std::string(defaultName(n->type)) : n->label;
    ps.color = probeColor((int)probes_.size());
    ps.samples.reserve(kMaxSamples);
    probes_.push_back(std::move(ps));
    notify();
  }

  void removeProbe(int nodeId)
  {
    probes_.erase(
        std::remove_if(probes_.begin(), probes_.end(),
                       [nodeId](const ProbedSignal &p)
                       { return p.nodeId == nodeId; }),
        probes_.end());
    notify();
  }

  void clearProbes()
  {
    probes_.clear();
    tickCount_ = 0;
    notify();
  }

  // Called after every evaluate() – snapshots current node values
  void recordSample()
  {
    if (probes_.empty())
      return;
    ++tickCount_;
    for (auto &ps : probes_)
    {
      const CircuitNode *n = findNodeConst(ps.nodeId);
      bool v = n ? n->value : false;
      if ((int)ps.samples.size() >= kMaxSamples)
        ps.samples.erase(ps.samples.begin());
      ps.samples.push_back(v);
    }
    // Auto-scroll to keep newest sample visible
    int totalCycles = (int)(probes_.empty() ? 0 : probes_[0].samples.size());
    // (scroll handled in drawOscilloscope)
  }

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

  // Returns pointer to the BusWire that contains wireId, or nullptr.
  const BusWire *findBusForWire(int wireId) const
  {
    for (auto &b : buses_)
      for (int wid : b.wireIds)
        if (wid == wireId)
          return &b;
    return nullptr;
  }

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
        ctx.setFillColor(Color::fromRGBA(255, 255, 255, 18));
        ctx.fillRoundedRect(mx + 2.f / currentZoom_,
                            cy + 1.f / currentZoom_,
                            mw - 4.f / currentZoom_,
                            itemH - 2.f / currentZoom_,
                            2.f / currentZoom_);
      }

      // Label
      ctx.setFillColor(i == ctxMenu_.hoveredItem
                           ? Color::fromRGB(220, 225, 235)
                           : Color::fromRGB(170, 170, 188));
      ctx.fillText(it.label.c_str(),
                   mx + pad * 2.f,
                   cy + itemH * 0.5f);

      cy += itemH;
    }
  }

  void drawOscilloscope(Canvas2D &ctx)
  {
    if (probes_.empty())
      return;

    const float Z = currentZoom_;
    const float SX = viewW_;      // screen width  (px)
    const float SH = oscHeight_;  // panel height  (px)
    const float SY = viewH_ - SH; // panel top     (px)

    // Convert screen-space panel rect to world coordinates
    float wx = viewOffsetX_;
    float wy = viewOffsetY_ + SY / Z;
    float ww = SX / Z;
    float wh = SH / Z;

    // ── Background + border ───────────────────────────────────────
    ctx.setFillColor(Color::fromRGBA(10, 10, 15, 230));
    ctx.fillRect(wx, wy, ww, wh);
    ctx.setStrokeColor(Color::fromRGBA(60, 60, 80, 255));
    ctx.setLineWidth(1.f / Z);
    ctx.strokeRect(wx, wy, ww, wh);

    // ── Header bar (title + controls) ────────────────────────────
    float headerH = 20.f / Z;
    ctx.setFillColor(Color::fromRGBA(20, 20, 30, 255));
    ctx.fillRect(wx, wy, ww, headerH);

    float fs = 11.f / Z;
    char font[32];
    snprintf(font, sizeof(font), "bold %.0fpx sans", fs);
    ctx.setFont(font);
    ctx.setTextBaseline(TextBaseline::Middle);
    ctx.setFillColor(Color::fromRGBA(140, 140, 160, 255));
    ctx.fillText("Oscilloscope", wx + 6.f / Z, wy + headerH * 0.5f);

    // Zoom buttons  (drawn as tiny labelled rects in world space)
    float btnW = 20.f / Z, btnH = 14.f / Z;
    float btnY = wy + (headerH - btnH) * 0.5f;
    float btnX0 = wx + ww - 3.f * (btnW + 3.f / Z);

    auto drawBtn = [&](float bx, const char *lbl)
    {
      ctx.setFillColor(Color::fromRGBA(40, 40, 55, 255));
      ctx.fillRoundedRect(bx, btnY, btnW, btnH, 2.f / Z);
      ctx.setStrokeColor(Color::fromRGBA(70, 70, 90, 255));
      ctx.setLineWidth(0.5f / Z);
      ctx.strokeRoundedRect(bx, btnY, btnW, btnH, 2.f / Z);
      ctx.setFillColor(Color::fromRGBA(180, 180, 200, 255));
      float tw = ctx.measureText(lbl);
      ctx.fillText(lbl, bx + (btnW - tw) * 0.5f, btnY + btnH * 0.5f);
    };
    drawBtn(btnX0, "–");
    drawBtn(btnX0 + btnW + 3.f / Z, "+");
    drawBtn(btnX0 + 2.f * (btnW + 3.f / Z), "×");

    // ── Signal rows ───────────────────────────────────────────────
    float labelW = kLabelW / Z;
    float rowH = kRowH / Z;
    float cycleW = kCycleW * oscCycleZoom_ / Z;
    float contentW = ww - labelW;

    int numSamples = probes_.empty() ? 0 : (int)probes_[0].samples.size();
    // Clamp scroll
    float maxScroll = std::max(0.f, float(numSamples) - contentW / cycleW);
    oscScrollX_ = std::max(0.f, std::min(oscScrollX_, maxScroll));

    int firstSample = (int)oscScrollX_;

    float rowTop = wy + headerH;

    for (int ri = 0; ri < (int)probes_.size(); ++ri)
    {
      auto &ps = probes_[ri];
      float ry = rowTop + ri * rowH;
      if (ry + rowH > wy + wh)
        break; // panel full

      // Row divider
      ctx.setStrokeColor(Color::fromRGBA(40, 40, 55, 255));
      ctx.setLineWidth(0.5f / Z);
      ctx.beginPath();
      ctx.moveTo(wx, ry + rowH);
      ctx.lineTo(wx + ww, ry + rowH);
      ctx.stroke();

      // Label background
      ctx.setFillColor(Color::fromRGBA(18, 18, 26, 255));
      ctx.fillRect(wx, ry, labelW, rowH);

      // Colour strip
      ctx.setFillColor(ps.color);
      ctx.fillRect(wx, ry, 3.f / Z, rowH);

      // Signal label
      snprintf(font, sizeof(font), "bold %.0fpx sans", 10.f / Z);
      ctx.setFont(font);
      ctx.setFillColor(ps.color);
      ctx.fillText(ps.label.c_str(), wx + 6.f / Z, ry + rowH * 0.5f);

      // × close button (small, right side of label)
      float xBtnX = wx + labelW - 14.f / Z;
      float xBtnY = ry + (rowH - 10.f / Z) * 0.5f;
      ctx.setFillColor(Color::fromRGBA(80, 60, 60, 200));
      ctx.fillRoundedRect(xBtnX, xBtnY, 10.f / Z, 10.f / Z, 2.f / Z);
      ctx.setFillColor(Color::fromRGBA(200, 120, 120, 255));
      float xFs = 9.f / Z;
      char xFont[32];
      snprintf(xFont, sizeof(xFont), "bold %.0fpx sans", xFs);
      ctx.setFont(xFont);
      ctx.fillText("×", xBtnX + 1.5f / Z, xBtnY + 5.f / Z);

      // ── Waveform ─────────────────────────────────────────────
      // Clip drawing to content area
      float midY = ry + rowH * 0.5f;
      float hiY = ry + rowH * 0.18f;
      float loY = ry + rowH * 0.82f;
      float sigX = wx + labelW;

      // Vertical grid lines every 8 cycles
      ctx.setStrokeColor(Color::fromRGBA(35, 35, 50, 255));
      ctx.setLineWidth(0.5f / Z);
      for (int c = firstSample; c < numSamples; ++c)
      {
        if ((c % 8) == 0)
        {
          float gx = sigX + (c - firstSample) * cycleW;
          if (gx > wx + ww)
            break;
          ctx.beginPath();
          ctx.moveTo(gx, ry);
          ctx.lineTo(gx, ry + rowH);
          ctx.stroke();
        }
      }

      // Signal trace
      ctx.setStrokeColor(ps.color);
      ctx.setLineWidth(1.5f / Z);
      ctx.beginPath();

      bool started = false;
      bool lastVal = false;
      for (int ci = firstSample; ci < numSamples; ++ci)
      {
        float sx = sigX + (ci - firstSample) * cycleW;
        if (sx > wx + ww)
          break;
        bool v = ps.samples[ci];
        float sy = v ? hiY : loY;
        if (!started)
        {
          ctx.moveTo(sx, sy);
          started = true;
          lastVal = v;
        }
        else
        {
          if (v != lastVal)
          {
            float ex = sx;
            ctx.lineTo(ex, lastVal ? hiY : loY); // horizontal
            ctx.lineTo(ex, v ? hiY : loY);       // vertical edge
            lastVal = v;
          }
          ctx.lineTo(sx + cycleW, sy);
        }
      }
      ctx.stroke();

      // Current value dot (rightmost sample)
      if (numSamples > 0)
      {
        bool cur = ps.samples.back();
        float dotX = sigX + (numSamples - firstSample) * cycleW;
        if (dotX <= wx + ww)
        {
          ctx.setFillColor(ps.color);
          ctx.fillCircle(dotX, cur ? hiY : loY, 3.f / Z);
        }
      }
    }

    // ── Cycle-number axis ─────────────────────────────────────────
    float axisY = rowTop + (int)probes_.size() * rowH;
    if (axisY + 14.f / Z <= wy + wh)
    {
      ctx.setFillColor(Color::fromRGBA(12, 12, 20, 255));
      ctx.fillRect(wx, axisY, ww, 14.f / Z);
      snprintf(font, sizeof(font), "%.0fpx sans", 9.f / Z);
      ctx.setFont(font);
      ctx.setFillColor(Color::fromRGBA(80, 80, 100, 255));

      float sigX = wx + labelW;
      for (int c = firstSample; c < numSamples; c += std::max(1, 4 / oscCycleZoom_))
      {
        float gx = sigX + (c - firstSample) * cycleW;
        if (gx > wx + ww)
          break;
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", c);
        ctx.fillText(buf, gx + 1.f / Z, axisY + 9.f / Z);
      }
    }

    // ── Cursor line ───────────────────────────────────────────────
    if (oscCursorCycle_ >= 0.f)
    {
      float sigX = wx + labelW;
      float cx = sigX + (oscCursorCycle_ - oscScrollX_) * cycleW;
      if (cx >= sigX && cx <= wx + ww)
      {
        ctx.setStrokeColor(Color::fromRGBA(240, 160, 40, 200));
        ctx.setLineWidth(1.f / Z);
        ctx.beginPath();
        ctx.moveTo(cx, wy + headerH);
        ctx.lineTo(cx, wy + wh);
        ctx.stroke();

        // Cursor label
        ctx.setFillColor(Color::fromRGBA(240, 160, 40, 220));
        ctx.fillRoundedRect(cx - 12.f / Z, wy + headerH, 24.f / Z, 10.f / Z, 2.f / Z);
        snprintf(font, sizeof(font), "%.0fpx sans", 8.f / Z);
        ctx.setFont(font);
        ctx.setFillColor(Color::fromRGBA(20, 15, 5, 255));
        char tbuf[16];
        snprintf(tbuf, sizeof(tbuf), "t=%d", (int)oscCursorCycle_);
        float tw = ctx.measureText(tbuf);
        ctx.fillText(tbuf, cx - tw * 0.5f, wy + headerH + 7.f / Z);
      }
    }

    // ── Resize handle ─────────────────────────────────────────────
    float rhy = wy - 3.f / Z;
    float rhh = 6.f / Z;
    ctx.setFillColor(Color::fromRGBA(60, 60, 80, 200));
    ctx.fillRoundedRect(wx + ww * 0.4f, rhy, ww * 0.2f, rhh, 3.f / Z);
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

    // Scale factors: world => minimap
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
      else if (n.type == CircuitNodeType::SevenSegment)
        fill = n.value
                   ? Color::fromRGB(50, 180, 60)
                   : Color::fromRGB(40, 60, 40);
      else
        fill = n.selected ? Color::fromRGB(60, 120, 200) : Color::fromRGB(100, 100, 120);

      ctx.setFillColor(fill);
      ctx.fillRect(nx, ny, nw, nh);
    }

    // Sub-circuit instances on minimap
    for (auto &inst : instances_)
    {
      float nx = toMiniX(inst.x);
      float ny = toMiniY(inst.y);
      float nw = std::max(2.f / currentZoom_, inst.w * scaleX);
      float nh = std::max(2.f / currentZoom_, inst.h * scaleY);
      ctx.setFillColor(inst.selected
                           ? Color::fromRGB(120, 80, 220)
                           : Color::fromRGB(80, 60, 160));
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
  static void nodeDims(CircuitNodeType type, float &w, float &h,
                       int inputCount = 2)
  {
    switch (type)
    {
    case CircuitNodeType::AND:
    case CircuitNodeType::NAND:
      w = 1120;
      h = float(std::max(2, inputCount)) * 590.f;
      break;
    case CircuitNodeType::OR:
    case CircuitNodeType::NOR:
    case CircuitNodeType::XOR:
    case CircuitNodeType::XNOR:
      w = 1220;
      h = float(std::max(2, inputCount)) * 590.f;
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
    case CircuitNodeType::Clock:
      w = 1100;
      h = 1160;
      break;
    case CircuitNodeType::SevenSegment:
      w = 1600.f;
      h = 2600.f;
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

      float x0, y0, x1, y1;

      // Resolve source
      if (w.fromInstanceId >= 0)
      {
        const SubCircuitInstance *si = findInstanceConst(w.fromInstanceId);
        if (!si)
          continue;
        Pt p = instanceOutputPortPos(*si, w.fromPortIdx);
        x0 = p.x;
        y0 = p.y;
      }
      else
      {
        const CircuitNode *src = findNodeConst(w.fromNodeId);
        if (!src)
          continue;
        Pt p = outputPortPos(*src);
        x0 = p.x;
        y0 = p.y;
      }

      // Resolve destination
      if (w.toInstanceId >= 0)
      {
        const SubCircuitInstance *di = findInstanceConst(w.toInstanceId);
        if (!di)
          continue;
        Pt p = instanceInputPortPos(*di, w.toInstancePort);
        x1 = p.x;
        y1 = p.y;
      }
      else
      {
        const CircuitNode *dst = findNodeConst(w.toNodeId);
        if (!dst)
          continue;
        Pt p = inputPortPos(*dst, w.toPortIndex);
        x1 = p.x;
        y1 = p.y;
      }

      float ax = x0 + stub, bx = x1 - stub;

      auto segTest = [&](float sx0, float sy0,
                         float sx1, float sy1) -> bool
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
        hit = segTest(x0, y0, ax, y0) || segTest(ax, y0, midX, y0) ||
              segTest(midX, y0, midX, y1) || segTest(midX, y1, bx, y1) ||
              segTest(bx, y1, x1, y1);
      }
      else
      {
        float midY = (y0 + y1) * 0.5f;
        hit = segTest(x0, y0, ax, y0) || segTest(ax, y0, ax, midY) ||
              segTest(ax, midY, bx, midY) || segTest(bx, midY, bx, y1) ||
              segTest(bx, y1, x1, y1);
      }
      if (hit)
        return i;
    }
    return -1;
  }

  int hitBus(float mx, float my) const
  {
    const float thresh = 8.f / currentZoom_;
    for (int i = (int)buses_.size() - 1; i >= 0; --i)
    {
      const BusWire &b = buses_[i];
      const CircuitNode *src = findNodeConst(b.fromNodeId);
      const CircuitNode *dst = findNodeConst(b.toNodeId);
      if (!src || !dst)
        continue;
      Pt p0 = outputPortPos(*src);
      Pt p1 = inputPortPos(*dst, 0);
      // Simple midpoint distance test
      float cx = (p0.x + p1.x) * 0.5f;
      float cy = (p0.y + p1.y) * 0.5f;
      if (std::hypot(mx - cx, my - cy) < thresh * 8.f)
        return i;
    }
    return -1;
  }

  // Runs the internal logic of one sub-circuit instance given its
  // current inputVals, and writes results into outputVals.
  // Returns true if any output value changed.
  bool evaluateInstance(SubCircuitInstance &inst, const SubCircuitDef &def)
  {
    std::vector<CircuitNode> local = def.nodes;
    const std::vector<CircuitWire> &localWires = def.wires;

    // Initialise all inputVals to false
    for (auto &n : local)
      n.inputVals.assign(n.inputCount(), false);

    for (auto &port : def.ports)
    {
      if (port.isOutput)
        continue;
      bool driven = (port.portIndex < (int)inst.inputVals.size())
                        ? inst.inputVals[port.portIndex]
                        : false;

      for (auto &n : local)
      {
        if (n.id != port.internalNodeId)
          continue;

        if (n.type == CircuitNodeType::Input)
        {
          // Dedicated Input node — value flows out through wires
          n.value = driven;
        }
        else
        {

          int gatePin = 0;
          int sameNodeInputCount = 0;
          for (auto &p2 : def.ports)
          {
            if (!p2.isOutput && p2.internalNodeId == port.internalNodeId)
            {
              if (p2.portIndex == port.portIndex)
                gatePin = sameNodeInputCount;
              ++sameNodeInputCount;
            }
          }
          if (gatePin < (int)n.inputVals.size())
            n.inputVals[gatePin] = driven;
        }
        break;
      }
    }

    auto snapshot = [&]() -> std::vector<bool>
    {
      std::vector<bool> s;
      s.reserve(local.size());
      for (auto &n : local)
        s.push_back(n.value);
      return s;
    };

    std::vector<std::vector<bool>> history;
    history.reserve(64);
    static constexpr int kMaxIter = 64;

    for (int iter = 0; iter < kMaxIter; ++iter)
    {
      bool changed = false;

      // Evaluate all nodes
      for (auto &n : local)
      {
        bool prev = n.value;
        switch (n.type)
        {
        case CircuitNodeType::Input:
          break; // value already seeded
        case CircuitNodeType::AND:
        {
          bool v = !n.inputVals.empty();
          for (auto b : n.inputVals)
            v = v && b;
          n.value = v;
          break;
        }
        case CircuitNodeType::OR:
        {
          bool v = false;
          for (auto b : n.inputVals)
            v = v || b;
          n.value = v;
          break;
        }
        case CircuitNodeType::NAND:
        {
          bool v = !n.inputVals.empty();
          for (auto b : n.inputVals)
            v = v && b;
          n.value = !v;
          break;
        }
        case CircuitNodeType::NOR:
        {
          bool v = false;
          for (auto b : n.inputVals)
            v = v || b;
          n.value = !v;
          break;
        }
        case CircuitNodeType::XOR:
        {
          int ones = 0;
          for (auto b : n.inputVals)
            ones += b;
          n.value = (ones % 2) != 0;
          break;
        }
        case CircuitNodeType::XNOR:
        {
          int ones = 0;
          for (auto b : n.inputVals)
            ones += b;
          n.value = (ones % 2) == 0;
          break;
        }
        case CircuitNodeType::NOT:
          n.value = !n.inputVals.empty() && !n.inputVals[0];
          break;
        case CircuitNodeType::Output:
          n.value = !n.inputVals.empty() && n.inputVals[0];
          break;
        default:
          break;
        }
        if (prev != n.value)
          changed = true;
      }

      // Propagate wires
      for (auto &w : localWires)
      {
        CircuitNode *src = nullptr, *dst = nullptr;
        for (auto &n : local)
        {
          if (n.id == w.fromNodeId)
            src = &n;
          if (n.id == w.toNodeId)
            dst = &n;
        }
        if (!src || !dst)
          continue;
        if (w.toPortIndex < (int)dst->inputVals.size())
          dst->inputVals[w.toPortIndex] = src->value;
      }

      if (!changed)
        break;

      auto snap = snapshot();
      for (auto &prev : history)
        if (prev == snap)
          goto done;
      history.push_back(std::move(snap));
    }
  done:

    // ── Read outputs ──────────────────────────────────────────────────
    bool anyChanged = false;
    for (auto &port : def.ports)
    {
      if (!port.isOutput)
        continue;
      bool v = false;
      for (auto &n : local)
      {
        if (n.id == port.internalNodeId)
        {

          v = n.value;
          break;
        }
      }
      if (port.portIndex < (int)inst.outputVals.size() &&
          inst.outputVals[port.portIndex] != v)
      {
        inst.outputVals[port.portIndex] = v;
        anyChanged = true;
      }
    }
    return anyChanged;
  }
  // ── Logic evaluation ──────────────────────────────────────────────────
  void evaluate()
  {
    for (auto &n : nodes_)
      std::fill(n.inputVals.begin(), n.inputVals.end(), false);
    for (auto &inst : instances_)
      std::fill(inst.inputVals.begin(), inst.inputVals.end(), false);

    for (int iter = 0; iter < 30; ++iter)
    {
      bool changed = false;

      // ── 1. Evaluate instances first (they read inputVals, write outputVals) ──
      for (auto &inst : instances_)
      {
        const SubCircuitDef *def = findDefConst(inst.defId);
        if (!def)
          continue;
        if (evaluateInstance(inst, *def))
          changed = true;
      }

      // ── 2. Propagate ALL wires (instance outputs now up-to-date) ──────────
      for (auto &w : wires_)
      {
        bool srcVal = false;
        if (w.fromInstanceId >= 0)
        {
          const SubCircuitInstance *si = findInstanceConst(w.fromInstanceId);
          if (!si)
            continue;
          srcVal = (w.fromPortIdx < (int)si->outputVals.size())
                       ? si->outputVals[w.fromPortIdx]
                       : false;
        }
        else
        {
          const CircuitNode *src = findNodeConst(w.fromNodeId);
          if (!src)
            continue;
          srcVal = src->value;
        }

        if (w.toInstanceId >= 0)
        {
          SubCircuitInstance *di = findInstance(w.toInstanceId);
          if (!di)
            continue;
          int port = w.toInstancePort;
          if (port < (int)di->inputVals.size() &&
              di->inputVals[port] != srcVal)
          {
            di->inputVals[port] = srcVal;
            changed = true;
          }
        }
        else
        {
          CircuitNode *dst = findNode(w.toNodeId);
          if (!dst)
            continue;
          if (w.toPortIndex < (int)dst->inputVals.size() &&
              dst->inputVals[w.toPortIndex] != srcVal)
          {
            dst->inputVals[w.toPortIndex] = srcVal;
            changed = true;
          }
        }
      }

      // ── Evaluate all regular nodes ────────────────────────────────
      for (auto &n : nodes_)
      {
        bool prev = n.value;
        switch (n.type)
        {
        case CircuitNodeType::AND:
        {
          bool v = !n.inputVals.empty();
          for (auto b : n.inputVals)
            v = v && b;
          n.value = v;
          break;
        }
        case CircuitNodeType::OR:
        {
          bool v = false;
          for (auto b : n.inputVals)
            v = v || b;
          n.value = v;
          break;
        }
        case CircuitNodeType::NAND:
        {
          bool v = !n.inputVals.empty();
          for (auto b : n.inputVals)
            v = v && b;
          n.value = !v;
          break;
        }
        case CircuitNodeType::NOR:
        {
          bool v = false;
          for (auto b : n.inputVals)
            v = v || b;
          n.value = !v;
          break;
        }
        case CircuitNodeType::XOR:
        {
          int ones = 0;
          for (auto b : n.inputVals)
            ones += b ? 1 : 0;
          n.value = (ones % 2) != 0;
          break;
        }
        case CircuitNodeType::XNOR:
        {
          int ones = 0;
          for (auto b : n.inputVals)
            ones += b ? 1 : 0;
          n.value = (ones % 2) == 0;
          break;
        }
        case CircuitNodeType::Input:
          break;
        case CircuitNodeType::Output:
          n.value = !n.inputVals.empty() && n.inputVals[0];
          break;
        case CircuitNodeType::SevenSegment:
        {
          bool any = false;
          for (auto b : n.inputVals)
            any = any || b;
          n.value = any;
          break;
        }
        case CircuitNodeType::Clock:
          break;
        }
        if (prev != n.value)
          changed = true;
      }

      if (!changed)
        break;
    }

    recordSample();
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

  // ── Sub-circuit lookup helpers ────────────────────────────────────────

  // Returns pointer to the instance under (x,y), or nullptr
  SubCircuitInstance *hitInstance(float x, float y)
  {
    for (int i = int(instances_.size()) - 1; i >= 0; --i)
    {
      auto &inst = instances_[i];
      if (x >= inst.x && x <= inst.x + inst.w &&
          y >= inst.y && y <= inst.y + inst.h)
        return &inst;
    }
    return nullptr;
  }

  // Returns a PortRef for instance ports, encoding instanceId in nodeId
  // with a flag bit so the wire-mode handler can distinguish them.
  // We use nodeId = -(instanceId*100 + portIdx) for input ports,
  // and a separate isOutput flag. A cleaner approach is to extend
  // PortRef — do that here:
  PortRef hitInstancePort(float x, float y)
  {
    const float R = 10.f / currentZoom_;
    for (auto &inst : instances_)
    {
      const SubCircuitDef *def = findDefConst(inst.defId);
      if (!def)
        continue;

      int inCount = def->inputPortCount();
      for (int i = 0; i < inCount; ++i)
      {
        Pt p = instanceInputPortPos(inst, i);
        if (std::hypot(x - p.x, y - p.y) < R)
        {
          PortRef ref;
          ref.nodeId = -(inst.instanceId + 1);
          ref.isOutput = false;
          ref.portIdx = i;
          ref.instancePort = true;
          return ref;
        }
      }

      int outCount = def->outputPortCount();
      for (int i = 0; i < outCount; ++i)
      {
        Pt p = instanceOutputPortPos(inst, i);
        if (std::hypot(x - p.x, y - p.y) < R)
        {
          PortRef ref;
          ref.nodeId = -(inst.instanceId + 1);
          ref.isOutput = true;
          ref.portIdx = i;
          ref.instancePort = true;
          return ref;
        }
      }
    }
    return {};
  }

  // Decode a negative nodeId back to instanceId and portIdx
  static void decodeInstancePortRef(const PortRef &ref,
                                    int &outInstanceId, int &outPortIdx)
  {
    outInstanceId = (-ref.nodeId) - 1;
    outPortIdx = ref.portIdx;
  }

  SubCircuitDef *findDef(int defId)
  {
    for (auto &d : defs_)
      if (d.defId == defId)
        return &d;
    return nullptr;
  }
  const SubCircuitDef *findDefConst(int defId) const
  {
    for (auto &d : defs_)
      if (d.defId == defId)
        return &d;
    return nullptr;
  }

  SubCircuitInstance *findInstance(int instanceId)
  {
    for (auto &i : instances_)
      if (i.instanceId == instanceId)
        return &i;
    return nullptr;
  }
  const SubCircuitInstance *findInstanceConst(int instanceId) const
  {
    for (auto &i : instances_)
      if (i.instanceId == instanceId)
        return &i;
    return nullptr;
  }

  static const SubCircuitPort *findInputPort(const SubCircuitDef &def, int portIdx)
  {
    for (auto &p : def.ports)
      if (!p.isOutput && p.portIndex == portIdx)
        return &p;
    return nullptr;
  }
  static const SubCircuitPort *findOutputPort(const SubCircuitDef &def, int portIdx)
  {
    for (auto &p : def.ports)
      if (p.isOutput && p.portIndex == portIdx)
        return &p;
    return nullptr;
  }

  // Port world positions for instances
  static float instanceInputPortY(const SubCircuitInstance &inst, int idx)
  {
    // Header is ~400 units tall; then each port spaced evenly
    return inst.y + 400.f + idx * 580.f + 290.f;
  }
  static float instanceOutputPortY(const SubCircuitInstance &inst, int idx)
  {
    return inst.y + 400.f + idx * 580.f + 290.f;
  }
  static Pt instanceInputPortPos(const SubCircuitInstance &inst, int idx)
  {
    return {inst.x, instanceInputPortY(inst, idx)};
  }
  static Pt instanceOutputPortPos(const SubCircuitInstance &inst, int idx)
  {
    return {inst.x + inst.w, instanceOutputPortY(inst, idx)};
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
    PortRef instRef = hitInstancePort(x, y);
    if (instRef.valid())
      return instRef;
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

  void drawSevenSegmentShape(Canvas2D &ctx, const CircuitNode &n) const
  {
    float x = n.x, y = n.y, w = n.w, h = n.h;
    float Z = currentZoom_;

    // ── Bezel background ──────────────────────────────────────────────
    Color bezelFill = Color::fromRGB(12, 14, 10);
    Color bezelStroke = n.selected
                            ? selStrokeColor()
                            : Color::fromRGBA(60, 70, 55, 255);
    ctx.setFillColor(bezelFill);
    ctx.setStrokeColor(bezelStroke);
    ctx.setLineWidth((n.selected ? 2.f : 1.2f) / Z);
    ctx.fillRoundedRect(x, y, w, h, 6.f / Z);
    ctx.strokeRoundedRect(x, y, w, h, 6.f / Z);

    // ── Segment geometry ──────────────────────────────────────────────
    // Digit face sits in the right ~60% of the node width,
    // leaving the left side for input port stubs.
    float faceL = x + w * 0.42f; // left edge of digit face
    float faceR = x + w * 0.92f; // right edge
    float faceT = y + h * 0.08f; // top
    float faceB = y + h * 0.88f; // bottom (dp sits below this)
    float faceW = faceR - faceL;
    float faceH = faceB - faceT;

    // Thickness of each segment bar
    float thick = faceW * 0.13f;
    // Gap between segments (so they don't touch)
    float gap = thick * 0.18f;

    // Horizontal segment: a rect centered on a horizontal line
    // Vertical segment:   a rect centered on a vertical line

    // Segment lit / unlit colors
    auto segColor = [&](bool on) -> Color
    {
      return on ? Color::fromRGB(50, 230, 80)
                : Color::fromRGBA(30, 50, 28, 255);
    };

    bool a = n.inputVals.size() > 0 && n.inputVals[0];  // top horiz
    bool b = n.inputVals.size() > 1 && n.inputVals[1];  // top-right vert
    bool c = n.inputVals.size() > 2 && n.inputVals[2];  // bot-right vert
    bool d = n.inputVals.size() > 3 && n.inputVals[3];  // bot horiz
    bool e = n.inputVals.size() > 4 && n.inputVals[4];  // bot-left vert
    bool f = n.inputVals.size() > 5 && n.inputVals[5];  // top-left vert
    bool g = n.inputVals.size() > 6 && n.inputVals[6];  // mid horiz
    bool dp = n.inputVals.size() > 7 && n.inputVals[7]; // decimal point

    // Precomputed key Y coordinates
    float yTop = faceT;
    float yMid = faceT + faceH * 0.5f;
    float yBot = faceB;

    // Helper — draw a horizontal segment bar
    auto drawH = [&](float cx, float cy, bool on)
    {
      float hw = faceW * 0.5f - thick * 0.5f - gap;
      float hh = thick * 0.5f;
      // Hexagonal end-caps via a simple inset
      float cap = hh * 0.7f;
      ctx.setFillColor(segColor(on));
      ctx.beginPath();
      ctx.moveTo(cx - hw + cap, cy - hh);
      ctx.lineTo(cx + hw - cap, cy - hh);
      ctx.lineTo(cx + hw, cy);
      ctx.lineTo(cx + hw - cap, cy + hh);
      ctx.lineTo(cx - hw + cap, cy + hh);
      ctx.lineTo(cx - hw, cy);
      ctx.closePath();
      ctx.fill();
    };

    // Helper — draw a vertical segment bar
    auto drawV = [&](float cx, float cy, float segH, bool on)
    {
      float hw = thick * 0.5f;
      float hh = segH * 0.5f - thick * 0.5f - gap;
      float cap = hw * 0.7f;
      ctx.setFillColor(segColor(on));
      ctx.beginPath();
      ctx.moveTo(cx - hw, cy - hh + cap);
      ctx.lineTo(cx, cy - hh);
      ctx.lineTo(cx + hw, cy - hh + cap);
      ctx.lineTo(cx + hw, cy + hh - cap);
      ctx.lineTo(cx, cy + hh);
      ctx.lineTo(cx - hw, cy + hh - cap);
      ctx.closePath();
      ctx.fill();
    };

    float digitCX = (faceL + faceR) * 0.5f;
    float halfH = faceH * 0.5f;

    // ── Draw the 7 segments ───────────────────────────────────────────
    // a — top horizontal
    drawH(digitCX, yTop + thick * 0.5f, a);
    // d — bottom horizontal
    drawH(digitCX, yBot - thick * 0.5f, d);
    // g — middle horizontal
    drawH(digitCX, yMid, g);

    // f — top-left vertical
    drawV(faceL + thick * 0.5f, yTop + halfH * 0.5f, halfH, f);
    // b — top-right vertical
    drawV(faceR - thick * 0.5f, yTop + halfH * 0.5f, halfH, b);
    // e — bottom-left vertical
    drawV(faceL + thick * 0.5f, yBot - halfH * 0.5f, halfH, e);
    // c — bottom-right vertical
    drawV(faceR - thick * 0.5f, yBot - halfH * 0.5f, halfH, c);

    // ── Decimal point ─────────────────────────────────────────────────
    float dpCX = faceR + thick * 1.1f;
    float dpCY = yBot - thick * 0.5f;
    float dpR = thick * 0.45f;
    ctx.setFillColor(segColor(dp));
    ctx.fillCircle(dpCX, dpCY, dpR);

    // ── Segment labels (a–g, dp) on input stubs ───────────────────────
    static const char *segLabels[] = {"a", "b", "c", "d", "e", "f", "g", "dp"};
    float fs = 9.f / Z;
    char font[32];
    snprintf(font, sizeof(font), "%.0fpx sans", fs);
    ctx.setFont(font);
    ctx.setTextBaseline(TextBaseline::Middle);

    for (int i = 0; i < 8; ++i)
    {
      Pt p = inputPortPos(n, i);
      // Stub line
      ctx.setStrokeColor(gateStrokeColor());
      ctx.setLineWidth(1.f / Z);
      ctx.beginPath();
      ctx.moveTo(p.x - 6.f / Z, p.y);
      ctx.lineTo(p.x, p.y);
      ctx.stroke();
      // Port dot
      bool lit = (int)n.inputVals.size() > i && n.inputVals[i];
      ctx.setFillColor(lit ? Color::fromRGB(50, 230, 80) : portInColor());
      ctx.fillCircle(p.x, p.y, 4.f / Z);
      // Label to the right of the stub
      ctx.setFillColor(lit
                           ? Color::fromRGBA(50, 230, 80, 200)
                           : Color::fromRGBA(120, 140, 120, 180));
      ctx.fillText(segLabels[i], p.x + 8.f / Z, p.y);
    }
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
    case CircuitNodeType::Clock:
      drawClockShape(ctx, n);
      break;
    case CircuitNodeType::SevenSegment:
      drawSevenSegmentShape(ctx, n);
      return;
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
                              n.type == CircuitNodeType::Output ||
                              n.type == CircuitNodeType::Clock);
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
  void drawClockShape(Canvas2D &ctx, const CircuitNode &n) const
  {
    float x = n.x, y = n.y, w = n.w, h = n.h;
    float r = 4.f / currentZoom_;

    // Box — purple tint when running, dimmed when paused
    Color boxFill = n.clockRunning_
                        ? Color::fromRGBA(20, 15, 35, 255)
                        : Color::fromRGBA(18, 18, 24, 255);
    Color boxStroke = n.selected ? selStrokeColor()
                      : n.clockRunning_
                          ? (n.value ? Color::fromRGB(160, 80, 255) : Color::fromRGB(90, 50, 160))
                          : Color::fromRGBA(60, 60, 75, 255);

    ctx.setFillColor(boxFill);
    ctx.setStrokeColor(boxStroke);
    ctx.setLineWidth((n.selected ? 2.f : 1.5f) / currentZoom_);
    ctx.fillRoundedRect(x, y, w, h, r);
    ctx.strokeRoundedRect(x, y, w, h, r);

    // Draw a tiny square-wave symbol inside the box
    float margin = w * 0.18f;
    float waveX = x + margin;
    float waveW = w - margin * 2.f;
    float midY = y + h * 0.5f;
    float amp = h * 0.22f;
    float seg = waveW / 4.f;

    Color waveCol = n.clockRunning_
                        ? (n.value ? Color::fromRGB(180, 100, 255) : Color::fromRGB(100, 60, 180))
                        : Color::fromRGBA(70, 70, 85, 255);

    ctx.setStrokeColor(waveCol);
    ctx.setLineWidth(2.f / currentZoom_);
    ctx.beginPath();
    ctx.moveTo(waveX, midY + amp);             // low
    ctx.lineTo(waveX + seg, midY + amp);       // low flat
    ctx.lineTo(waveX + seg, midY - amp);       // rising edge
    ctx.lineTo(waveX + seg * 2.f, midY - amp); // high flat
    ctx.lineTo(waveX + seg * 2.f, midY + amp); // falling edge
    ctx.lineTo(waveX + seg * 3.f, midY + amp); // low flat
    ctx.lineTo(waveX + seg * 3.f, midY - amp); // rising edge
    ctx.lineTo(waveX + seg * 4.f, midY - amp); // high flat
    ctx.stroke();

    // Hz label below the box
    float fs = 10.f / currentZoom_;
    char font[32];
    snprintf(font, sizeof(font), "bold %.0fpx sans", fs);
    ctx.setFont(font);
    ctx.setTextBaseline(TextBaseline::Top);

    char hzBuf[32];
    if (n.clockHz_ >= 1.f)
      snprintf(hzBuf, sizeof(hzBuf), "%.0f Hz", n.clockHz_);
    else
      snprintf(hzBuf, sizeof(hzBuf), "%.2f Hz", n.clockHz_);

    ctx.setFillColor(n.clockRunning_
                         ? Color::fromRGBA(160, 100, 255, 200)
                         : Color::fromRGBA(80, 80, 95, 200));
    float tw = ctx.measureText(hzBuf);
    ctx.fillText(hzBuf, x + w * 0.5f - tw * 0.5f, y + h + 2.f / currentZoom_);
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
    if (findBusForWire(w.id))
      return;

    float x0, y0, x1, y1;
    bool active = false;

    // ── Resolve source ────────────────────────────────────────────────
    if (w.fromInstanceId >= 0)
    {
      const SubCircuitInstance *si = findInstanceConst(w.fromInstanceId);
      if (!si)
        return;
      Pt p = instanceOutputPortPos(*si, w.fromPortIdx);
      x0 = p.x;
      y0 = p.y;
      active = (w.fromPortIdx < (int)si->outputVals.size())
                   ? si->outputVals[w.fromPortIdx]
                   : false;
    }
    else
    {
      const CircuitNode *src = findNodeConst(w.fromNodeId);
      if (!src)
        return;
      Pt p = outputPortPos(*src);
      x0 = p.x;
      y0 = p.y;
      active = src->value;
    }

    // ── Resolve destination ───────────────────────────────────────────
    if (w.toInstanceId >= 0)
    {
      const SubCircuitInstance *di = findInstanceConst(w.toInstanceId);
      if (!di)
        return;
      Pt p = instanceInputPortPos(*di, w.toInstancePort);
      x1 = p.x;
      y1 = p.y;
    }
    else
    {
      const CircuitNode *dst = findNodeConst(w.toNodeId);
      if (!dst)
        return;
      Pt p = inputPortPos(*dst, w.toPortIndex);
      x1 = p.x;
      y1 = p.y;
    }

    drawOrthogonalWire(ctx, x0, y0, x1, y1, active, false, w.selected);
  }

  void drawSubCircuitInstance(Canvas2D &ctx,
                              const SubCircuitInstance &inst) const
  {
    const SubCircuitDef *def = findDefConst(inst.defId);
    if (!def)
      return;

    float x = inst.x, y = inst.y, w = inst.w, h = inst.h;
    float Z = currentZoom_;

    // ── Outer box ─────────────────────────────────────────────────────
    Color bodyFill = Color::fromRGB(16, 18, 28);
    Color bodyStroke = inst.selected
                           ? Color::fromRGB(80, 160, 255)
                           : Color::fromRGB(120, 100, 200);
    float lw = (inst.selected ? 2.f : 1.5f) / Z;

    ctx.setFillColor(bodyFill);
    ctx.setStrokeColor(bodyStroke);
    ctx.setLineWidth(lw);
    ctx.fillRoundedRect(x, y, w, h, 6.f / Z);
    ctx.strokeRoundedRect(x, y, w, h, 6.f / Z);

    // ── Header band ───────────────────────────────────────────────────
    float headerH = 320.f;
    ctx.setFillColor(Color::fromRGB(28, 22, 48));
    ctx.fillRoundedRect(x, y, w, headerH, 6.f / Z);
    // Flat bottom on header
    ctx.setFillColor(Color::fromRGB(28, 22, 48));
    ctx.fillRect(x, y + headerH * 0.5f, w, headerH * 0.5f);

    // ── Module name ───────────────────────────────────────────────────
    float fs = 18.f / Z;
    char font[32];
    snprintf(font, sizeof(font), "bold %.0fpx sans", fs);
    ctx.setFont(font);
    ctx.setTextBaseline(TextBaseline::Middle);
    ctx.setFillColor(Color::fromRGB(180, 160, 255));
    const std::string &lbl = inst.label.empty() ? def->name : inst.label;
    float tw = ctx.measureText(lbl.c_str());
    ctx.fillText(lbl.c_str(), x + w * 0.5f - tw * 0.5f, y + headerH * 0.5f);

    // "<<module>>" stereotype label
    float sfs = 11.f / Z;
    char sfont[32];
    snprintf(sfont, sizeof(sfont), "%.0fpx sans", sfs);
    ctx.setFont(sfont);
    ctx.setFillColor(Color::fromRGBA(140, 120, 200, 180));
    float stw = ctx.measureText("<<module>>");
    ctx.fillText("<<module>>",
                 x + w * 0.5f - stw * 0.5f,
                 y + headerH * 0.5f - fs * 1.2f);

    // ── Divider line under header ─────────────────────────────────────
    ctx.setStrokeColor(Color::fromRGBA(120, 100, 200, 120));
    ctx.setLineWidth(0.8f / Z);
    ctx.beginPath();
    ctx.moveTo(x, y + headerH);
    ctx.lineTo(x + w, y + headerH);
    ctx.stroke();

    // ── Input ports (left side) ───────────────────────────────────────
    float pfs = 12.f / Z;
    char pfont[32];
    snprintf(pfont, sizeof(pfont), "%.0fpx sans", pfs);
    ctx.setFont(pfont);
    ctx.setTextBaseline(TextBaseline::Middle);

    int inCount = def->inputPortCount();
    for (int i = 0; i < inCount; ++i)
    {
      Pt p = instanceInputPortPos(inst, i);

      // Find port name
      const SubCircuitPort *port = findInputPort(*def, i);
      std::string portName = port ? port->name : ("in_" + std::to_string(i));

      // Driven value
      bool driven = (i < (int)inst.inputVals.size()) && inst.inputVals[i];

      // Stub line
      ctx.setStrokeColor(Color::fromRGBA(120, 100, 200, 180));
      ctx.setLineWidth(1.f / Z);
      ctx.beginPath();
      ctx.moveTo(p.x - 8.f / Z, p.y);
      ctx.lineTo(p.x, p.y);
      ctx.stroke();

      // Port dot
      ctx.setFillColor(driven ? Color::fromRGB(60, 200, 120)
                              : portInColor());
      ctx.fillCircle(p.x, p.y, 4.f / Z);

      // Port label (inside box, to the right of the dot)
      ctx.setFillColor(driven
                           ? Color::fromRGBA(60, 200, 120, 200)
                           : Color::fromRGBA(160, 140, 220, 200));
      ctx.fillText(portName.c_str(), p.x + 10.f / Z, p.y);
    }

    // ── Output ports (right side) ─────────────────────────────────────
    int outCount = def->outputPortCount();
    for (int i = 0; i < outCount; ++i)
    {
      Pt p = instanceOutputPortPos(inst, i);

      const SubCircuitPort *port = findOutputPort(*def, i);
      std::string portName = port ? port->name : ("out_" + std::to_string(i));

      bool active = (i < (int)inst.outputVals.size()) && inst.outputVals[i];

      // Stub line
      ctx.setStrokeColor(Color::fromRGBA(120, 100, 200, 180));
      ctx.setLineWidth(1.f / Z);
      ctx.beginPath();
      ctx.moveTo(p.x, p.y);
      ctx.lineTo(p.x + 8.f / Z, p.y);
      ctx.stroke();

      // Port dot
      ctx.setFillColor(active ? wireActiveColor() : portOutColor());
      ctx.fillCircle(p.x, p.y, 4.f / Z);

      // Port label (right-aligned, inside box to the left of the dot)
      ctx.setFillColor(active
                           ? Color::fromRGBA(60, 200, 120, 200)
                           : Color::fromRGBA(160, 140, 220, 200));
      float ptw = ctx.measureText(portName.c_str());
      ctx.fillText(portName.c_str(),
                   p.x - 10.f / Z - ptw, p.y);
    }

    // ── Inline text edit overlay (rename) ────────────────────────────
    if (textEditing_ && textEdit_.nodeId == -(inst.instanceId))
      drawInstanceTextEdit(ctx, inst);
  }

  void drawInstanceTextEdit(Canvas2D &ctx,
                            const SubCircuitInstance &inst) const
  {
    float fs = 18.f / currentZoom_;
    char font[32];
    snprintf(font, sizeof(font), "%.0fpx sans", fs);
    ctx.setFont(font);
    ctx.setTextBaseline(TextBaseline::Middle);

    float cy = inst.y + 160.f; // vertically centred in header
    float tw = ctx.measureText(textEdit_.text.c_str());
    float pad = 4.f / currentZoom_;

    // Background pill
    ctx.setFillColor(Color::fromRGBA(20, 15, 40, 230));
    ctx.fillRect(inst.x + inst.w * 0.5f - tw * 0.5f - pad,
                 cy - fs * 0.5f - pad,
                 tw + pad * 2.f + fs,
                 fs + pad * 2.f);

    // Text
    ctx.setFillColor(Color::fromRGB(200, 180, 255));
    ctx.fillText(textEdit_.text.c_str(),
                 inst.x + inst.w * 0.5f - tw * 0.5f, cy);

    // Cursor
    if (textEdit_.cursorVisible)
    {
      ctx.setFillColor(Color::fromRGB(160, 120, 255));
      ctx.fillRect(inst.x + inst.w * 0.5f - tw * 0.5f + tw + 1.f / currentZoom_,
                   cy - fs * 0.5f,
                   1.5f / currentZoom_, fs);
    }
  }

  void drawBusWire(Canvas2D &ctx, const BusWire &b) const
  {
    // Find source and destination nodes
    const CircuitNode *src = findNodeConst(b.fromNodeId);
    const CircuitNode *dst = findNodeConst(b.toNodeId);
    if (!src || !dst)
      return;

    Pt p0 = outputPortPos(*src);
    Pt p1 = inputPortPos(*dst, 0); // bus lands at first port

    // ── Thick bus line ────────────────────────────────────────────────
    const float busLW = 4.f / currentZoom_;
    const float stub = 24.f / currentZoom_;
    const float cr = 6.f / currentZoom_;

    Color busCol = b.selected
                       ? Color::fromRGB(80, 160, 255)
                       : Color::fromRGB(160, 130, 60);

    ctx.setStrokeColor(busCol);
    ctx.setLineWidth(busLW);

    float ax = p0.x + stub, bx = p1.x - stub;
    std::vector<std::pair<float, float>> pts;
    if (ax <= bx)
    {
      float mx = (ax + bx) * 0.5f;
      pts = {{p0.x, p0.y}, {ax, p0.y}, {mx, p0.y}, {mx, p1.y}, {bx, p1.y}, {p1.x, p1.y}};
    }
    else
    {
      float my = (p0.y + p1.y) * 0.5f;
      pts = {{p0.x, p0.y}, {ax, p0.y}, {ax, my}, {bx, my}, {bx, p1.y}, {p1.x, p1.y}};
    }
    ctx.beginPath();
    ctx.moveTo(pts[0].first, pts[0].second);
    roundedPolyline(ctx, pts, cr);
    ctx.stroke();

    // ── Slash notation ────────────────────────────────────────────────
    // Find the midpoint of the bus line for the slash + label
    float midX = (p0.x + p1.x) * 0.5f;
    float midY = (p0.y + p1.y) * 0.5f;

    float slashLen = 8.f / currentZoom_;
    float slashLW = 2.f / currentZoom_;
    ctx.setStrokeColor(busCol);
    ctx.setLineWidth(slashLW);
    ctx.beginPath();
    ctx.moveTo(midX - slashLen, midY + slashLen);
    ctx.lineTo(midX + slashLen, midY - slashLen);
    ctx.stroke();

    // Bit-width label next to the slash
    float fs = 11.f / currentZoom_;
    char font[32];
    snprintf(font, sizeof(font), "%.0fpx sans", fs);
    ctx.setFont(font);
    ctx.setTextBaseline(TextBaseline::Middle);
    ctx.setFillColor(busCol);
    std::string label = std::to_string(b.bitWidth);
    ctx.fillText(label.c_str(),
                 midX + slashLen + 2.f / currentZoom_,
                 midY - slashLen);

    // ── Fan-out tick marks at both ends ───────────────────────────────
    float tickLen = 6.f / currentZoom_;
    ctx.setStrokeColor(busCol);
    ctx.setLineWidth(slashLW);

    // Source end ticks (one per bit)
    for (int i = 0; i < b.bitWidth; ++i)
    {
      float off = (float(i) - float(b.bitWidth - 1) * 0.5f) * (4.f / currentZoom_);
      ctx.beginPath();
      ctx.moveTo(p0.x, p0.y + off - tickLen);
      ctx.lineTo(p0.x + stub * 0.5f, p0.y + off);
      ctx.stroke();
    }
    // Destination end ticks
    for (int i = 0; i < b.bitWidth; ++i)
    {
      float off = (float(i) - float(b.bitWidth - 1) * 0.5f) * (4.f / currentZoom_);
      ctx.beginPath();
      ctx.moveTo(p1.x - stub * 0.5f, p1.y + off);
      ctx.lineTo(p1.x, p1.y + off - tickLen);
      ctx.stroke();
    }
  }

  void drawWirePreview(Canvas2D &ctx) const
  {
    float x0, y0;

    if (wireStart_.nodeId < 0)
    {
      // Started from an instance port
      int iid, pidx;
      decodeInstancePortRef(wireStart_, iid, pidx);
      const SubCircuitInstance *si = findInstanceConst(iid);
      if (!si)
        return;
      Pt p = wireStart_.isOutput
                 ? instanceOutputPortPos(*si, pidx)
                 : instanceInputPortPos(*si, pidx);
      x0 = p.x;
      y0 = p.y;
    }
    else
    {
      const CircuitNode *sn = findNodeConst(wireStart_.nodeId);
      if (!sn)
        return;
      Pt p = wireStart_.isOutput
                 ? outputPortPos(*sn)
                 : inputPortPos(*sn, wireStart_.portIdx);
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
    undoNodes_.push_back({nodes_, wires_, instances_});
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
  std::vector<BusWire> buses_;
  int nextBusId_ = 1;
  int nextId_ = 1;

  // ── Sub-circuit defs + placed instances ───────────────────────────────
  std::vector<SubCircuitDef> defs_;
  std::vector<SubCircuitInstance> instances_;
  int nextDefId_ = 1;
  int nextInstanceId_ = 1;

  struct Snapshot
  {
    std::vector<CircuitNode> nodes;
    std::vector<CircuitWire> wires;
    std::vector<SubCircuitInstance> instances;
  };
  std::vector<Snapshot> undoNodes_, redoNodes_;

  struct ClipboardEntry
  {
    CircuitNodeType type;
    float x, y; // relative to clipboard centroid
    bool value;
    std::string label;
    int clipId; // stable ID used only inside the clipboard to match wire endpoints
    int inputCount_ = 2;
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
        {"", CircuitNodeType::AND, true}, // divider
        {"CLK", CircuitNodeType::Clock},
        {"", CircuitNodeType::AND, true}, // divider
        {"7SEG", CircuitNodeType::SevenSegment},
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
                               SizedBox(2, 0),
                               savePicker,
                               openPicker,
                               verilogPicker,
                               vhdlPicker,
                               pngPicker,
                               svgPicker,
                               SizedBox(2, 0),
                               undoBtn,
                               SizedBox(2, 0),
                               redoBtn,
                               SizedBox(2, 0),
                               copyBtn,
                               SizedBox(2, 0),
                               pasteBtn,
                               SizedBox(2, 0),
                               dupBtn,
                               SizedBox(2, 0),
                               clrBtn,
                               SizedBox(2, 0),
                               fitBtn,
                               SizedBox(2, 0),
                               rstBtn,
                               SizedBox(2, 0),
                               ttBtn,
                               SizedBox(2, 0),
                               Container(modeRow)->setHeight(30),
                           })
                           ->setPadding(1)
                           ->setSpacing(1)
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