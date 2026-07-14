// flux_json.hpp
#pragma once
#include <cstdio>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

// ============================================================================
// JSON VALUE
// ============================================================================

struct JsonValue;

using JsonNull = std::monostate;
using JsonBool = bool;
using JsonNumber = double;
using JsonString = std::string;
using JsonArray = std::vector<JsonValue>;
using JsonObject = std::map<std::string, JsonValue>;

struct JsonValue
{
    std::variant<JsonNull, JsonBool, JsonNumber,
                 JsonString, JsonArray, JsonObject>
        data;

    // ── Construction ──────────────────────────────────────────────────────
    JsonValue() = default;
    JsonValue(std::nullptr_t) {}
    JsonValue(bool b) : data(b) {}
    JsonValue(int i) : data(static_cast<double>(i)) {}
    JsonValue(double d) : data(d) {}
    JsonValue(const char* s) : data(std::string(s)) {}
    JsonValue(std::string s) : data(std::move(s)) {}
    JsonValue(JsonArray a) : data(std::move(a)) {}
    JsonValue(JsonObject o) : data(std::move(o)) {}

    // Enables: json::object() / json obj = { {"key", value}, ... };
    JsonValue(std::initializer_list<std::pair<const std::string, JsonValue>> init)
    {
        JsonObject obj;
        for (auto& kv : init) obj.emplace(kv.first, kv.second);
        data = std::move(obj);
    }

    static JsonValue object() { return JsonValue(JsonObject{}); }
    static JsonValue array() { return JsonValue(JsonArray{}); }

    // ── Type checks ──────────────────────────────────────────────────────────
    bool isNull() const { return std::holds_alternative<JsonNull>(data); }
    bool isBool() const { return std::holds_alternative<JsonBool>(data); }
    bool isNumber() const { return std::holds_alternative<JsonNumber>(data); }
    bool isString() const { return std::holds_alternative<JsonString>(data); }
    bool isArray() const { return std::holds_alternative<JsonArray>(data); }
    bool isObject() const { return std::holds_alternative<JsonObject>(data); }

    // ── Accessors (throw on wrong type) ──────────────────────────────────────
    bool asBool() const { return std::get<JsonBool>(data); }
    double asNumber() const { return std::get<JsonNumber>(data); }
    int asInt() const { return (int)std::get<JsonNumber>(data); }
    const std::string &asString() const { return std::get<JsonString>(data); }
    const JsonArray &asArray() const { return std::get<JsonArray>(data); }
    const JsonObject &asObject() const { return std::get<JsonObject>(data); }

    // ── Safe accessors (return default on wrong type) ─────────────────────────
    bool getBool(bool def = false) const noexcept
    {
        return isBool() ? asBool() : def;
    }
    double getNumber(double def = 0.0) const noexcept
    {
        return isNumber() ? asNumber() : def;
    }
    int getInt(int def = 0) const noexcept
    {
        return isNumber() ? asInt() : def;
    }
    std::string getString(const std::string &def = {}) const noexcept
    {
        return isString() ? asString() : def;
    }

    // nlohmann-style .value(key, default) for object members
    std::string value(const std::string& key, const std::string& def) const
    {
        const JsonValue* v = get(key);
        return (v && v->isString()) ? v->asString() : def;
    }

    // ── Object key access (read-only, throws if missing) ──────────────────────
    const JsonValue &operator[](const std::string &key) const
    {
        auto &obj = asObject();
        auto it = obj.find(key);
        if (it == obj.end())
            throw std::out_of_range("Key not found: " + key);
        return it->second;
    }

    // ── Object key access (mutable, auto-vivifies) ─────────────────────────────
    JsonValue &operator[](const std::string &key)
    {
        if (!isObject()) data = JsonObject{};
        return std::get<JsonObject>(data)[key];
    }

    bool has(const std::string &key) const noexcept
    {
        return isObject() && asObject().count(key);
    }
    bool contains(const std::string &key) const noexcept { return has(key); }

    void erase(const std::string &key)
    {
        if (isObject()) std::get<JsonObject>(data).erase(key);
    }

