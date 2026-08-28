#include "Json.hpp"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

namespace mjnexus {

// ============================================================
// 构造 / 工厂
// ============================================================

JsonValue::JsonValue()                    : m_data(std::monostate{}) {}
JsonValue::JsonValue(std::nullptr_t)     : m_data(std::monostate{}) {}
JsonValue::JsonValue(bool b)              : m_data(b) {}
JsonValue::JsonValue(double d)            : m_data(d) {}
JsonValue::JsonValue(int64_t i)           : m_data((double)i) {}
JsonValue::JsonValue(const std::string& s): m_data(s) {}
JsonValue::JsonValue(std::string&& s)     : m_data(std::move(s)) {}
JsonValue::JsonValue(const char* s)       : m_data(std::string(s ? s : "")) {}
JsonValue::JsonValue(std::vector<JsonValue> arr) : m_data(std::move(arr)) {}
JsonValue::JsonValue(std::map<std::string, JsonValue> obj) : m_data(std::move(obj)) {}

JsonValue JsonValue::make_object() {
    return JsonValue{ObjectMap{}};
}
JsonValue JsonValue::make_array() {
    return JsonValue{ArrayVec{}};
}

// ============================================================
// 类型查询
// ============================================================

JsonValue::Type JsonValue::type() const {
    if (std::holds_alternative<std::monostate>(m_data)) return Type::Null;
    if (std::holds_alternative<bool>(m_data))            return Type::Bool;
    if (std::holds_alternative<double>(m_data))          return Type::Number;
    if (std::holds_alternative<std::string>(m_data))     return Type::String;
    if (std::holds_alternative<ArrayVec>(m_data))        return Type::Array;
    if (std::holds_alternative<ObjectMap>(m_data))      return Type::Object;
    return Type::Null;
}

bool JsonValue::is_null()   const { return std::holds_alternative<std::monostate>(m_data); }
bool JsonValue::is_bool()   const { return std::holds_alternative<bool>(m_data); }
bool JsonValue::is_number() const { return std::holds_alternative<double>(m_data); }
bool JsonValue::is_string() const { return std::holds_alternative<std::string>(m_data); }
bool JsonValue::is_array()  const { return std::holds_alternative<ArrayVec>(m_data); }
bool JsonValue::is_object() const { return std::holds_alternative<ObjectMap>(m_data); }

// ============================================================
// 值提取
// ============================================================

std::string JsonValue::as_string() const {
    if (auto* p = std::get_if<std::string>(&m_data)) return *p;
    if (auto* p = std::get_if<double>(&m_data)) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%g", *p);
        return buf;
    }
    return {};
}

double JsonValue::as_number() const {
    if (auto* p = std::get_if<double>(&m_data)) return *p;
    if (auto* p = std::get_if<bool>(&m_data))   return *p ? 1.0 : 0.0;
    if (auto* p = std::get_if<std::string>(&m_data)) {
        char* endp = nullptr;
        double v = std::strtod(p->c_str(), &endp);
        if (endp != p->c_str()) return v;
    }
    return 0.0;
}

bool JsonValue::as_bool() const {
    if (auto* p = std::get_if<bool>(&m_data))   return *p;
    if (auto* p = std::get_if<double>(&m_data)) return *p != 0.0;
    if (auto* p = std::get_if<std::string>(&m_data)) return !p->empty();
    return false;
}

const std::vector<JsonValue>& JsonValue::as_array() const {
    static const ArrayVec empty;
    if (auto* p = std::get_if<ArrayVec>(&m_data)) return *p;
    return empty;
}

const std::map<std::string, JsonValue>& JsonValue::as_object() const {
    static const ObjectMap empty;
    if (auto* p = std::get_if<ObjectMap>(&m_data)) return *p;
    return empty;
}

std::vector<JsonValue>& JsonValue::as_array_mut() {
    if (!is_array()) m_data = ArrayVec{};
    return std::get<ArrayVec>(m_data);
}

std::map<std::string, JsonValue>& JsonValue::as_object_mut() {
    if (!is_object()) m_data = ObjectMap{};
    return std::get<ObjectMap>(m_data);
}

// ============================================================
// 便捷访问
// ============================================================

bool JsonValue::has(const std::string& key) const {
    if (auto* p = std::get_if<ObjectMap>(&m_data)) {
        return p->count(key) != 0;
    }
    return false;
}

JsonValue& JsonValue::operator[](const std::string& key) {
    auto& obj = as_object_mut();
    return obj[key];
}

const JsonValue& JsonValue::operator[](const std::string& key) const {
    static const JsonValue null_val;
    if (auto* p = std::get_if<ObjectMap>(&m_data)) {
        auto it = p->find(key);
        if (it != p->end()) return it->second;
    }
    return null_val;
}

JsonValue& JsonValue::operator[](size_t idx) {
    auto& arr = as_array_mut();
    if (idx >= arr.size()) arr.resize(idx + 1);
    return arr[idx];
}

