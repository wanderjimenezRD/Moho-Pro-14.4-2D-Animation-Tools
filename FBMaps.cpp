#include "FBMaps.h"

#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <optional>

namespace {
    // string literals only => stable string_view targets
    static const std::unordered_map<std::string_view, std::string_view> kNodeMap{
        // --- ROOT / CORE ---
        {"NPC", "NPC"},
        {"Root", "NPC Root [Root]"},
        {"COM", "NPC COM [COM ]"},  // note: COM has a trailing space inside brackets in some lists

        // --- TORSO / HEAD ---
        {"Pelvis", "NPC Pelvis [Pelv]"},
        {"Spine", "NPC Spine [Spn0]"},
        {"Spine0", "NPC Spine [Spn0]"},
        {"Spine1", "NPC Spine1 [Spn1]"},
        {"Spine2", "NPC Spine2 [Spn2]"},
        {"Neck", "NPC Neck [Neck]"},
        {"Head", "NPC Head [Head]"},

        // --- LEFT ARM ---
        {"LClavicle", "NPC L Clavicle [LClv]"},
        {"LeftClavicle", "NPC L Clavicle [LClv]"},
        {"LUpperArm", "NPC L UpperArm [LUar]"},
        {"LeftUpperArm", "NPC L UpperArm [LUar]"},
        {"LForearm", "NPC L Forearm [LLar]"},
        {"LeftForearm", "NPC L Forearm [LLar]"},
        {"LHand", "NPC L Hand [LHnd]"},
        {"LeftHand", "NPC L Hand [LHnd]"},

        // --- RIGHT ARM ---
        {"RClavicle", "NPC R Clavicle [RClv]"},
        {"RightClavicle", "NPC R Clavicle [RClv]"},
        {"RUpperArm", "NPC R UpperArm [RUar]"},
        {"RightUpperArm", "NPC R UpperArm [RUar]"},
        {"RForearm", "NPC R Forearm [RLar]"},
        {"RightForearm", "NPC R Forearm [RLar]"},
        {"RHand", "NPC R Hand [RHnd]"},
        {"RightHand", "NPC R Hand [RHnd]"},

        // --- LEFT LEG ---
        {"LThigh", "NPC L Thigh [LThg]"},
        {"LeftThigh", "NPC L Thigh [LThg]"},
        {"LCalf", "NPC L Calf [LClf]"},
        {"LeftCalf", "NPC L Calf [LClf]"},
        {"LFoot", "NPC L Foot [LLft ]"},
        {"LeftFoot", "NPC L Foot [LLft ]"},
        {"LToe", "NPC L Toe0 [LToe]"},
        {"LeftToe", "NPC L Toe0 [LToe]"},

        // --- RIGHT LEG ---
        {"RThigh", "NPC R Thigh [RThg]"},
        {"RightThigh", "NPC R Thigh [RThg]"},
        {"RCalf", "NPC R Calf [RClf]"},
        {"RightCalf", "NPC R Calf [RClf]"},
        {"RFoot", "NPC R Foot [Rft ]"},
        {"RightFoot", "NPC R Foot [Rft ]"},
        {"RToe", "NPC R Toe0 [RToe]"},
        {"RightToe", "NPC R Toe0 [RToe]"},

        // --- QUALITY-OF-LIFE ALIASES (your older short tokens, just in case) ---
        {"Pelv", "NPC Pelvis [Pelv]"},
        {"Spn0", "NPC Spine [Spn0]"},
        {"Spn1", "NPC Spine1 [Spn1]"},
        {"Spn2", "NPC Spine2 [Spn2]"},
        {"Neck", "NPC Neck [Neck]"},
        {"Head", "NPC Head [Head]"},
        {"LThg", "NPC L Thigh [LThg]"},
        {"RThg", "NPC R Thigh [RThg]"},
        {"LClf", "NPC L Calf [LClf]"},
        {"RClf", "NPC R Calf [RClf]"},
        {"LLft", "NPC L Foot [LLft ]"},
        {"Rft", "NPC R Foot [Rft ]"},
        {"LHnd", "NPC L Hand [LHnd]"},
        {"RHnd", "NPC R Hand [RHnd]"},
        {"LUar", "NPC L UpperArm [LUar]"},
        {"RUar", "NPC R UpperArm [RUar]"},
        {"LLar", "NPC L Forearm [LLar]"},
        {"RLar", "NPC R Forearm [RLar]"},
        {"LClv", "NPC L Clavicle [LClv]"},
        {"RClv", "NPC R Clavicle [RClv]"},
    };

