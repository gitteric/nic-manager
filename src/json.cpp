#include "json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace json {

const Value* Value::Find(const std::string& key) const {
    for (const auto& kv : m_obj)
        if (kv.first == key) return &kv.second;
    return nullptr;
}

const Value& Value::operator[](const std::string& key) const {
    static const Value kNull;
    const Value* v = Find(key);
    return v ? *v : kNull;
}

Value& Value::Set(const std::string& key, Value v) {
    m_type = Type::Object;
    for (auto& kv : m_obj)
        if (kv.first == key) { kv.second = std::move(v); return kv.second; }
    m_obj.emplace_back(key, std::move(v));
    return m_obj.back().second;
}

// ---- writer -------------------------------------------------------------

namespace {

void WriteString(std::string& out, const std::string& s) {
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
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
    out += '"';
}

void WriteValue(std::string& out, const Value& v, bool pretty, int depth) {
    auto newline = [&](int d) {
        if (!pretty) return;
        out += '\n';
        out.append((size_t)d * 2, ' ');
    };
    switch (v.type()) {
    case Value::Type::Null: out += "null"; break;
    case Value::Type::Bool: out += v.AsBool() ? "true" : "false"; break;
    case Value::Type::Number: {
        double n = v.AsNumber();
        char buf[32];
        if (std::fabs(n) < 1e15 && n == std::floor(n))
            std::snprintf(buf, sizeof(buf), "%lld", (long long)n);
        else
            std::snprintf(buf, sizeof(buf), "%.17g", n);
        out += buf;
        break;
    }
    case Value::Type::String: WriteString(out, v.AsString()); break;
    case Value::Type::Array: {
        if (v.Items().empty()) { out += "[]"; break; }
        out += '[';
        bool first = true;
        for (const auto& item : v.Items()) {
            if (!first) out += ',';
            first = false;
            newline(depth + 1);
            WriteValue(out, item, pretty, depth + 1);
        }
        newline(depth);
        out += ']';
        break;
    }
    case Value::Type::Object: {
        if (v.Members().empty()) { out += "{}"; break; }
        out += '{';
        bool first = true;
        for (const auto& kv : v.Members()) {
            if (!first) out += ',';
            first = false;
            newline(depth + 1);
            WriteString(out, kv.first);
            out += pretty ? ": " : ":";
            WriteValue(out, kv.second, pretty, depth + 1);
        }
        newline(depth);
        out += '}';
        break;
    }
    }
}

// ---- parser -------------------------------------------------------------

struct Parser {
    std::string_view s;
    size_t i = 0;
    std::string err;

    bool Fail(const char* msg) {
        if (err.empty()) err = std::string(msg) + " at offset " + std::to_string(i);
        return false;
    }

    void Ws() {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i;
    }

    bool Literal(const char* lit) {
        size_t n = std::char_traits<char>::length(lit);
        if (s.substr(i, n) != lit) return false;
        i += n;
        return true;
    }

