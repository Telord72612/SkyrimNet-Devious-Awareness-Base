#include "PCH.h"
#include "PromptState.h"
#include "WornDevices.h"
#include "TaskRelay.h"

#include <atomic>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>

// The writer shape is VRTouchEvents' PromptState.cpp (VR-verified 2026-09-13, report 45), with the
// entry widened from one number to the whole prompt content.
#ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#    define NOMINMAX
#endif
#include <windows.h>

namespace logger = SKSE::log;

namespace
{
    // SkyrimNet API v3+: FormID -> the entity UUID a template sees as npc.UUID. 0 = not known yet.
    std::uint64_t (*PublicFormIDToUUID)(std::uint32_t) = nullptr;

    std::mutex                              g_mtx;
    std::map<std::uint32_t, std::uint64_t>  g_uuid;      // FormID -> resolved UUID (0 = retry)
    std::string                             g_lastText;  // what is on disk now
    std::atomic<bool>                       g_refreshQueued{ false };

    // Relative to the game's working directory, so MO2's virtual filesystem resolves it exactly like
    // SkyrimNet's read_json does ("Data\SKSE\Plugins\<mod>\<file>.json").
    const std::filesystem::path kDir  = std::filesystem::path("Data") / "SKSE" / "Plugins" / "VRTE_DDZaZ";
    const std::filesystem::path kFile = kDir / "prompt_state.json";
    const std::filesystem::path kTmp  = kDir / "prompt_state.json.tmp";

    std::string JsonEscape(const std::string& s)
    {
        std::string out;
        out.reserve(s.size() + 8);
        for (const unsigned char c : s) {
            switch (c) {
            case '"':  out += "\\\""; break;
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
                    out += static_cast<char>(c);
                }
            }
        }
        return out;
    }

    std::uint64_t UuidOf(std::uint32_t fid)   // caller holds g_mtx
    {
        auto it = g_uuid.find(fid);
        if (it != g_uuid.end() && it->second != 0) return it->second;
        const std::uint64_t u = PublicFormIDToUUID ? PublicFormIDToUUID(fid) : 0;
        g_uuid[fid] = u;
        return u;
    }

    void Field(std::ostringstream& js, const char* key, const std::string& v)
    {
        js << ",\"" << key << "\":\"" << JsonEscape(v) << "\"";
    }

    // Builds the whole file, writes it to a temp file and swaps it in, so SkyrimNet's read_json can
    // never parse a half-written file. Writes only when the text changed.
    void Publish(const std::string& text, const char* why, int npcs)
    {
        {
            std::lock_guard lk(g_mtx);
            if (text == g_lastText) return;
            g_lastText = text;
        }
        std::error_code ec;
        std::filesystem::create_directories(kDir, ec);
        {
            std::ofstream f(kTmp, std::ios::binary | std::ios::trunc);
            if (!f) {
                logger::warn("[PROMPT-STATE] cannot open {} for writing ({}) - state NOT published", kTmp.string(), why);
                return;
            }
            f.write(text.data(), static_cast<std::streamsize>(text.size()));
        }
        if (!::MoveFileExW(kTmp.wstring().c_str(), kFile.wstring().c_str(),
                           MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            const auto err = ::GetLastError();
            std::ofstream f(kFile, std::ios::binary | std::ios::trunc);
            if (!f) {
                logger::warn("[PROMPT-STATE] swap failed (err {}) and direct write failed ({})", err, why);
                return;
            }
            f.write(text.data(), static_cast<std::streamsize>(text.size()));
            logger::warn("[PROMPT-STATE] swap failed (err {}) - wrote directly instead ({})", err, why);
        }
        logger::info("[PROMPT-STATE] {} -> {} npc(s), {} bytes", why, npcs, text.size());
    }
}

namespace PromptState
{
    void Install()
    {
        HMODULE h = ::LoadLibraryA("SkyrimNet");
        if (h) {
            auto* getVersion = reinterpret_cast<int (*)()>(::GetProcAddress(h, "PublicGetVersion"));
            const int version = getVersion ? getVersion() : 0;
            if (version >= 3) {
                PublicFormIDToUUID = reinterpret_cast<std::uint64_t (*)(std::uint32_t)>(
                    ::GetProcAddress(h, "PublicFormIDToUUID"));
            }
            logger::info("[PROMPT-STATE] SkyrimNet API v{} - FormIDToUUID {}", version,
                         PublicFormIDToUUID ? "resolved" : "MISSING (entries will carry uuid 0 and never match)");
        } else {
            logger::warn("[PROMPT-STATE] SkyrimNet.dll not loaded - prompt state is written but nothing reads it");
        }
        Reset();
    }

    void Reset()
    {
        {
            std::lock_guard lk(g_mtx);
            g_uuid.clear();
            g_lastText.clear();   // force the write even if the file already reads empty
        }
        Publish("{\"version\":1,\"npcs\":[]}", "empty (load boundary)", 0);
    }

    void Refresh(const char* why)
    {
        g_refreshQueued.store(false, std::memory_order_relaxed);
        std::ostringstream js;
        js << "{\"version\":1,\"npcs\":[";
        int  n     = 0;
        bool first = true;
        for (const std::uint32_t fid : WornDevices::PromptActors()) {
            auto* f = RE::TESForm::LookupByID(fid);
            auto* a = f ? f->As<RE::Actor>() : nullptr;
            if (!a) continue;
            WornDevices::PromptFields e;
            if (!WornDevices::PromptEntry(a, e)) continue;
            std::uint64_t uuid = 0;
            {
                std::lock_guard lk(g_mtx);
                uuid = UuidOf(fid);
            }
            if (!first) js << ",";
            first = false;
            ++n;
            js << "{\"uuid\":" << uuid << ",\"formId\":" << fid << ",\"name\":\"" << JsonEscape(e.name) << "\"";
            Field(js, "worn",          e.worn);
            Field(js, "worn_visible",  e.wornVisible);
            Field(js, "gag",           e.gag);
            Field(js, "blind",         e.blind);
            Field(js, "arms",          e.arms);
            Field(js, "legs",          e.legs);
            Field(js, "all",           e.all);
            Field(js, "deaf",          e.deaf);
            Field(js, "after",         e.after);
            Field(js, "after_visible", e.afterVisible);
            Field(js, "ungag",         e.ungag);
            Field(js, "unblind",       e.unblind);
            Field(js, "undeaf",        e.undeaf);
            Field(js, "unbound_arms",  e.unboundArms);
            Field(js, "unbound_legs",  e.unboundLegs);
            Field(js, "unbound_all",   e.unboundAll);
            js << "}";
            if (uuid == 0) {
                logger::warn("[PROMPT-STATE] 0x{:08X} '{}' has no SkyrimNet UUID yet - re-resolved on the next refresh",
                             fid, e.name);
            }
        }
        js << "]}";
        Publish(js.str(), why, n);
    }

    void RequestRefresh(const char* why)
    {
        if (g_refreshQueued.exchange(true, std::memory_order_relaxed)) return;   // one in flight is enough
        if (!SKSE::GetTaskInterface()) { g_refreshQueued.store(false, std::memory_order_relaxed); return; }
        std::string w(why);
        // 1.3.6: through the relay - "equip" is requested from inside the TESEquipEvent sink (TaskRelay.h)
        TaskRelay::Add([w]() { Refresh(w.c_str()); });
    }
}
