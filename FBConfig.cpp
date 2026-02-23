#include "FBConfig.h"
#include "FBMaps.h"


#include <memory>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <unordered_map>
#include <algorithm>
#include <cctype>
#include <cstring>

namespace {
    std::shared_ptr<const Snapshot> g_snapshot;

    static inline void FBTrimInPlace(std::string& s) {
        auto notSpace = [](unsigned char c) { return !std::isspace(c); };
        s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
        s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
    }

    static inline void StripInlineComment(std::string& s) { 
        auto posHash = s.find('#');
        auto posSemi = s.find(';');
        auto pos = std::min(posHash == std::string::npos ? s.size() : posHash,
                            posSemi == std::string::npos ? s.size() : posSemi);
        if (pos != std::string::npos && pos < s.size()) s.erase(pos);
    }

    static inline bool IEquals(const std::string& a, const char* b) {
        if (a.size() != std::strlen(b)) return false;
        for (size_t i = 0; i < a.size(); i++) {
            if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i])) return false;
        }
        return true;
    }

    static std::vector<std::filesystem::path> GetGeneralIniCandidates() { 
    return {std::filesystem::path("Data") / "FullBodiedIni.ini",
                std::filesystem::path("Data") / "SKSE" / "Plugins" / "FullBodiedIni.ini"};
    }

    static std::filesystem::path GetOARRoot() {
        return std::filesystem::path("Data") / "meshes" / "actors" / "character" / "animations" /
               "OpenAnimationReplacer";
    }

    static std::optional<std::filesystem::path> FindDirRecursive(const std::filesystem::path& root,
                                                                 const std::string& dirName) {
        std::error_code ec;
        if (!std::filesystem::exists(root, ec)) return std::nullopt;

        for (std::filesystem::recursive_directory_iterator it(root, ec), end; it != end && !ec; it.increment(ec)) {
            if (it->is_directory(ec) && it->path().filename().string() == dirName) {
                return it->path();
            }
        }
        return std::nullopt;
    }

    static std::optional<float> ParseFloat(const std::string& s) {
        char* end = nullptr;
        float v = std::strtof(s.c_str(), &end);
        if (end == s.c_str()) return std::nullopt;
        return v;
    }

    static std::string NodeKeyToNiNode(std::string_view key) {
        // Phase 2: minimal mapping for testing
        if (key == "Head") return "NPC Head [Head]";
        return std::string(key);  // fallback (lets you see what’s missing)
    }

}
static bool BuildSnapshotFromIni(Snapshot& out) {
    // 1) Find global ini
    std::filesystem::path generalIni;
    for (auto& p : GetGeneralIniCandidates()) {
        std::ifstream t(p);
        if (t.good()) {
            generalIni = p;
            break;
        }
    }
    if (generalIni.empty()) {
        spdlog::warn("[FB] INI: FullBodiedIni.ini not found under Data; using fallback");
        return false;
    }
    spdlog::info("[FB] INI: using general ini at '{}'", generalIni.string());

    // 2) Parse global ini (only [General] and [FBFiles] for Phase 2)
    bool enableTimelines = true;
    std::unordered_map<std::string, std::string> fbFiles;  // alias -> clip.hkx

    std::ifstream in(generalIni);
    std::string currentSection;
    std::string line;

    while (std::getline(in, line)) {
        StripInlineComment(line);
        FBTrimInPlace(line);
        if (line.empty()) continue;

        if (line.front() == '[' && line.back() == ']') {
            currentSection = line.substr(1, line.size() - 2);
            FBTrimInPlace(currentSection);
            continue;
        }

        auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        FBTrimInPlace(key);
        FBTrimInPlace(val);

        if (IEquals(currentSection, "General")) {
            if (IEquals(key, "EnableTimelines")) {
                enableTimelines = (val == "true" || val == "1" || val == "True");
            }
            if (IEquals(key, "ResetOnPairEnd")) {
                out.ResetOnPairEnd = (val == "true" || val == "1" || IEquals(val, "true"));
            }
            if (IEquals(key, "ResetDelay")) {
                try {
                    out.ResetDelay = std::stof(val);
                    if (out.ResetDelay < 0.0f) {
                        out.ResetDelay = 0.0f;
                    }
                } catch (...) {
                    spdlog::warn("[FB] Config: invalid ResetDelay='{}' (expected seconds float); using 0.0", val);
                    out.ResetDelay = 0.0f;
                }
            }
        
        } else if (IEquals(currentSection, "FBFiles")) {
            if (!key.empty() && !val.empty()) {
                fbFiles[key] = val;
            }
        } else if (IEquals(currentSection, "EventMap") || IEquals(currentSection, "EventToTimeline")) {
            // Optional support if you add it later
            if (!key.empty() && !val.empty()) {
                out.eventMap[key] = val;
            }
        }
        //spdlog::info("[FB] INI: section='{}'", currentSection);

    }

    if (!enableTimelines) {
        spdlog::info("[FB] INI: timelines disabled");
        return true;  // valid empty snapshot
    }

    if (fbFiles.empty()) {
        spdlog::warn("[FB] INI: no [FBFiles] entries");
        return false;
    }

    // If FBEvent isn't mapped and there is exactly one FBFiles entry, default it.
    if (out.eventMap.find("FBEvent") == out.eventMap.end() && fbFiles.size() == 1) {
        const auto& only = *fbFiles.begin();
        out.eventMap["FBEvent"] = only.second;  // + ".hkx";
    }

    // TEMP: keep your harness working without changing other files yet
    //if (out.eventMap.find("FB_TestEvent") == out.eventMap.end() && fbFiles.size() == 1) {
    //    const auto& only = *fbFiles.begin();
    //    out.eventMap["FB_TestEvent"] = only.first + ".hkx";
    //}

    // 3) For each FBFiles entry, find _variants_<clipBase> folder and load FB_<alias>.ini
    const auto oarRoot = GetOARRoot();
    for (auto& [alias, clip] : fbFiles) {
        const std::string scriptKey = clip;
        out.scripts[scriptKey].clear();


        // clipBase = paired_huga from paired_huga.hkx
        std::string clipBase = clip;
        if (clipBase.size() > 4 && clipBase.substr(clipBase.size() - 4) == ".hkx") {
            clipBase.resize(clipBase.size() - 4);
        }
        const std::string variantsDir = "_variants_" + clipBase;
        spdlog::info("[FB] INI: alias='{}' clip='{}' scriptKey='{}'", alias, clip, scriptKey);

        auto folderOpt = FindDirRecursive(oarRoot, variantsDir);
        if (!folderOpt) {
            spdlog::warn("[FB] INI: could not find {} under {}", variantsDir, oarRoot.string());
            continue;
        }

        const auto folder = *folderOpt;
        const auto animIni = folder / ("FB_" + alias + ".ini");
        std::ifstream ain(animIni);
        if (!ain.good()) {
            spdlog::warn("[FB] INI: missing per-anim ini: {}", animIni.string());
            continue;
        }
        

        // Parse per-anim ini
        std::string wantCaster = "FB:" + clip + "|Caster";
        std::string wantTarget = "FB:" + clip + "|Target";
        spdlog::info("[FB] INI: using per-anim ini: {}", animIni.string());
        spdlog::info("[FB] INI: want sections: [{}] and [{}]", wantCaster, wantTarget);
        enum class Sec { None, Caster, Target };
        Sec sec = Sec::None;

        std::string l2;
        while (std::getline(ain, l2)) {
            StripInlineComment(l2);
            FBTrimInPlace(l2);
            if (l2.empty()) continue;

            if (l2.front() == '[' && l2.back() == ']') {
                std::string sect = l2.substr(1, l2.size() - 2);
                FBTrimInPlace(sect);
                if (sect == wantCaster) {
                    spdlog::info("[FB] INI: entered Caster section ({})", sect);
                    sec = Sec::Caster;
                } else if (sect == wantTarget) {
                    spdlog::info("[FB] INI: entered Target section ({})", sect);
                    sec = Sec::Target;
                } else {
                    sec = Sec::None;
                }
                continue;

            }

            if (sec == Sec::None) continue;

            std::istringstream iss(l2);
            std::string timeTok;
            if (!(iss >> timeTok)) continue;

            auto t = ParseFloat(timeTok);

            std::string cmdStr;
            std::getline(iss, cmdStr);
            FBTrimInPlace(cmdStr);

            if (cmdStr.empty()) continue;

            if (!t) continue;

            ActorRole role = (sec == Sec::Caster) ? ActorRole::Caster : ActorRole::Target;

            bool targetOverride = false;
            if (cmdStr.rfind("2_", 0) == 0) {
                targetOverride = true;
                cmdStr = cmdStr.substr(2);
                FBTrimInPlace(cmdStr);
            }

            if (sec == Sec::Caster && targetOverride) {
                role = ActorRole::Target;
            }

            auto open = cmdStr.find('(');
            auto close = cmdStr.rfind(')');
            if (open == std::string::npos || close == std::string::npos || close <= open) {
                continue;
            }

            std::string opAndNode = cmdStr.substr(0, open);
            FBTrimInPlace(opAndNode);

            std::string argStr = cmdStr.substr(open + 1, close - open - 1);
            FBTrimInPlace(argStr);





            FBCommand cmd{};
            cmd.role = role;
            cmd.generation = out.generation;

            if (opAndNode.rfind("FBScale_", 0) == 0) {
                std::string nodeKey = opAndNode.substr(std::string("FBScale_").size());
                FBTrimInPlace(nodeKey);

                const std::string_view resolved = FB::Maps::ResolveNode(nodeKey);

                cmd.type = FBCommandType::Transform;
                cmd.opcode = "Scale";
                cmd.target = std::string(resolved);  // store owning copy
                //cmd.target = niNode;  // may be friendly key or full node; Exec will ResolveNode anyway
                cmd.args = argStr;

            } else if (opAndNode.rfind("FBMorph_", 0) == 0) {
                std::string morphKey = opAndNode.substr(std::string("FBMorph_").size());
                FBTrimInPlace(morphKey);

                cmd.type = FBCommandType::Morph;
                cmd.opcode = "Set";
                cmd.target = morphKey;  // KEEP FRIENDLY KEY; Exec will ResolveMorph (pass-through for now)
                cmd.args = argStr;

            } else {
                continue;
            }


            

            TimedCommand tc{};
            tc.time = *t;
            tc.command = std::move(cmd);

            spdlog::info("[FB] INI: added cmd t={} role={} op='{}' target='{}' args='{}'", tc.time,
                         (tc.command.role == ActorRole::Caster ? "Caster" : "Target"), tc.command.opcode,
                         tc.command.target, tc.command.args);


            out.scripts[scriptKey].push_back(std::move(tc));


            

        }

        // Sort script by time
        auto& list = out.scripts[scriptKey];
        std::sort(list.begin(), list.end(),
                  [](const TimedCommand& a, const TimedCommand& b) { return a.time < b.time; });


        spdlog::info("[FB] INI: parsed {} cmds for script {}", out.scripts[scriptKey].size(), scriptKey);
    }

    for (auto& [k, v] : out.scripts) {
        spdlog::info("[FB] INI: scriptKey='{}' cmds={}", k, v.size());
    }


    return true;
}

