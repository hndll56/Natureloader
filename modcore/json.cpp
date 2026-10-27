#include "json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace nm {

const Json& Json::path(const std::string& dotted) const {
    const Json* cur = this;
    size_t pos = 0;
    while (pos <= dotted.size()) {
        size_t dot = dotted.find('.', pos);
        std::string part = dotted.substr(pos, dot == std::string::npos ? std::string::npos : dot - pos);
        if (!cur->isObject()) return null();
        cur = cur->find(part);
        if (!cur) return null();
        if (dot == std::string::npos) break;
        pos = dot + 1;
    }
    return *cur;
}

bool Json::operator==(const Json& o) const {
    if (t_ != o.t_) {
        if (isNumber() && o.isNumber()) return asDouble() == o.asDouble();
        return false;
    }
    switch (t_) {
        case Null: return true;
        case Bool: case Int: return i_ == o.i_;
        case Double: return d_ == o.d_;
        case String: return s_ == o.s_;
        case Array: return a_ == o.a_;
        case Object: return o_ == o.o_;
    }
    return false;
}

namespace {
struct Parser {
    const std::string& s;
    size_t i = 0;
    size_t maxDepth;
    Parser(const std::string& text, size_t depth) : s(text), maxDepth(depth) {}

    [[noreturn]] void fail(const std::string& m) const {
        throw JsonError("JSON: " + m + " pada posisi " + std::to_string(i));
    }
    void ws() { while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i; }
    char peek() { return i < s.size() ? s[i] : '\0'; }
    bool eat(char c) { if (peek() == c) { ++i; return true; } return false; }

    Json value(size_t depth) {
        if (depth > maxDepth) fail("terlalu dalam");
        ws();
        char c = peek();
        if (c == '{') return object(depth);
        if (c == '[') return array(depth);
        if (c == '"') return Json(str());
        if (c == 't') return lit("true", Json(true));
        if (c == 'f') return lit("false", Json(false));
        if (c == 'n') return lit("null", Json());
        if (c == '-' || (c >= '0' && c <= '9')) return number();
        fail("token tidak dikenal");
    }
    Json lit(const char* word, Json v) {
        size_t n = std::strlen(word);
        if (s.compare(i, n, word) != 0) fail("literal tidak valid");
        i += n;
        return v;
    }
    Json number() {
        size_t st = i;
        if (peek() == '-') ++i;
        if (peek() == '0') { ++i; }
        else if (peek() >= '1' && peek() <= '9') { while (peek() >= '0' && peek() <= '9') ++i; }
        else fail("angka tidak valid");
        bool isFloat = false;
        if (peek() == '.') {
            isFloat = true; ++i;
            if (!(peek() >= '0' && peek() <= '9')) fail("angka tidak valid");
            while (peek() >= '0' && peek() <= '9') ++i;
        }
        if (peek() == 'e' || peek() == 'E') {
            isFloat = true; ++i;
            if (peek() == '+' || peek() == '-') ++i;
            if (!(peek() >= '0' && peek() <= '9')) fail("angka tidak valid");
            while (peek() >= '0' && peek() <= '9') ++i;
        }
        std::string tok = s.substr(st, i - st);
        if (!isFloat) {
            errno = 0;
            char* end = nullptr;
            long long v = std::strtoll(tok.c_str(), &end, 10);
            if (errno == 0) return Json(static_cast<int64_t>(v));
        }
        double d = std::strtod(tok.c_str(), nullptr);
        if (!std::isfinite(d)) fail("angka di luar jangkauan");
        return Json(d);
    }
    static void utf8(std::string& o, uint32_t cp) {
        if (cp < 0x80) o += char(cp);
        else if (cp < 0x800) { o += char(0xC0 | (cp >> 6)); o += char(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { o += char(0xE0 | (cp >> 12)); o += char(0x80 | ((cp >> 6) & 0x3F)); o += char(0x80 | (cp & 0x3F)); }
        else { o += char(0xF0 | (cp >> 18)); o += char(0x80 | ((cp >> 12) & 0x3F)); o += char(0x80 | ((cp >> 6) & 0x3F)); o += char(0x80 | (cp & 0x3F)); }
    }
    uint32_t hex4() {
        if (i + 4 > s.size()) fail("escape \\u terpotong");
        uint32_t v = 0;
        for (int k = 0; k < 4; ++k) {
            char c = s[i++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= c - '0';
            else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
            else fail("hex tidak valid");
        }
        return v;
    }
    std::string str() {
        if (!eat('"')) fail("string diharapkan");
        std::string out;
        while (true) {
            if (i >= s.size()) fail("string tidak ditutup");
            unsigned char c = static_cast<unsigned char>(s[i++]);
            if (c == '"') break;
            if (c < 0x20) fail("karakter kontrol dalam string");
            if (c != '\\') { out += char(c); continue; }
            if (i >= s.size()) fail("escape terpotong");
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
                    uint32_t cp = hex4();
                    if (cp >= 0xD800 && cp <= 0xDBFF) {
                        if (s.compare(i, 2, "\\u") != 0) fail("surrogate tidak berpasangan");
                        i += 2;
                        uint32_t lo = hex4();
                        if (lo < 0xDC00 || lo > 0xDFFF) fail("surrogate tidak valid");
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    } else if (cp >= 0xDC00 && cp <= 0xDFFF) fail("surrogate tidak berpasangan");
                    utf8(out, cp);
                    break;
                }
                default: fail("escape tidak dikenal");
            }
        }
        return out;
    }
    Json array(size_t depth) {
        ++i;
        Json a = Json::array();
        ws();
        if (eat(']')) return a;
        while (true) {
            a.push(value(depth + 1));
            ws();
            if (eat(',')) continue;
            if (eat(']')) break;
            fail("',' atau ']' diharapkan");
        }
        return a;
    }
    Json object(size_t depth) {
        ++i;
        Json o = Json::object();
        ws();
        if (eat('}')) return o;
        while (true) {
            ws();
            std::string k = str();
            ws();
            if (!eat(':')) fail("':' diharapkan");
            if (o.has(k)) fail("kunci ganda \"" + k + "\"");
            o.members()[k] = value(depth + 1);
            ws();
            if (eat(',')) continue;
            if (eat('}')) break;
            fail("',' atau '}' diharapkan");
        }
        return o;
    }
};

void escape(std::string& out, const std::string& s) {
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); out += b; }
                else out += char(c);
        }
    }
    out += '"';
}
}  // namespace

