// palsave.cpp -- implementation.
#include "palsave.hpp"

#include <windows.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <system_error>

#include "miniz.h"

// From the vendored ooz decoder (kraken.cpp). C++ linkage.
int Kraken_Decompress(const unsigned char* src, size_t src_len,
                      unsigned char* dst, size_t dst_len);

namespace pal {
namespace {

constexpr int SAFE_SPACE = 64;   // ooz may write a few bytes past the buffer
constexpr uint8_t SAVE_TYPE_PLZ = 0x31;

// ---- little-endian helpers ------------------------------------------------
uint32_t le32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
void put32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back((uint8_t)(x)); v.push_back((uint8_t)(x >> 8));
    v.push_back((uint8_t)(x >> 16)); v.push_back((uint8_t)(x >> 24));
}

// ---- file IO --------------------------------------------------------------
std::vector<uint8_t> readFile(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open " + p.string());
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)),
                                 std::istreambuf_iterator<char>());
}
void writeFile(const fs::path& p, const std::vector<uint8_t>& d) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) throw std::runtime_error("cannot write " + p.string());
    f.write(reinterpret_cast<const char*>(d.data()), (std::streamsize)d.size());
}

// ---- container decode/encode ---------------------------------------------
// Decompress a Palworld .sav (PlM/Oodle or PlZ/zlib) to raw GVAS bytes.
std::vector<uint8_t> decodeSav(const std::vector<uint8_t>& data) {
    if (data.size() < 12) throw std::runtime_error("file too small to be a save");
    uint32_t uncompressed = le32(&data[0]);
    uint32_t compressed = le32(&data[4]);
    const uint8_t* magic = &data[8];
    uint8_t type = data[11];

    if (magic[0] == 'P' && magic[1] == 'l' && magic[2] == 'M') {
        std::vector<uint8_t> out(uncompressed + SAFE_SPACE);
        int n = Kraken_Decompress(&data[12], data.size() - 12, out.data(), uncompressed);
        if (n != (int)uncompressed)
            throw std::runtime_error("Oodle decode failed (not a supported PlM save?)");
        out.resize(uncompressed);
        return out;
    }
    if (magic[0] == 'P' && magic[1] == 'l' && magic[2] == 'Z') {
        std::vector<uint8_t> out(uncompressed);
        mz_ulong outLen = uncompressed;
        if (type == 0x32) {  // double zlib
            std::vector<uint8_t> mid(compressed);
            mz_ulong midLen = compressed;
            if (mz_uncompress(mid.data(), &midLen, &data[12], data.size() - 12) != MZ_OK)
                throw std::runtime_error("zlib decode failed (outer)");
            if (mz_uncompress(out.data(), &outLen, mid.data(), midLen) != MZ_OK)
                throw std::runtime_error("zlib decode failed (inner)");
        } else {  // 0x31 single zlib
            if (mz_uncompress(out.data(), &outLen, &data[12], data.size() - 12) != MZ_OK)
                throw std::runtime_error("zlib decode failed");
        }
        out.resize(outLen);
        return out;
    }
    throw std::runtime_error("unrecognized save format (Xbox CNK saves not supported)");
}

// Compress raw GVAS to a PlZ (single-zlib) .sav -- read by every Palworld version.
std::vector<uint8_t> encodePlz(const std::vector<uint8_t>& gvas) {
    mz_ulong bound = mz_compressBound((mz_ulong)gvas.size());
    std::vector<uint8_t> comp(bound);
    mz_ulong compLen = bound;
    if (mz_compress2(comp.data(), &compLen, gvas.data(), (mz_ulong)gvas.size(),
                     MZ_DEFAULT_COMPRESSION) != MZ_OK)
        throw std::runtime_error("zlib compress failed");
    comp.resize(compLen);

    std::vector<uint8_t> out;
    out.reserve(12 + comp.size());
    put32(out, (uint32_t)gvas.size());   // uncompressed length
    put32(out, (uint32_t)comp.size());   // compressed length
    out.push_back('P'); out.push_back('l'); out.push_back('Z');
    out.push_back(SAVE_TYPE_PLZ);
    out.insert(out.end(), comp.begin(), comp.end());
    return out;
}