    const std::unordered_map<std::string_view, std::string_view> kMorphMap{
        // From FBMorph - OLD.h (trimmed to what you actually want)
        {"PreyBelly", "Vore Prey Belly"},       
        {"PreyBelly2", "Vore Prey Belly 2"},
        {"PreyBelly3", "Vore Prey Belly 3"},    
        {"StruggleBumps1", "Struggle Bumps 1"},
        {"StruggleBumps2", "Struggle Bumps 2"}, 
        {"StruggleBumps3", "Struggle Bumps 3"},
        {"Swallow1", "FB Swallow 1"},
    };

    // log unknown keys once to avoid spam
    static std::unordered_set<std::string> g_unknownNodeKeys;
}

namespace FB::Maps {
    std::string_view ResolveNode(std::string_view key) {
        if (key.empty()) {
            return key;
        }

        if (auto it = kNodeMap.find(key); it != kNodeMap.end()) {
            return it->second;
        }

        // pass-through, but log once
        if (g_unknownNodeKeys.insert(std::string(key)).second) {
            spdlog::debug("[FB] Maps: ResolveNode pass-through key='{}'", key);
        }

        return key;
    }

    std::string_view ResolveMorph(std::string_view key) {
        if (key.empty()) {
            return key;
        }
        if (auto it = kMorphMap.find(key); it != kMorphMap.end()) {
            return it->second;
        }
        return key;  // pass-through
    }

     std::optional<std::int32_t> TryGetPhonemeIndex(std::string_view name) {
        using sv = std::string_view;
        if (name == sv{"Aah"}) return 0;
        if (name == sv{"BigAah"}) return 1;
        if (name == sv{"BMP"}) return 2;
        if (name == sv{"ChJSh"}) return 3;
        if (name == sv{"DST"}) return 4;
        if (name == sv{"Eee"}) return 5;
        if (name == sv{"Eh"}) return 6;
        if (name == sv{"FV"}) return 7;
        if (name == sv{"I"}) return 8;
        if (name == sv{"K"}) return 9;
        if (name == sv{"N"}) return 10;
        if (name == sv{"Oh"}) return 11;
        if (name == sv{"OohQ"}) return 12;
        if (name == sv{"R"}) return 13;
        if (name == sv{"Th"}) return 14;
        if (name == sv{"W"}) return 15;
        return std::nullopt;
    }

    std::optional<std::int32_t> TryGetMoodId(std::string_view name) {
        using sv = std::string_view;
        if (name == sv{"Neutral"}) return 7;
        if (name == sv{"Anger"}) return 8;
        if (name == sv{"Fear"}) return 9;
        if (name == sv{"Happy"}) return 10;
        if (name == sv{"Sad"}) return 11;
        if (name == sv{"Surprise"}) return 12;
        if (name == sv{"Puzzled"}) return 13;
        if (name == sv{"Disgusted"}) return 14;
        return std::nullopt;
    }

    std::optional<std::int32_t> TryGetModifierIndex(std::string_view name) {
        using sv = std::string_view;

        // Eyes / look
        if (name == sv{"BlinkL"} || name == sv{"BlinkLeft"}) return 0;
        if (name == sv{"BlinkR"} || name == sv{"BlinkRight"}) return 1;
        if (name == sv{"LookDown"}) return 8;
        if (name == sv{"LookLeft"}) return 9;
        if (name == sv{"LookRight"}) return 10;
        if (name == sv{"LookUp"}) return 11;

        if (name == sv{"SquintL"} || name == sv{"SquintLeft"}) return 12;
        if (name == sv{"SquintR"} || name == sv{"SquintRight"}) return 13;

        // Brows
        if (name == sv{"BrowDownL"} || name == sv{"BrowDownLeft"}) return 2;
        if (name == sv{"BrowDownR"} || name == sv{"BrowDownRight"}) return 3;
        if (name == sv{"BrowInL"} || name == sv{"BrowInLeft"}) return 4;
        if (name == sv{"BrowInR"} || name == sv{"BrowInRight"}) return 5;
        if (name == sv{"BrowUpL"} || name == sv{"BrowUpLeft"}) return 6;
        if (name == sv{"BrowUpR"} || name == sv{"BrowUpRight"}) return 7;

        return std::nullopt;
    }
}