const JsonValue& JsonValue::operator[](size_t idx) const {
    static const JsonValue null_val;
    if (auto* p = std::get_if<ArrayVec>(&m_data)) {
        if (idx < p->size()) return (*p)[idx];
    }
    return null_val;
}

size_t JsonValue::size() const {
    if (auto* p = std::get_if<ObjectMap>(&m_data)) return p->size();
    if (auto* p = std::get_if<ArrayVec>(&m_data))  return p->size();
    return 0;
}

// ============================================================
// 匿名命名空间：递归下降解析器 + string 转义
// ============================================================

namespace {

inline void skip_ws(const std::string& s, size_t& pos) {
    while (pos < s.size() && std::isspace((unsigned char)s[pos])) ++pos;
}

inline bool parse_string(const std::string& s, size_t& pos,
                          std::string& out, std::string& err) {
    if (pos >= s.size() || s[pos] != '"') {
        err = "expected '\"' at offset " + std::to_string(pos);
        return false;
    }
    ++pos;
    while (pos < s.size()) {
        char c = s[pos++];
        if (c == '"') return true;
        if (c == '\\') {
            if (pos >= s.size()) { err = "bad escape"; return false; }
            char e = s[pos++];
            switch (e) {
                case '"':  out += '"'; break;
                case '\\': out += '\\'; break;
                case '/':  out += '/'; break;
                case 'b':  out += '\b'; break;
                case 'f':  out += '\f'; break;
                case 'n':  out += '\n'; break;
                case 'r':  out += '\r'; break;
                case 't':  out += '\t'; break;
                case 'u': {
                    if (pos + 4 > s.size()) { err = "bad \\u escape"; return false; }
                    char hex[5] = {s[pos], s[pos+1], s[pos+2], s[pos+3], 0};
                    unsigned codepoint = (unsigned)std::strtoul(hex, nullptr, 16);
                    pos += 4;
                    if (codepoint < 0x80) out += (char)codepoint;
                    else if (codepoint < 0x800) {
                        out += (char)(0xC0 | (codepoint >> 6));
                        out += (char)(0x80 | (codepoint & 0x3F));
                    } else {
                        out += (char)(0xE0 | (codepoint >> 12));
                        out += (char)(0x80 | ((codepoint >> 6) & 0x3F));
                        out += (char)(0x80 | (codepoint & 0x3F));
                    }
                    break;
                }
                default:
                    err = std::string("unknown escape \\") + e;
                    return false;
            }
        } else {
            out += c;
        }
    }
    err = "unterminated string";
    return false;
}

inline bool parse_number(const std::string& s, size_t& pos,
                          double& out, std::string& err) {
    size_t start = pos;
    if (pos < s.size() && (s[pos] == '-' || s[pos] == '+')) ++pos;
    if (pos < s.size() && s[pos] == '0') {
        ++pos;
    } else {
        while (pos < s.size() && std::isdigit((unsigned char)s[pos])) ++pos;
    }
    if (pos < s.size() && s[pos] == '.') {
        ++pos;
        while (pos < s.size() && std::isdigit((unsigned char)s[pos])) ++pos;
    }
    if (pos < s.size() && (s[pos] == 'e' || s[pos] == 'E')) {
        ++pos;
        if (pos < s.size() && (s[pos] == '+' || s[pos] == '-')) ++pos;
        while (pos < s.size() && std::isdigit((unsigned char)s[pos])) ++pos;
    }
    if (start == pos) { err = "bad number"; return false; }
    out = std::strtod(s.c_str() + start, nullptr);
    return true;
}

inline bool parse_value_inner(const std::string& s, size_t& pos,
                               JsonValue& out, std::string& err);

inline bool parse_object_inner(const std::string& s, size_t& pos,
                                JsonValue& out, std::string& err) {
    JsonValue::ObjectMap obj;
    if (pos >= s.size() || s[pos] != '{') { err = "expected '{'"; return false; }
    ++pos;
    skip_ws(s, pos);
    if (pos < s.size() && s[pos] == '}') { ++pos; out = JsonValue{obj}; return true; }

    while (true) {
        skip_ws(s, pos);
        std::string key;
        if (!parse_string(s, pos, key, err)) return false;
        skip_ws(s, pos);
        if (pos >= s.size() || s[pos] != ':') { err = "expected ':' after key"; return false; }
        ++pos;
        skip_ws(s, pos);
        JsonValue val;
        if (!parse_value_inner(s, pos, val, err)) return false;
        obj[key] = std::move(val);

        skip_ws(s, pos);
        if (pos < s.size() && s[pos] == ',') { ++pos; continue; }
        if (pos < s.size() && s[pos] == '}') { ++pos; break; }
        err = "expected ',' or '}'";
        return false;
    }
    out = JsonValue{std::move(obj)};
    return true;
}

inline bool parse_array_inner(const std::string& s, size_t& pos,
                               JsonValue& out, std::string& err) {
    JsonValue::ArrayVec arr;
    if (pos >= s.size() || s[pos] != '[') { err = "expected '['"; return false; }
    ++pos;
    skip_ws(s, pos);
    if (pos < s.size() && s[pos] == ']') { ++pos; out = JsonValue{arr}; return true; }

    while (true) {
        skip_ws(s, pos);
        JsonValue val;
        if (!parse_value_inner(s, pos, val, err)) return false;
        arr.push_back(std::move(val));

        skip_ws(s, pos);
        if (pos < s.size() && s[pos] == ',') { ++pos; continue; }
        if (pos < s.size() && s[pos] == ']') { ++pos; break; }
        err = "expected ',' or ']'";
        return false;
    }
    out = JsonValue{std::move(arr)};
    return true;
}

inline bool parse_value_inner(const std::string& s, size_t& pos,
                               JsonValue& out, std::string& err) {
    skip_ws(s, pos);
    if (pos >= s.size()) { err = "unexpected end of JSON"; return false; }
    char c = s[pos];
    if (c == '"') {
        std::string str;
        if (!parse_string(s, pos, str, err)) return false;
        out = JsonValue{std::move(str)};
        return true;
    }
    if (c == '{') return parse_object_inner(s, pos, out, err);
    if (c == '[') return parse_array_inner(s, pos, out, err);
    if (c == 't' || c == 'f') {
        if (s.compare(pos, 4, "true") == 0) { pos += 4; out = JsonValue{true}; return true; }
        if (s.compare(pos, 5, "false") == 0) { pos += 5; out = JsonValue{false}; return true; }
        err = "bad bool literal"; return false;
    }
    if (c == 'n') {
        if (s.compare(pos, 4, "null") == 0) { pos += 4; out = JsonValue{}; return true; }
        err = "bad null literal"; return false;
    }
    if (c == '-' || c == '+' || std::isdigit((unsigned char)c)) {
        double num;
        if (!parse_number(s, pos, num, err)) return false;
        out = JsonValue{num};
        return true;
    }
    err = std::string("unexpected char '") + c + "'";
    return false;
}

inline std::string escape_json_string(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    for (unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += (char)c;
                }
        }
    }
    return out;
}

} // namespace (anonymous)

