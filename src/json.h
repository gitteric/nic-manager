// Minimal JSON value, parser and writer (UTF-8). Enough for settings and
// preset files; avoids pulling in a third-party dependency.
#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace json {

class Value {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Value() = default;
    Value(bool b) : m_type(Type::Bool), m_bool(b) {}
    Value(int n) : m_type(Type::Number), m_num(n) {}
    Value(double n) : m_type(Type::Number), m_num(n) {}
    Value(const char* s) : m_type(Type::String), m_str(s) {}
    Value(std::string s) : m_type(Type::String), m_str(std::move(s)) {}

    static Value Array() { Value v; v.m_type = Type::Array; return v; }
    static Value Object() { Value v; v.m_type = Type::Object; return v; }

    Type type() const { return m_type; }
    bool IsNull() const { return m_type == Type::Null; }
    bool IsBool() const { return m_type == Type::Bool; }
    bool IsNumber() const { return m_type == Type::Number; }
    bool IsString() const { return m_type == Type::String; }
    bool IsArray() const { return m_type == Type::Array; }
    bool IsObject() const { return m_type == Type::Object; }

    bool AsBool(bool def = false) const { return IsBool() ? m_bool : def; }
    double AsNumber(double def = 0) const { return IsNumber() ? m_num : def; }
    int AsInt(int def = 0) const { return IsNumber() ? (int)m_num : def; }
    std::string AsString(const std::string& def = {}) const { return IsString() ? m_str : def; }

    // Arrays
    const std::vector<Value>& Items() const { return m_arr; }
    void Push(Value v) { m_type = Type::Array; m_arr.push_back(std::move(v)); }

    // Objects (insertion ordered)
    const std::vector<std::pair<std::string, Value>>& Members() const { return m_obj; }
    const Value* Find(const std::string& key) const;
    const Value& operator[](const std::string& key) const;  // Null value if missing
    Value& Set(const std::string& key, Value v);

private:
    Type m_type = Type::Null;
    bool m_bool = false;
    double m_num = 0;
    std::string m_str;
    std::vector<Value> m_arr;
    std::vector<std::pair<std::string, Value>> m_obj;
};

std::string Write(const Value& v, bool pretty = true);
bool Parse(std::string_view text, Value& out, std::string* error = nullptr);

}  // namespace json
