// Minimal strict JSON value/parser/serializer. No dependencies (usable without Qt).
#pragma once
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace nm {

struct JsonError : std::runtime_error { using std::runtime_error::runtime_error; };

class Json {
public:
    enum Type { Null, Bool, Int, Double, String, Array, Object };
    using Arr = std::vector<Json>;
    using Obj = std::map<std::string, Json>;

    Json() = default;
    Json(std::nullptr_t) {}
    Json(bool b) : t_(Bool), i_(b) {}
    Json(int v) : t_(Int), i_(v) {}
    Json(int64_t v) : t_(Int), i_(v) {}
    Json(uint32_t v) : t_(Int), i_(v) {}
    Json(double v) : t_(Double), d_(v) {}
    Json(const char* s) : t_(String), s_(s) {}
    Json(std::string s) : t_(String), s_(std::move(s)) {}
    static Json array() { Json j; j.t_ = Array; return j; }
    static Json object() { Json j; j.t_ = Object; return j; }

    Type type() const { return t_; }
    bool isNull() const { return t_ == Null; }
    bool isBool() const { return t_ == Bool; }
    bool isInt() const { return t_ == Int; }
    bool isNumber() const { return t_ == Int || t_ == Double; }
    bool isString() const { return t_ == String; }
    bool isArray() const { return t_ == Array; }
    bool isObject() const { return t_ == Object; }

    bool asBool(bool def = false) const { return t_ == Bool ? i_ != 0 : def; }
    int64_t asInt(int64_t def = 0) const {
        if (t_ == Int) return i_;
        if (t_ != Double) return def;
        // INT64_MAX rounds up to 2^63 as a double; use an exclusive upper bound.
        const double lower = static_cast<double>(std::numeric_limits<int64_t>::min());
        if (!std::isfinite(d_) || d_ < lower || d_ >= -lower)
            throw JsonError("angka di luar rentang int64");
        return static_cast<int64_t>(d_);
    }
    double asDouble(double def = 0) const {
        return t_ == Double ? d_ : (t_ == Int ? static_cast<double>(i_) : def);
    }
    const std::string& asString() const { return t_ == String ? s_ : empty(); }

    const Arr& items() const { return t_ == Array ? a_ : emptyArr(); }
    Arr& items() { if (t_ != Array) { *this = array(); } return a_; }
    const Obj& members() const { return t_ == Object ? o_ : emptyObj(); }
    Obj& members() { if (t_ != Object) { *this = object(); } return o_; }
    size_t size() const { return t_ == Array ? a_.size() : (t_ == Object ? o_.size() : 0); }

    const Json* find(const std::string& k) const {
        if (t_ != Object) return nullptr;
        auto it = o_.find(k);
        return it == o_.end() ? nullptr : &it->second;
    }
    const Json& operator[](const std::string& k) const {
        const Json* p = find(k);
        return p ? *p : null();
    }
    Json& operator[](const std::string& k) { return members()[k]; }
    const Json& operator[](size_t i) const { return i < items().size() ? items()[i] : null(); }
    void push(Json v) { items().push_back(std::move(v)); }
    bool has(const std::string& k) const { return find(k) != nullptr; }

    // Follow a dotted path ("a.b.c") through objects. Returns null Json when absent.
    const Json& path(const std::string& dotted) const;

    std::string dump(int indent = -1) const;
    static Json parse(const std::string& text, size_t maxDepth = 64);
    bool operator==(const Json& o) const;
    bool operator!=(const Json& o) const { return !(*this == o); }

    static const Json& null() { static const Json n; return n; }

private:
    static const std::string& empty() { static const std::string e; return e; }
    static const Arr& emptyArr() { static const Arr e; return e; }
    static const Obj& emptyObj() { static const Obj e; return e; }
    void dumpTo(std::string& out, int indent, int level) const;

    Type t_ = Null;
    int64_t i_ = 0;
    double d_ = 0;
    std::string s_;
    Arr a_;
    Obj o_;
};

}  // namespace nm