    static void AppendUtf8(std::string& out, unsigned cp) {
        if (cp < 0x80) {
            out += (char)cp;
        } else if (cp < 0x800) {
            out += (char)(0xC0 | (cp >> 6));
            out += (char)(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += (char)(0xE0 | (cp >> 12));
            out += (char)(0x80 | ((cp >> 6) & 0x3F));
            out += (char)(0x80 | (cp & 0x3F));
        } else {
            out += (char)(0xF0 | (cp >> 18));
            out += (char)(0x80 | ((cp >> 12) & 0x3F));
            out += (char)(0x80 | ((cp >> 6) & 0x3F));
            out += (char)(0x80 | (cp & 0x3F));
        }
    }

    bool Hex4(unsigned& cp) {
        if (i + 4 > s.size()) return Fail("truncated \\u escape");
        cp = 0;
        for (int k = 0; k < 4; ++k) {
            char c = s[i++];
            cp <<= 4;
            if (c >= '0' && c <= '9') cp |= (unsigned)(c - '0');
            else if (c >= 'a' && c <= 'f') cp |= (unsigned)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') cp |= (unsigned)(c - 'A' + 10);
            else return Fail("bad \\u escape");
        }
        return true;
    }

    bool String(std::string& out) {
        if (i >= s.size() || s[i] != '"') return Fail("expected string");
        ++i;
        while (i < s.size()) {
            char c = s[i++];
            if (c == '"') return true;
            if ((unsigned char)c < 0x20) return Fail("control character in string");
            if (c != '\\') { out += c; continue; }
            if (i >= s.size()) break;
            char e = s[i++];
            switch (e) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                unsigned cp = 0;
                if (!Hex4(cp)) return false;
                if (cp >= 0xD800 && cp <= 0xDBFF) {
                    unsigned lo = 0;
                    if (i + 2 > s.size() || s[i] != '\\' || s[i + 1] != 'u') return Fail("lone surrogate");
                    i += 2;
                    if (!Hex4(lo)) return false;
                    if (lo < 0xDC00 || lo > 0xDFFF) return Fail("bad surrogate pair");
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                }
                AppendUtf8(out, cp);
                break;
            }
            default: return Fail("bad escape");
            }
        }
        return Fail("unterminated string");
    }

    bool Number(Value& out) {
        size_t start = i;
        while (i < s.size() && (std::string_view("+-.eE0123456789").find(s[i]) != std::string_view::npos)) ++i;
        std::string tmp(s.substr(start, i - start));
        char* end = nullptr;
        double d = std::strtod(tmp.c_str(), &end);
        if (tmp.empty() || end != tmp.c_str() + tmp.size()) return Fail("bad number");
        out = Value(d);
        return true;
    }

    bool Parse(Value& out, int depth) {
        if (depth > 64) return Fail("nesting too deep");
        Ws();
        if (i >= s.size()) return Fail("unexpected end of input");
        char c = s[i];
        if (c == '{') {
            ++i;
            out = Value::Object();
            Ws();
            if (i < s.size() && s[i] == '}') { ++i; return true; }
            for (;;) {
                Ws();
                std::string key;
                if (!String(key)) return false;
                Ws();
                if (i >= s.size() || s[i] != ':') return Fail("expected ':'");
                ++i;
                Value v;
                if (!Parse(v, depth + 1)) return false;
                out.Set(key, std::move(v));
                Ws();
                if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == '}') { ++i; return true; }
                return Fail("expected ',' or '}'");
            }
        }
        if (c == '[') {
            ++i;
            out = Value::Array();
            Ws();
            if (i < s.size() && s[i] == ']') { ++i; return true; }
            for (;;) {
                Value v;
                if (!Parse(v, depth + 1)) return false;
                out.Push(std::move(v));
                Ws();
                if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == ']') { ++i; return true; }
                return Fail("expected ',' or ']'");
            }
        }
        if (c == '"') {
            std::string str;
            if (!String(str)) return false;
            out = Value(std::move(str));
            return true;
        }
        if (Literal("true")) { out = Value(true); return true; }
        if (Literal("false")) { out = Value(false); return true; }
        if (Literal("null")) { out = Value(); return true; }
        if (c == '-' || (c >= '0' && c <= '9')) return Number(out);
        return Fail("unexpected character");
    }
};

}  // namespace

std::string Write(const Value& v, bool pretty) {
    std::string out;
    WriteValue(out, v, pretty, 0);
    if (pretty) out += '\n';
    return out;
}

bool Parse(std::string_view text, Value& out, std::string* error) {
    if (text.size() >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB &&
        (unsigned char)text[2] == 0xBF)
        text.remove_prefix(3);  // UTF-8 BOM
    Parser p;
    p.s = text;
    Value v;
    bool ok = p.Parse(v, 0);
    if (ok) {
        p.Ws();
        if (p.i != text.size()) ok = p.Fail("trailing characters");
    }
    if (!ok) {
        if (error) *error = p.err;
        return false;
    }
    out = std::move(v);
    return true;
}

}  // namespace json
