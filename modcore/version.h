// Semantic-ish versions and range constraints (">=1.2 <2", "^1.2.0", "~1.2", "*", "1.2.3").
#pragma once
#include <string>
#include <utility>
#include <vector>

namespace nm {

struct Version {
    int major = 0, minor = 0, patch = 0;
    static bool parse(const std::string& s, Version& out);
    std::string str() const { return std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch); }
    int cmp(const Version& o) const {
        if (major != o.major) return major < o.major ? -1 : 1;
        if (minor != o.minor) return minor < o.minor ? -1 : 1;
        if (patch != o.patch) return patch < o.patch ? -1 : 1;
        return 0;
    }
    bool operator<(const Version& o) const { return cmp(o) < 0; }
    bool operator==(const Version& o) const { return cmp(o) == 0; }
};

class VersionRange {
public:
    static bool parse(const std::string& s, VersionRange& out);
    static VersionRange any() { VersionRange r; r.text_ = "*"; return r; }
    bool satisfies(const Version& v) const;
    const std::string& str() const { return text_; }
private:
    enum Op { Ge, Gt, Le, Lt, Eq };
    std::vector<std::pair<Op, Version>> terms_;
    std::string text_ = "*";
};

}  // namespace nm