    // Returns nullptr-like value if key missing
    const JsonValue *get(const std::string &key) const noexcept
    {
        if (!isObject())
            return nullptr;
        auto it = asObject().find(key);
        return (it != asObject().end()) ? &it->second : nullptr;
    }

    // ── Array index access ────────────────────────────────────────────────────
    const JsonValue &operator[](size_t i) const { return asArray()[i]; }
    size_t size() const noexcept
    {
        if (isArray())
            return asArray().size();
        if (isObject())
            return asObject().size();
        return 0;
    }

    // ── Serialization ──────────────────────────────────────────────────────
    // dump(-1) => compact, dump(N) => pretty-printed with N-space indent
    std::string dump(int indent = -1) const
    {
        std::string out;
        dumpImpl(out, indent, 0);
        return out;
    }

private:
    static void appendEscaped(std::string &out, const std::string &s)
    {
        out += '"';
        for (char c : s)
        {
            switch (c)
            {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20)
                {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                }
                else
                {
                    out += c;
                }
            }
        }
        out += '"';
    }

    static void appendNumber(std::string &out, double d)
    {
        if (d == static_cast<long long>(d))
        {
            out += std::to_string(static_cast<long long>(d));
        }
        else
        {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%g", d);
            out += buf;
        }
    }

    void dumpImpl(std::string &out, int indent, int depth) const
    {
        const bool pretty = indent >= 0;
        const std::string pad = pretty ? std::string((depth + 1) * indent, ' ') : std::string();
        const std::string closePad = pretty ? std::string(depth * indent, ' ') : std::string();
        const std::string nl = pretty ? "\n" : "";
        const std::string colon = pretty ? ": " : ":";

        if (isNull()) { out += "null"; return; }
        if (isBool()) { out += asBool() ? "true" : "false"; return; }
        if (isNumber()) { appendNumber(out, asNumber()); return; }
        if (isString()) { appendEscaped(out, asString()); return; }

        if (isArray())
        {
            const auto &arr = asArray();
            if (arr.empty()) { out += "[]"; return; }
            out += "["; out += nl;
            for (size_t i = 0; i < arr.size(); ++i)
            {
                out += pad;
                arr[i].dumpImpl(out, indent, depth + 1);
                if (i + 1 < arr.size()) out += ",";
                out += nl;
            }
            out += closePad; out += "]";
            return;
        }

        if (isObject())
        {
            const auto &obj = asObject();
            if (obj.empty()) { out += "{}"; return; }
            out += "{"; out += nl;
            size_t i = 0;
            for (const auto &[key, val] : obj)
            {
                out += pad;
                appendEscaped(out, key);
                out += colon;
                val.dumpImpl(out, indent, depth + 1);
                if (++i < obj.size()) out += ",";
                out += nl;
            }
            out += closePad; out += "}";
            return;
        }
    }
};


// ============================================================================
// PARSER
// ============================================================================

class JsonParser
{
public:
    static JsonValue parse(const std::string &input)
    {
        JsonParser p(input);
        p.skipWs();
        auto v = p.parseValue();
        p.skipWs();
        if (p.pos_ < p.src_.size())
            throw std::runtime_error("Unexpected trailing content");
        return v;
    }