// ---- minimal GVAS property walking ---------------------------------------
// Build the on-disk name field: int32(len incl null) + name + '\0'.
std::vector<uint8_t> nameField(const char* name) {
    uint32_t len = (uint32_t)std::strlen(name) + 1;
    std::vector<uint8_t> f;
    put32(f, len);
    f.insert(f.end(), name, name + std::strlen(name));
    f.push_back(0);
    return f;
}

// Find where a named property's *record* starts (index of its int32 length prefix).
size_t findProp(const std::vector<uint8_t>& b, const char* name) {
    std::vector<uint8_t> needle = nameField(name);
    auto it = std::search(b.begin(), b.end(), needle.begin(), needle.end());
    return (it == b.end()) ? SIZE_MAX : (size_t)(it - b.begin());
}

// Read an FString at pos (advances pos). ASCII only (sufficient for our fields).
std::string readFString(const std::vector<uint8_t>& b, size_t& pos) {
    if (pos + 4 > b.size()) return "";
    int32_t len = (int32_t)le32(&b[pos]); pos += 4;
    if (len <= 0 || pos + (size_t)len > b.size()) return "";
    std::string s(reinterpret_cast<const char*>(&b[pos]), (size_t)len);
    pos += (size_t)len;
    if (!s.empty() && s.back() == '\0') s.pop_back();
    return s;
}

// Offset of a BoolProperty's value byte, or SIZE_MAX if not found/mismatch.
size_t boolValueOffset(const std::vector<uint8_t>& b, const char* name) {
    size_t p = findProp(b, name);
    if (p == SIZE_MAX) return SIZE_MAX;
    p += nameField(name).size();          // skip name field
    std::string type = readFString(b, p); // "BoolProperty"
    if (type != "BoolProperty") return SIZE_MAX;
    p += 8;                                // int64 size (== 0 for bool)
    return (p < b.size()) ? p : SIZE_MAX;  // value byte
}

// Read an EnumProperty's value tail (after "::"); "?" if not found.
std::string readEnum(const std::vector<uint8_t>& b, const char* name) {
    size_t p = findProp(b, name);
    if (p == SIZE_MAX) return "?";
    p += nameField(name).size();
    std::string type = readFString(b, p);
    if (type != "EnumProperty") return "?";
    p += 8;                          // int64 size
    readFString(b, p);               // enum type name
    p += 1;                          // has-guid byte
    std::string v = readFString(b, p);
    auto c = v.rfind("::");
    return (c == std::string::npos) ? v : v.substr(c + 2);
}

// ---- backups --------------------------------------------------------------
fs::path backupsDir() { return exeDir() / "backups"; }

std::string timestamp() {
    SYSTEMTIME t; GetLocalTime(&t);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d%02d%02d_%02d%02d%02d_%03d",
                  t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    return buf;
}

fs::path makeBackup(const fs::path& worldFolder) {
    std::error_code ec;
    fs::create_directories(backupsDir(), ec);
    std::string wid = worldFolder.filename().string();
    fs::path dst;
    int n = 0;
    do {
        std::string suffix = (n == 0) ? "" : ("_" + std::to_string(n));
        dst = backupsDir() / (wid + "_WorldOption_" + timestamp() + suffix + ".sav");
        ++n;
    } while (fs::exists(dst));
    fs::copy_file(worldFolder / "WorldOption.sav", dst, ec);
    if (ec) throw std::runtime_error("backup failed: " + ec.message());
    return dst;
}

}  // namespace

