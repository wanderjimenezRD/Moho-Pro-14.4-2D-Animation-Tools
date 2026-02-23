#include "FBExec.h"
#include "FBMaps.h"

#include <spdlog/spdlog.h>

#include "FBMorph.h"
#include "FBActors.h"
#include "FBTransform.h"

static bool TryParseFloat(std::string_view s, float& out) {
    // Minimal local helper. (Later we can move parsing utilities to FBUtil.)
    try {
        out = std::stof(std::string(s));
        return true;
    } catch (...) {
        return false;
    }
}

void FB::Exec::Execute(const FBCommand& cmd, const FBEvent& ctxEvent) {
    if (cmd.type == FBCommandType::Transform && cmd.opcode == "Scale") {
        float scale = 1.0f;
        if (!TryParseFloat(cmd.args, scale)) {
            spdlog::warn("[FB] Exec: failed to parse scale from args='{}'", cmd.args);
            return;
        }

        RE::Actor* actor = FB::Actors::ResolveActorForEvent(ctxEvent, cmd.role);
        if (!actor) {
            spdlog::info("[FB] Exec: could not resolve actor for role={} formID=0x{:08X}",
                         static_cast<std::uint32_t>(cmd.role), ctxEvent.actor.formID);
            return;
        }

        const auto nodeName = FB::Maps::ResolveNode(cmd.target);

        // Safe-any-thread version (queues a task)
        FBTransform::ApplyScale(actor, nodeName, scale);
        return;



    }

    if (cmd.type == FBCommandType::Morph && cmd.opcode == "Set") {
        float value = 0.0f;
        if (!TryParseFloat(cmd.args, value)) {
            spdlog::warn("[FB] Exec: failed to parse morph value from args='{}'", cmd.args);
            return;
        }

        RE::Actor* actor = FB::Actors::ResolveActorForEvent(ctxEvent, cmd.role);
        if (!actor) {
            spdlog::info("[FB] Exec: could not resolve actor for role={} formID=0x{:08X}",
                         static_cast<std::uint32_t>(cmd.role), ctxEvent.actor.formID);
            return;
        }

        const auto morphName = FB::Maps::ResolveMorph(cmd.target);

        // Safe-any-thread version (queues a task)
        FB::Morph::Set(actor, morphName, value);
        return;
    }


    spdlog::info("[FB] Exec: cmd type {} not implemented (opcode='{}')", static_cast<std::uint32_t>(cmd.type),
                 cmd.opcode);
}


void FB::Exec::Execute_MainThread(const FBCommand& cmd, const FBEvent& ctxEvent) {
    // This should be the same logic as Execute(), except it calls _MainThread transform variants.
    if (cmd.type == FBCommandType::Transform && cmd.opcode == "Scale") {
        float scale = 1.0f;
        if (!TryParseFloat(cmd.args, scale)) {
            spdlog::warn("[FB] Exec: failed to parse scale from args='{}'", cmd.args);
            return;
        }

        RE::Actor* actor = FB::Actors::ResolveActorForEvent(ctxEvent, cmd.role);
        if (!actor) {
            spdlog::info("[FB] Exec: could not resolve actor for role={} formID=0x{:08X}",
                         static_cast<std::uint32_t>(cmd.role), ctxEvent.actor.formID);
            return;
        }

        // Node mapping (pass-through if not found)
        const auto nodeName = FB::Maps::ResolveNode(cmd.target);

        // IMPORTANT: we are already on the game thread in Tick()
        FBTransform::ApplyScale_MainThread(actor, nodeName, scale);
        return;
    }

    if (cmd.type == FBCommandType::Morph && cmd.opcode == "Set") {
        float value = 0.0f;
        if (!TryParseFloat(cmd.args, value)) {
            spdlog::warn("[FB] Exec: failed to parse morph value from args='{}'", cmd.args);
            return;
        }

        RE::Actor* actor = FB::Actors::ResolveActorForEvent(ctxEvent, cmd.role);
        if (!actor) {
            spdlog::info("[FB] Exec: could not resolve actor for role={} formID=0x{:08X}",
                         static_cast<std::uint32_t>(cmd.role), ctxEvent.actor.formID);
            return;
        }

        const auto morphName = FB::Maps::ResolveMorph(cmd.target);

        spdlog::info("[FB] Exec: MORPH Set actor=0x{:08X} role={} morph='{}' value={}", actor->formID,
                     static_cast<std::uint32_t>(cmd.role), morphName, value);

        // Defer Papyrus to task queue even from Tick, to avoid VM timing/reentrancy CTDs.
        FB::Morph::Set(actor, morphName, value);
        return;
    }




    spdlog::info("[FB] Exec: cmd type {} not implemented (opcode='{}')", static_cast<std::uint32_t>(cmd.type),
                 cmd.opcode);
}
