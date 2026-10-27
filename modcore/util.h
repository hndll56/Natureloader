// Small shared helpers: SHA-256, safe pack paths, id validation, pack sources.
#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace nm {

std::string sha256Hex(const std::string& data);

// Identifier rules shared by mod ids and local record ids: [a-z][a-z0-9_-]{0,63}.
bool validLocalId(const std::string& s);
// Full id "<mod>:<local>".
bool splitId(const std::string& full, std::string& mod, std::string& local);
// Pack-relative path: no "..", no absolute, no backslash/colon, no control chars.
bool safePackPath(const std::string& p);
bool reservedWindowsName(const std::string& id);

// Read-only view of one mod package (directory, zip, or memory for tests).
class PackSource {
public:
    virtual ~PackSource() = default;
    virtual bool exists(const std::string& path) const = 0;
    virtual bool read(const std::string& path, std::string& out) const = 0;
    virtual std::vector<std::string> list(const std::string& prefix) const = 0;
    virtual std::string label() const = 0;
};

class MemPackSource : public PackSource {
public:
    explicit MemPackSource(std::string label = "memory") : label_(std::move(label)) {}
    void put(const std::string& path, std::string data) { files_[path] = std::move(data); }
    bool exists(const std::string& p) const override { return files_.count(p) != 0; }
    bool read(const std::string& p, std::string& out) const override {
        auto it = files_.find(p);
        if (it == files_.end()) return false;
        out = it->second;
        return true;
    }
    std::vector<std::string> list(const std::string& prefix) const override {
        std::vector<std::string> r;
        for (const auto& kv : files_) if (kv.first.compare(0, prefix.size(), prefix) == 0) r.push_back(kv.first);
        return r;
    }
    std::string label() const override { return label_; }
private:
    std::string label_;
    std::map<std::string, std::string> files_;
};

class DirPackSource : public PackSource {
public:
    explicit DirPackSource(std::string root) : root_(std::move(root)) {}
    bool exists(const std::string& p) const override;
    bool read(const std::string& p, std::string& out) const override;
    std::vector<std::string> list(const std::string& prefix) const override;
    std::string label() const override { return root_; }
private:
    std::string root_;
};

bool readWholeFile(const std::string& path, std::string& out, size_t limit = 256u * 1024 * 1024);
bool writeWholeFile(const std::string& path, const std::string& data);

}  // namespace nm