static bool TryParseFloat(std::string_view s, float& out) {
    if (auto pos = s.find('='); pos != std::string_view::npos) {
        s = s.substr(pos + 1);
    }

    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);

    std::string tmp(s);
    char* end = nullptr;
    out = std::strtof(tmp.c_str(), &end);
    return end != tmp.c_str();
}


bool FBConfig::LoadInitial() {
    auto snapshot = std::make_shared<Snapshot>();
    snapshot->generation = 1;

    snapshot->eventMap.clear();
    snapshot->scripts.clear();

    if (!BuildSnapshotFromIni(*snapshot)) {
        spdlog::error("[FB] Config: INI parse failed; no fallback will run");

    }

    g_snapshot = std::move(snapshot);
    return true;
}




bool FBConfig::Reload() {
    auto next = std::make_shared<Snapshot>();
    next->generation = GetGeneration() + 1;

    next->eventMap.clear();
    next->scripts.clear();

    if (!BuildSnapshotFromIni(*next)) {
        spdlog::error("[FB] Config: Reload failed; keeping gen={}", GetGeneration());
        return false;
    }

    g_snapshot = std::move(next);
    spdlog::info("[FB] Config: Reload success; gen={}", GetGeneration());
    return true;
}




//Generation FBConfig::GetGeneration() const { return g_snapshot ? g_snapshot->generation : 0; }

std::shared_ptr<const Snapshot> FBConfig::GetSnapshot() const { return g_snapshot; }