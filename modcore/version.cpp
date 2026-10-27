#include "version.h"

#include <sstream>

namespace nm {

bool Version::parse(const std::string& s, Version& out) {
    int parts[3] = {0, 0, 0};
    int n = 0;
    size_t i = 0;
    if (s.empty()) return false;
    while (i < s.size()) {
        if (n >= 3) return false;
        size_t st = i;
        long v = 0;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
            v = v * 10 + (s[i] - '0');
            if (v > 999999) return false;
            ++i;
        }
        if (i == st) return false;
        parts[n++] = static_cast<int>(v);
        if (i < s.size()) {
            if (s[i] != '.') return false;
            ++i;
            if (i == s.size()) return false;
        }
    }
    out.major = parts[0]; out.minor = parts[1]; out.patch = parts[2];
    return true;
}

bool VersionRange::parse(const std::string& s, VersionRange& out) {
    VersionRange r;
    r.text_ = s;
    std::string norm;
    for (char c : s) norm += (c == ',') ? ' ' : c;
    std::istringstream is(norm);
    std::string tok;
    bool any = false;
    while (is >> tok) {
        any = true;
        if (tok == "*") continue;
        Version v;
        if (tok[0] == '^' || tok[0] == '~') {
            if (!Version::parse(tok.substr(1), v)) return false;
            Version hi = v;
            if (tok[0] == '^') {
                if (v.major > 0) { hi.major += 1; hi.minor = 0; hi.patch = 0; }
                else { hi.minor += 1; hi.patch = 0; }
            } else { hi.minor += 1; hi.patch = 0; }
            r.terms_.push_back({Ge, v});
            r.terms_.push_back({Lt, hi});
            continue;
        }
        Op op = Eq;
        size_t skip = 0;
        if (tok.compare(0, 2, ">=") == 0) { op = Ge; skip = 2; }
        else if (tok.compare(0, 2, "<=") == 0) { op = Le; skip = 2; }
        else if (tok[0] == '>') { op = Gt; skip = 1; }
        else if (tok[0] == '<') { op = Lt; skip = 1; }
        else if (tok[0] == '=') { op = Eq; skip = 1; }
        if (!Version::parse(tok.substr(skip), v)) return false;
        r.terms_.push_back({op, v});
    }
    if (!any) return false;
    out = r;
    return true;
}

bool VersionRange::satisfies(const Version& v) const {
    for (const auto& t : terms_) {
        int c = v.cmp(t.second);
        switch (t.first) {
            case Ge: if (c < 0) return false; break;
            case Gt: if (c <= 0) return false; break;
            case Le: if (c > 0) return false; break;
            case Lt: if (c >= 0) return false; break;
            case Eq: if (c != 0) return false; break;
        }
    }
    return true;
}

}  // namespace nm