// ============================================================
// 文件 I/O（直接调用匿名命名空间里的解析器）
// ============================================================

JsonValue JsonValue::parse_file(const std::string& path, std::string* error) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs.is_open()) {
        if (error) *error = "cannot open file: " + path;
        return JsonValue{};
    }
    std::ostringstream ss;
    ss << ifs.rdbuf();
    std::string content = ss.str();

    size_t pos = 0;
    JsonValue root;
    std::string parse_err;
    if (!parse_value_inner(content, pos, root, parse_err)) {
        if (error) *error = parse_err;
        return JsonValue{};
    }
    while (pos < content.size() && std::isspace((unsigned char)content[pos])) ++pos;
    if (pos != content.size()) {
        if (error) *error = "trailing garbage after JSON at offset " + std::to_string(pos);
        return JsonValue{};
    }
    return root;
}

bool JsonValue::write_file(const std::string& path, std::string* error) const {
    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    if (!ofs.is_open()) {
        if (error) *error = "cannot create file: " + path;
        return false;
    }
    ofs << to_string(0) << "\n";
    ofs.close();
    return true;
}

// to_string：2 空格缩进
std::string JsonValue::to_string(int indent) const {
    const std::string pad(indent * 2, ' ');
    const std::string pad1((indent + 1) * 2, ' ');

    switch (type()) {
        case Type::Null:  return "null";
        case Type::Bool:  return as_bool() ? "true" : "false";
        case Type::Number: {
            double n = as_number();
            if (n == (double)(int64_t)n && n < 1e15 && n > -1e15) {
                return std::to_string((int64_t)n);
            }
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%.17g", n);
            return buf;
        }
        case Type::String: return "\"" + escape_json_string(as_string()) + "\"";
        case Type::Array: {
            const auto& arr = as_array();
            if (arr.empty()) return "[]";
            std::string out = "[\n";
            for (size_t i = 0; i < arr.size(); ++i) {
                out += pad1 + arr[i].to_string(indent + 1);
                if (i + 1 != arr.size()) out += ",";
                out += "\n";
            }
            out += pad + "]";
            return out;
        }
        case Type::Object: {
            const auto& obj = as_object();
            if (obj.empty()) return "{}";
            std::string out = "{\n";
            size_t i = 0;
            for (const auto& kv : obj) {
                out += pad1 + "\"" + escape_json_string(kv.first) + "\": "
                    + kv.second.to_string(indent + 1);
                if (++i != obj.size()) out += ",";
                out += "\n";
            }
            out += pad + "}";
            return out;
        }
    }
    return "null";
}

} // namespace mjnexus