// ---- public API -----------------------------------------------------------
fs::path defaultSaveRoot() {
    wchar_t* v = _wgetenv(L"LOCALAPPDATA");
    fs::path base = v ? fs::path(v) : fs::path();
    return base / L"Pal" / L"Saved" / L"SaveGames";
}

fs::path exeDir() {
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return fs::path(std::wstring(buf, n)).parent_path();
}

std::vector<fs::path> findWorlds(const fs::path& root) {
    std::vector<fs::path> out;
    std::error_code ec;
    if (!fs::exists(root, ec)) return out;
    // Allow pointing directly at a single world folder.
    if (fs::exists(root / "WorldOption.sav", ec)) { out.push_back(root); return out; }
    for (auto it = fs::recursive_directory_iterator(
                       root, fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) { ec.clear(); continue; }
        if (it->is_regular_file(ec) && it->path().filename() == L"WorldOption.sav")
            out.push_back(it->path().parent_path());
    }
    std::sort(out.begin(), out.end());
    return out;
}

Status readStatus(const fs::path& worldFolder) {
    Status s;
    try {
        std::vector<uint8_t> gvas = decodeSav(readFile(worldFolder / "WorldOption.sav"));
        size_t hc = boolValueOffset(gvas, "bHardcore");
        if (hc == SIZE_MAX) { s.error = "bHardcore not found in save"; return s; }
        s.hardcore = gvas[hc] != 0;
        s.difficulty = readEnum(gvas, "Difficulty");
        s.deathPenalty = readEnum(gvas, "DeathPenalty");
        s.ok = true;
    } catch (const std::exception& e) {
        s.error = e.what();
    }
    return s;
}

bool setHardcore(const fs::path& worldFolder, bool enabled,
                 std::string& changeSummary, fs::path& backupPath, std::string& error) {
    try {
        fs::path sav = worldFolder / "WorldOption.sav";
        std::vector<uint8_t> gvas = decodeSav(readFile(sav));
        size_t off = boolValueOffset(gvas, "bHardcore");
        if (off == SIZE_MAX) { error = "bHardcore not found in save"; return false; }
        bool cur = gvas[off] != 0;
        if (cur == enabled) { changeSummary.clear(); return true; }  // no-op
        backupPath = makeBackup(worldFolder);
        gvas[off] = enabled ? 1 : 0;
        writeFile(sav, encodePlz(gvas));
        changeSummary = std::string("bHardcore -> ") + (enabled ? "True" : "False");
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

bool hasBackup(const fs::path& worldFolder) {
    std::error_code ec;
    if (!fs::exists(backupsDir(), ec)) return false;
    std::string prefix = worldFolder.filename().string() + "_WorldOption_";
    for (auto& e : fs::directory_iterator(backupsDir(), ec))
        if (e.path().filename().string().rfind(prefix, 0) == 0) return true;
    return false;
}

bool restoreLatest(const fs::path& worldFolder, fs::path& restoredFrom, std::string& error) {
    try {
        std::error_code ec;
        std::string prefix = worldFolder.filename().string() + "_WorldOption_";
        std::vector<fs::path> baks;
        if (fs::exists(backupsDir(), ec))
            for (auto& e : fs::directory_iterator(backupsDir(), ec))
                if (e.path().filename().string().rfind(prefix, 0) == 0)
                    baks.push_back(e.path());
        if (baks.empty()) { error = "No backups found for this world."; return false; }
        std::sort(baks.begin(), baks.end());
        restoredFrom = baks.back();
        fs::copy_file(restoredFrom, worldFolder / "WorldOption.sav",
                      fs::copy_options::overwrite_existing, ec);
        if (ec) { error = ec.message(); return false; }
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

bool palworldRunning() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W pe; pe.dwSize = sizeof(pe);
    bool found = false;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, L"Palworld-Win64-Shipping.exe") == 0 ||
                _wcsicmp(pe.szExeFile, L"PalServer-Win64-Shipping.exe") == 0) {
                found = true; break;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return found;
}

}  // namespace pal