Json Json::parse(const std::string& text, size_t maxDepth) {
    Parser p(text, maxDepth);
    // UTF-8 BOM
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF && static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF) p.i = 3;
    Json v = p.value(0);
    p.ws();
    if (p.i != text.size()) p.fail("data berlebih setelah nilai");
    return v;
}

void Json::dumpTo(std::string& out, int indent, int level) const {
    auto nl = [&](int lv) { if (indent >= 0) { out += '\n'; out.append(static_cast<size_t>(indent * lv), ' '); } };
    switch (t_) {
        case Null: out += "null"; break;
        case Bool: out += i_ ? "true" : "false"; break;
        case Int: out += std::to_string(i_); break;
        case Double: { char b[40]; std::snprintf(b, sizeof b, "%.17g", d_); out += b; break; }
        case String: escape(out, s_); break;
        case Array:
            out += '[';
            for (size_t k = 0; k < a_.size(); ++k) {
                if (k) out += ',';
                nl(level + 1);
                a_[k].dumpTo(out, indent, level + 1);
            }
            if (!a_.empty()) nl(level);
            out += ']';
            break;
        case Object: {
            out += '{';
            bool first = true;
            for (const auto& kv : o_) {
                if (!first) out += ',';
                first = false;
                nl(level + 1);
                escape(out, kv.first);
                out += indent >= 0 ? ": " : ":";
                kv.second.dumpTo(out, indent, level + 1);
            }
            if (!o_.empty()) nl(level);
            out += '}';
            break;
        }
    }
}

std::string Json::dump(int indent) const { std::string o; dumpTo(o, indent, 0); return o; }

}  // namespace nm
