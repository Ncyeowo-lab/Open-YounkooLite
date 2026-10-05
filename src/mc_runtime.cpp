#include "mc_runtime.h"

#include <psapi.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "log.h"

namespace oy {

using json = nlohmann::json;

namespace {
struct EnumCtx {
    std::vector<ProcessInfo> out;
};

BOOL CALLBACK enumWindowCb(HWND hwnd, LPARAM lparam) {
    auto* ctx = reinterpret_cast<EnumCtx*>(lparam);
    if (!IsWindowVisible(hwnd)) return TRUE;

    char title[512]{};
    if (!GetWindowTextA(hwnd, title, sizeof(title)) || title[0] == '\0') return TRUE;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0) return TRUE;

    ProcessInfo pi;
    pi.pid = pid;
    pi.title = title;

    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (h) {
        char name[MAX_PATH]{};
        if (GetModuleBaseNameA(h, nullptr, name, sizeof(name))) pi.name = name;
        CloseHandle(h);
    }
    if (pi.name.empty()) pi.name = "?";

    std::string t = pi.title;
    std::string n = pi.name;
    std::transform(n.begin(), n.end(), n.begin(), ::tolower);
    bool isJava = n == "javaw.exe" || n == "java.exe";
    bool looksMc = t.find("Minecraft") != std::string::npos;
    if (isJava || looksMc) ctx->out.push_back(pi);

    return TRUE;
}
}

std::vector<ProcessInfo> McRuntime::listProcesses() {
    EnumCtx ctx;
    EnumWindows(enumWindowCb, reinterpret_cast<LPARAM>(&ctx));
    return ctx.out;
}

McRuntime::~McRuntime() { detach(); }

void McRuntime::detach() {
    if (handle_) {
        CloseHandle(handle_);
        handle_ = nullptr;
    }
    pid_ = 0;
    jvmBase_ = 0;
    vmOffsets_.clear();
    processName_.clear();
    windowTitle_.clear();
}

bool McRuntime::readMem(uint64_t addr, void* buf, size_t n) const {
    if (!handle_) return false;
    SIZE_T got = 0;
    return ReadProcessMemory(handle_, reinterpret_cast<LPCVOID>(addr), buf, n, &got) && got == n;
}

bool McRuntime::writeMem(uint64_t addr, const void* buf, size_t n) const {
    if (!handle_) return false;
    SIZE_T got = 0;
    return WriteProcessMemory(handle_, reinterpret_cast<LPVOID>(addr), buf, n, &got) && got == n;
}

bool McRuntime::readCString(uint64_t addr, std::string& out) const {
    if (!addr) return false;
    out.clear();
    char chunk[256];
    for (int round = 0; round < 8; ++round) {
        std::memset(chunk, 0, sizeof(chunk));
        if (!readMem(addr + round * sizeof(chunk), chunk, sizeof(chunk))) return false;
        for (int i = 0; i < static_cast<int>(sizeof(chunk)); ++i) {
            if (chunk[i] == '\0') { out.append(chunk, i); return true; }
        }
        out.append(chunk, sizeof(chunk));
    }
    return false;
}

namespace {
bool exportRva(HANDLE h, uint64_t modBase, const std::string& name, uint64_t& outRva) {
    auto rd = [&](uint64_t a, void* b, size_t n) {
        SIZE_T got = 0;
        return ReadProcessMemory(h, reinterpret_cast<LPCVOID>(a), b, n, &got) && got == n;
    };

    unsigned char dos[0x400]{};
    if (!rd(modBase, dos, sizeof(dos))) return false;
    uint32_t e_lfanew = 0;
    std::memcpy(&e_lfanew, dos + 0x3C, 4);
    if (e_lfanew == 0 || e_lfanew > 0x10000) return false;

    unsigned char nt[0x400]{};
    if (!rd(modBase + e_lfanew, nt, sizeof(nt))) return false;
    if (std::memcmp(nt, "PE\0\0", 4) != 0) return false;

    uint16_t machine = 0, optsz = 0;
    std::memcpy(&machine, nt + 4, 2);
    std::memcpy(&optsz, nt + 20, 2);
    bool pe64 = false;
    uint16_t magic = 0;
    std::memcpy(&magic, nt + 24, 2);
    pe64 = (magic == 0x20b);
    (void)machine;

    uint64_t ddOff = e_lfanew + 24 + (pe64 ? 112 : 96);
    uint32_t expRva = 0, expSize = 0;
    if (!rd(modBase + ddOff, &expRva, 4)) return false;
    if (!rd(modBase + ddOff + 4, &expSize, 4)) return false;
    if (!expRva) return false;

    struct ExpDir {
        uint32_t characteristics, timeDateStamp, majorVersion, minorVersion;
        uint32_t name, base, numberOfFunctions, numberOfNames;
        uint32_t addressOfFunctions, addressOfNames, addressOfNameOrdinals;
    } ed{};
    if (!rd(modBase + expRva, &ed, sizeof(ed))) return false;

    std::vector<uint32_t> names(ed.numberOfNames);
    if (ed.numberOfNames &&
        !rd(modBase + ed.addressOfNames, names.data(), names.size() * 4)) return false;

    for (uint32_t i = 0; i < ed.numberOfNames; ++i) {
        char buf[256]{};
        if (!rd(modBase + names[i], buf, sizeof(buf) - 1)) continue;
        if (name == buf) {
            uint16_t ord = 0;
            if (!rd(modBase + ed.addressOfNameOrdinals + i * 2, &ord, 2)) return false;
            uint32_t funcRva = 0;
            if (!rd(modBase + ed.addressOfFunctions + ord * 4, &funcRva, 4)) return false;
            outRva = funcRva;
            return true;
        }
    }
    return false;
}
}

