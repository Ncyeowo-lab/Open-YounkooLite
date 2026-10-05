#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <windows.h>

#include <nlohmann/json.hpp>

namespace oy {

struct ProcessInfo {
    DWORD pid = 0;
    std::string name;
    std::string title;
};

struct FieldDesc {
    std::string owner;
    std::string obf;
    std::string mapped;
    std::string type;
    std::string sig;
    std::string desc;
    long long offset = -1;
};

class McRuntime {
public:
    ~McRuntime();

    static std::vector<ProcessInfo> listProcesses();

    bool attach(DWORD pid, std::string& err);
    void detach();
    bool attached() const { return handle_ != nullptr; }

    nlohmann::json status() const;
    nlohmann::json camera() const;
    nlohmann::json entities() const;
    nlohmann::json matrix() const;
    nlohmann::json localPlayer() const;

    bool addMapping(const nlohmann::json& m, std::string& err);
    void clearMappings();
    nlohmann::json mappings() const;

    bool readField(const nlohmann::json& req, nlohmann::json& out, std::string& err);
    bool writeField(const nlohmann::json& req, std::string& err);

    nlohmann::json vmOffsets() const;

private:
    bool readMem(uint64_t addr, void* buf, size_t n) const;
    bool writeMem(uint64_t addr, const void* buf, size_t n) const;
    bool readCString(uint64_t addr, std::string& out) const;
    bool findJvmModule(std::string& err);
    bool parseVmStructs(std::string& err);
    bool resolveStaticOwner(const std::string& owner, uint64_t& addr, std::string& err) const;

    FieldDesc* findDesc(const std::string& owner, const std::string& key);
    const FieldDesc* findDesc(const std::string& owner, const std::string& key) const;

    bool readTyped(uint64_t address, long long offset, const std::string& type,
                   nlohmann::json& out, std::string& err) const;
    bool writeTyped(uint64_t address, long long offset, const std::string& type,
                    const nlohmann::json& value, std::string& err);

    HANDLE handle_ = nullptr;
    DWORD pid_ = 0;
    std::string processName_;
    std::string windowTitle_;

    uint64_t jvmBase_ = 0;
    std::map<std::string, long long> vmOffsets_;
    std::vector<FieldDesc> descs_;
    std::map<std::string, uint64_t> staticOwners_;
};

}