    // Returns nullopt-style empty JsonValue on failure instead of throwing
    static bool tryParse(const std::string &input, JsonValue &out) noexcept
    {
        try
        {
            out = parse(input);
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

private:
    const std::string &src_;
    size_t pos_ = 0;

    explicit JsonParser(const std::string &src) : src_(src) {}

    char peek() const { return pos_ < src_.size() ? src_[pos_] : '\0'; }
    char consume() { return src_[pos_++]; }

    void skipWs()
    {
        while (pos_ < src_.size() && (src_[pos_] == ' ' || src_[pos_] == '\t' || src_[pos_] == '\n' || src_[pos_] == '\r'))
            ++pos_;
    }
    void expect(char c)
    {
        if (consume() != c)
            throw std::runtime_error(std::string("Expected '") + c + "'");
    }

    JsonValue parseValue()
    {
        skipWs();
        char c = peek();
        if (c == '"')
            return parseString();
        if (c == '{')
            return parseObject();
        if (c == '[')
            return parseArray();
        if (c == 't' || c == 'f')
            return parseBool();
        if (c == 'n')
            return parseNull();
        if (c == '-' || std::isdigit((unsigned char)c))
            return parseNumber();
        throw std::runtime_error(std::string("Unexpected character: ") + c);
    }

    JsonValue parseString()
    {
        expect('"');
        std::string result;
        while (pos_ < src_.size() && src_[pos_] != '"')
        {
            if (src_[pos_] == '\\')
            {
                ++pos_;
                char esc = consume();
                switch (esc)
                {
                case '"':
                    result += '"';
                    break;
                case '\\':
                    result += '\\';
                    break;
                case '/':
                    result += '/';
                    break;
                case 'n':
                    result += '\n';
                    break;
                case 'r':
                    result += '\r';
                    break;
                case 't':
                    result += '\t';
                    break;
                case 'b':
                    result += '\b';
                    break;
                case 'f':
                    result += '\f';
                    break;
                case 'u':
                {
                    // Basic \uXXXX → char (ASCII range only)
                    std::string hex = src_.substr(pos_, 4);
                    pos_ += 4;
                    result += (char)std::stoi(hex, nullptr, 16);
                    break;
                }
                default:
                    result += esc;
                }
            }
            else
            {
                result += src_[pos_++];
            }
        }
        expect('"');
        JsonValue v;
        v.data = result;
        return v;
    }

    JsonValue parseNumber()
    {
        size_t start = pos_;
        if (src_[pos_] == '-')
            ++pos_;
        while (pos_ < src_.size() && std::isdigit((unsigned char)src_[pos_]))
            ++pos_;
        if (pos_ < src_.size() && src_[pos_] == '.')
        {
            ++pos_;
            while (pos_ < src_.size() && std::isdigit((unsigned char)src_[pos_]))
                ++pos_;
        }
        if (pos_ < src_.size() && (src_[pos_] == 'e' || src_[pos_] == 'E'))
        {
            ++pos_;
            if (pos_ < src_.size() && (src_[pos_] == '+' || src_[pos_] == '-'))
                ++pos_;
            while (pos_ < src_.size() && std::isdigit((unsigned char)src_[pos_]))
                ++pos_;
        }
        double d = std::stod(src_.substr(start, pos_ - start));
        JsonValue v;
        v.data = d;
        return v;
    }

    JsonValue parseBool()
    {
        if (src_.substr(pos_, 4) == "true")
        {
            pos_ += 4;
            JsonValue v;
            v.data = true;
            return v;
        }
        if (src_.substr(pos_, 5) == "false")
        {
            pos_ += 5;
            JsonValue v;
            v.data = false;
            return v;
        }
        throw std::runtime_error("Invalid boolean literal");
    }

    JsonValue parseNull()
    {
        if (src_.substr(pos_, 4) == "null")
        {
            pos_ += 4;
            JsonValue v;
            return v;
        }
        throw std::runtime_error("Invalid null literal");
    }

    JsonValue parseArray()
    {
        expect('[');
        JsonArray arr;
        skipWs();
        if (peek() == ']')
        {
            consume();
            JsonValue v;
            v.data = arr;
            return v;
        }
        while (true)
        {
            arr.push_back(parseValue());
            skipWs();
            if (peek() == ']')
            {
                consume();
                break;
            }
            expect(',');
        }
        JsonValue v;
        v.data = std::move(arr);
        return v;
    }

    JsonValue parseObject()
    {
        expect('{');
        JsonObject obj;
        skipWs();
        if (peek() == '}')
        {
            consume();
            JsonValue v;
            v.data = obj;
            return v;
        }
        while (true)
        {
            skipWs();
            auto keyVal = parseString();
            const std::string &key = keyVal.asString();
            skipWs();
            expect(':');
            obj[key] = parseValue();
            skipWs();
            if (peek() == '}')
            {
                consume();
                break;
            }
            expect(',');
        }
        JsonValue v;
        v.data = std::move(obj);
        return v;
    }
};