bool McRuntime::findJvmModule(std::string& err) {
    HMODULE mods[1024];
    DWORD needed = 0;
    if (!EnumProcessModules(handle_, mods, sizeof(mods), &needed)) {
        err = "EnumProcessModules failed";
        return false;
    }
    size_t count = needed / sizeof(HMODULE);
    for (size_t i = 0; i < count && i < 1024; ++i) {
        char path[MAX_PATH]{};
        if (!GetModuleFileNameExA(handle_, mods[i], path, sizeof(path))) continue;
        std::string p(path);
        std::string lower = p;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        size_t slash = lower.find_last_of("\\/");
        std::string base = (slash == std::string::npos) ? lower : lower.substr(slash + 1);
        if (base == "jvm.dll") {
            jvmBase_ = reinterpret_cast<uint64_t>(mods[i]);
            return true;
        }
    }
    err = "Failed to find jvm.dll";
    return false;
}

bool McRuntime::parseVmStructs(std::string& err) {
    if (!jvmBase_) return false;

    uint64_t rva = 0;
    if (!exportRva(handle_, jvmBase_, "gHotSpotVMStructs", rva)) {
        err = "Failed to find gHotSpotVMStructs";
        return false;
    }
    uint64_t ptr = 0;
    if (!readMem(jvmBase_ + rva, &ptr, sizeof(ptr)) || !ptr) {
        err = "Failed to find VMStructs";
        return false;
    }

    struct Entry {
        uint64_t typeName;
        uint64_t fieldName;
        uint64_t typeString;
        int32_t isStatic;
        int32_t pad;
        uint64_t offset;
        uint64_t address;
    };

    for (int i = 0; i < 4096; ++i) {
        Entry e{};
        if (!readMem(ptr + i * sizeof(Entry), &e, sizeof(Entry))) break;
        if (!e.typeName) break;

        std::string typeName, fieldName;
        readCString(e.typeName, typeName);
        readCString(e.fieldName, fieldName);
        if (typeName.empty()) break;

        vmOffsets_[typeName + "::" + fieldName] = static_cast<long long>(e.offset);
        if (e.isStatic && e.address) {
            uint64_t holder = 0;
            if (readMem(e.address, &holder, sizeof(holder)) && holder) {
                staticOwners_[typeName + "::" + fieldName] = holder;
            }
        }
    }
    return !vmOffsets_.empty();
}

bool McRuntime::attach(DWORD pid, std::string& err) {
    detach();

    if (pid == 0) { err = "pid is required"; return false; }

    HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | PROCESS_VM_WRITE,
                           FALSE, pid);
    if (!h) { err = "OpenProcess failed"; return false; }

    handle_ = h;
    pid_ = pid;

    char name[MAX_PATH]{};
    if (GetModuleBaseNameA(h, nullptr, name, sizeof(name))) processName_ = name;

    for (const auto& p : listProcesses()) {
        if (p.pid == pid) { windowTitle_ = p.title; break; }
    }

    std::string e2;
    if (!findJvmModule(e2)) {
        Log::warn("attach: " + e2);
    } else {
        std::string e3;
        if (parseVmStructs(e3)) {
            Log::info("attach: parsed " + std::to_string(vmOffsets_.size()) + " VM struct entries");
        } else {
            Log::warn("attach: " + e3);
        }
    }

    Log::info("attached to pid " + std::to_string(pid_) + " (" + processName_ + ")");
    return true;
}

FieldDesc* McRuntime::findDesc(const std::string& owner, const std::string& key) {
    for (auto& d : descs_) {
        if (d.owner != owner) continue;
        if (d.mapped == key || d.obf == key) return &d;
    }
    return nullptr;
}

const FieldDesc* McRuntime::findDesc(const std::string& owner, const std::string& key) const {
    return const_cast<McRuntime*>(this)->findDesc(owner, key);
}

bool McRuntime::addMapping(const json& m, std::string& err) {
    if (!m.is_object()) { err = "mapping table is required"; return false; }

    FieldDesc d;
    d.owner = m.value("owner", "");
    d.obf = m.value("obf", "");
    d.mapped = m.value("mapped", d.obf);
    d.type = m.value("type", "address");
    d.sig = m.value("sig", "");
    d.desc = m.value("descriptor", m.value("desc", ""));
    if (m.contains("offset") && m["offset"].is_number()) {
        d.offset = m["offset"].get<long long>();
    } else {
        auto it = vmOffsets_.find(d.owner + "::" + d.mapped);
        if (it == vmOffsets_.end()) it = vmOffsets_.find(d.owner + "::" + d.obf);
        if (it != vmOffsets_.end()) d.offset = it->second;
    }

    if (d.obf.empty() && d.mapped.empty()) { err = "invalid mapping"; return false; }

    if (FieldDesc* old = findDesc(d.owner, d.mapped)) {
        *old = d;
    } else {
        descs_.push_back(d);
    }
    return true;
}

void McRuntime::clearMappings() { descs_.clear(); }

json McRuntime::mappings() const {
    json arr = json::array();
    for (const auto& d : descs_) {
        arr.push_back(json{{"owner", d.owner}, {"obf", d.obf}, {"mapped", d.mapped},
                           {"type", d.type}, {"sig", d.sig}, {"offset", d.offset}});
    }
    return arr;
}

json McRuntime::vmOffsets() const {
    json o = json::object();
    for (const auto& kv : vmOffsets_) o[kv.first] = kv.second;
    return o;
}

bool McRuntime::resolveStaticOwner(const std::string& owner, uint64_t& addr, std::string& err) const {
    auto it = staticOwners_.find(owner);
    if (it != staticOwners_.end() && it->second) { addr = it->second; return true; }
    err = "static mirror not found";
    return false;
}

bool McRuntime::readTyped(uint64_t address, long long offset, const std::string& type,
                          json& out, std::string& err) const {
    if (!handle_) { err = "not attached"; return false; }
    uint64_t at = address + static_cast<uint64_t>(offset);

    auto num = [&](auto* v) {
        if (!readMem(at, v, sizeof(*v))) { err = "read failed"; return false; }
        return true;
    };

    if (type == "double" || type == "float") {
        double v = 0; if (!num(&v)) return false; out = v; return true;
    }
    if (type == "float") {
        float v = 0; if (!num(&v)) return false; out = v; return true;
    }
    if (type == "bool") {
        uint8_t v = 0; if (!num(&v)) return false; out = v != 0; return true;
    }
    if (type == "short" || type == "int") {
        int32_t v = 0; if (!num(&v)) return false; out = v; return true;
    }
    if (type == "objectHex") {
        uint64_t v = 0; if (!num(&v)) return false;
        char buf[32]; snprintf(buf, sizeof(buf), "0x%llx", static_cast<unsigned long long>(v));
        out = std::string(buf);
        return true;
    }
    uint64_t v = 0; if (!num(&v)) return false; out = v; return true;
}

bool McRuntime::writeTyped(uint64_t address, long long offset, const std::string& type,
                           const json& value, std::string& err) {
    if (!handle_) { err = "not attached"; return false; }
    uint64_t at = address + static_cast<uint64_t>(offset);

    if (type == "double" || type == "float") {
        double v = value.is_number() ? value.get<double>() : 0.0;
        float f = static_cast<float>(v);
        if (type == "double") { return writeMem(at, &v, sizeof(v)); }
        return writeMem(at, &f, sizeof(f));
    }
    if (type == "bool") {
        uint8_t v = value.is_boolean() ? (value.get<bool>() ? 1 : 0)
                                       : (value.is_number() ? (value.get<int>() != 0 ? 1 : 0) : 0);
        return writeMem(at, &v, sizeof(v));
    }
    if (type == "short" || type == "int") {
        int32_t v = value.is_number() ? value.get<int32_t>() : 0;
        if (type == "short") { int16_t s = static_cast<int16_t>(v); return writeMem(at, &s, sizeof(s)); }
        return writeMem(at, &v, sizeof(v));
    }
    uint64_t v = value.is_number() ? value.get<uint64_t>() : 0;
    if (value.is_string()) {
        v = strtoull(value.get<std::string>().c_str(), nullptr, 0);
    }
    return writeMem(at, &v, sizeof(v));
}

bool McRuntime::readField(const json& req, json& out, std::string& err) {
    if (!handle_) { err = "not attached"; return false; }

    std::string owner = req.value("owner", req.value("ownerName", ""));
    std::string key = req.value("field", req.value("fieldKey", req.value("key", "")));
    if (key.empty()) { err = "field key is empty"; return false; }

    long long offset = -1;
    std::string type = req.value("type", "address");

    if (const FieldDesc* d = findDesc(owner, key)) {
        offset = d->offset;
        if (!req.contains("type")) type = d->type;
    }
    if (req.contains("offset") && req["offset"].is_number()) offset = req["offset"].get<long long>();
    if (offset < 0) { err = "invalid mapping"; return false; }

    uint64_t address = 0;
    if (req.contains("address")) {
        address = req["address"].is_number() ? req["address"].get<uint64_t>()
                                             : strtoull(req["address"].get<std::string>().c_str(), nullptr, 0);
    } else if (!resolveStaticOwner(owner.empty() ? key : owner, address, err)) {
        if (err.empty()) err = "object address is required";
        return false;
    }
    if (!address) { err = "object address is required"; return false; }

    return readTyped(address, offset, type, out, err);
}

bool McRuntime::writeField(const json& req, std::string& err) {
    if (!handle_) { err = "not attached"; return false; }

    std::string owner = req.value("owner", req.value("ownerName", ""));
    std::string key = req.value("field", req.value("fieldKey", req.value("key", "")));
    if (key.empty()) { err = "field key is empty"; return false; }
    if (!req.contains("value")) { err = "value is required"; return false; }

    long long offset = -1;
    std::string type = req.value("type", "address");
    if (const FieldDesc* d = findDesc(owner, key)) {
        offset = d->offset;
        if (!req.contains("type")) type = d->type;
    }
    if (req.contains("offset") && req["offset"].is_number()) offset = req["offset"].get<long long>();
    if (offset < 0) { err = "invalid mapping"; return false; }

    uint64_t address = 0;
    if (req.contains("address")) {
        address = req["address"].is_number() ? req["address"].get<uint64_t>()
                                             : strtoull(req["address"].get<std::string>().c_str(), nullptr, 0);
    } else if (!resolveStaticOwner(owner.empty() ? key : owner, address, err)) {
        if (err.empty()) err = "object address is required";
        return false;
    }
    if (!address) { err = "object address is required"; return false; }

    return writeTyped(address, offset, type, req["value"], err);
}

namespace {
json zeroVec3() { return json{{"x", 0.0}, {"y", 0.0}, {"z", 0.0}}; }
}

json McRuntime::status() const {
    if (!handle_) {
        return json{{"attached", false}, {"pid", 0}, {"processName", ""},
                    {"windowTitle", ""}, {"state", "detached"},
                    {"message", "Waiting for Minecraft"}};
    }
    return json{{"attached", true}, {"pid", pid_}, {"processName", processName_},
                {"windowTitle", windowTitle_}, {"state", "attached"},
                {"jvmBase", jvmBase_}, {"vmOffsets", vmOffsets_.size()},
                {"message", "Attached"}};
}

json McRuntime::camera() const {
    json c = json{{"valid", handle_ != nullptr}, {"x", 0.0}, {"y", 0.0}, {"z", 0.0},
                  {"yaw", 0.0}, {"pitch", 0.0}, {"fov", 70.0},
                  {"nearPlane", 0.05}, {"farPlane", 1024.0}, {"aspect", 1.7777778},
                  {"partialTick", 1.0}};
    if (!handle_) return c;

    for (const char* k : {"x", "y", "z", "yaw", "pitch", "fov"}) {
        if (const FieldDesc* d = findDesc("camera", k)) {
            json v; std::string err;
            uint64_t addr = 0;
            if (resolveStaticOwner("camera", addr, err) && d->offset >= 0) {
                if (readTyped(addr, d->offset, d->type, v, err)) c[k] = v;
            }
        }
    }
    return c;
}

json McRuntime::localPlayer() const {
    json p = json{{"valid", handle_ != nullptr}, {"x", 0.0}, {"y", 0.0}, {"z", 0.0},
                  {"yaw", 0.0}, {"pitch", 0.0}, {"health", 0.0},
                  {"attackStrengthTicker", 0}, {"name", ""}};
    return p;
}

json McRuntime::matrix() const {
    json m = json::array();
    for (int i = 0; i < 16; ++i) m.push_back(0.0);
    return json{{"valid", handle_ != nullptr}, {"elements", m}};
}

json McRuntime::entities() const {
    return json{{"valid", handle_ != nullptr}, {"players", json::array()},
                {"count", 0}, {"caves", json::array()}};
}

}
