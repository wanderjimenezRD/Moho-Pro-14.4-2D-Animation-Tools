#include "FBTransform.h"

#include <spdlog/spdlog.h>

#include <string>

#include "RE/Skyrim.h"
#include "SKSE/SKSE.h"

void FBTransform::ApplyScale_MainThread(RE::Actor* actor, std::string_view nodeName, float scale) {
    if (!actor) {
        spdlog::warn("[FB] Transform.ApplyScale_MainThread: actor=null");
        return;
    }

    if (nodeName.empty()) {
        spdlog::warn("[FB] Transform.ApplyScale_MainThread: nodeName empty");
        return;
    }

    // Clamp so both logging and execution use the same sanitized value.
    if (scale < 0.0f) {
        scale = 0.0f;
    }

    auto* root = actor->Get3D1(false);
    if (!root) {
        spdlog::info("[FB] Transform.ApplyScale_MainThread: actor 0x{:08X} has no 3D root", actor->formID);
        return;
    }

    // Use BSFixedString for lookup
    const RE::BSFixedString bsName(std::string(nodeName).c_str());
    auto* obj = root->GetObjectByName(bsName);
    if (!obj) {
        spdlog::info("[FB] Transform.ApplyScale_MainThread: node '{}' not found on actor 0x{:08X}", nodeName,
                     actor->formID);
        return;
    }

    obj->local.scale = scale;

    spdlog::info("[FB] Transform.ApplyScale_MainThread: APPLIED actor=0x{:08X} node='{}' scale={}", actor->formID,
                  nodeName, scale);
}

void FBTransform::ApplyScale(RE::Actor* actor, std::string_view nodeName, float scale) {
    if (!actor) {
        spdlog::warn("[FB] Transform.ApplyScale: actor=null");
        return;
    }

    if (nodeName.empty()) {
        spdlog::warn("[FB] Transform.ApplyScale: nodeName empty");
        return;
    }

    // Clamp here so both logging and execution use the same sanitized value.
    if (scale < 0.0f) {
        scale = 0.0f;
    }

    auto* taskInterface = SKSE::GetTaskInterface();
    if (!taskInterface) {
        spdlog::error("[FB] Transform.ApplyScale: SKSE task interface is null");
        return;
    }

    // Copy nodeName because we will execute later on the task interface.
    const std::string nodeStr(nodeName);

    // Use a handle so the actor can be safely resolved later.
    const RE::ActorHandle handle = actor->CreateRefHandle();

    // IMPORTANT: no move-captures; keep lambda copyable for TaskFn
    taskInterface->AddTask([handle, nodeStr, scale]() {
        auto aPtr = handle.get();
        RE::Actor* a = aPtr.get();
        if (!a) {
            spdlog::info("[FB] Transform.ApplyScale(task): actor handle resolved to null");
            return;
        }

        // Now we are on the game thread, apply immediately:
        FBTransform::ApplyScale_MainThread(a, nodeStr, scale);
    });

    spdlog::info("[FB] Transform.ApplyScale: queued actor=0x{:08X} node='{}' scale={}", actor->formID, nodeName, scale);
}

bool FBTransform::TryGetScale(RE::Actor* actor, std::string_view nodeName, float& outScale) {
    if (!actor || nodeName.empty()) {
        return false;
    }

    auto* root = actor->Get3D1(false);
    if (!root) {
        return false;
    }

    const RE::BSFixedString bsName(std::string(nodeName).c_str());
    auto* obj = root->GetObjectByName(bsName);
    if (!obj) {
        return false;
    }

    outScale = obj->local.scale;

    return true;
}



void FBTransform::ApplyTranslate_MainThread(RE::Actor* actor, std::string_view nodeName, float x, float y, float z) {
    if (!actor) {
        spdlog::warn("[FB] Transform.ApplyTranslate_MainThread: actor=null");
        return;
    }
    if (nodeName.empty()) {
        spdlog::warn("[FB] Transform.ApplyTranslate_MainThread: nodeName empty");
        return;
    }

    auto* root = actor->Get3D1(false);
    if (!root) {
        spdlog::info("[FB] Transform.ApplyTranslate_MainThread: actor 0x{:08X} has no 3D root", actor->formID);
        return;
    }

    const RE::BSFixedString bsName(std::string(nodeName).c_str());
    auto* obj = root->GetObjectByName(bsName);
    if (!obj) {
        spdlog::info("[FB] Transform.ApplyTranslate_MainThread: node '{}' not found on actor 0x{:08X}", nodeName,
                     actor->formID);
        return;
    }

    // Write local-space translation
    obj->local.translate.x = x;
    obj->local.translate.y = y;
    obj->local.translate.z = z;

    // Force the scene graph to recompute transforms/bounds
    obj->GetFlags().set(RE::NiAVObject::Flag::kForceUpdate);

    RE::NiUpdateData data{};
    data.time = 0.0f;
    data.flags = RE::NiUpdateData::Flag::kDirty;

    obj->UpdateWorldData(&data);
    obj->UpdateWorldBound();

    // (Helps with culling/bounds propagation; cheap enough)
    root->UpdateWorldBound();

    spdlog::debug("[FB] Transform.ApplyTranslate_MainThread: APPLIED actor=0x{:08X} node='{}' pos=({}, {}, {})",
                  actor->formID, nodeName, x, y, z);
}

void FBTransform::ApplyTranslate(RE::Actor* actor, std::string_view nodeName, float x, float y, float z) {
    if (!actor) {
        spdlog::warn("[FB] Transform.ApplyTranslate: actor=null");
        return;
        
    }
    if (nodeName.empty()) {
        spdlog::warn("[FB] Transform.ApplyTranslate: nodeName empty");
        return;
        
    }
    auto* taskInterface = SKSE::GetTaskInterface();
    if (!taskInterface) {
        spdlog::error("[FB] Transform.ApplyTranslate: SKSE task interface is null");
        return;
        
    }
    const std::string nodeStr(nodeName);
    const RE::ActorHandle handle = actor->CreateRefHandle();
    taskInterface->AddTask([handle, nodeStr, x, y, z]() {
        auto aPtr = handle.get();
        RE::Actor* a = aPtr.get();
        if (!a) {
            spdlog::info("[FB] Transform.ApplyTranslate(task): actor handle resolved to null");
            return;
            
        }
        FBTransform::ApplyTranslate_MainThread(a, nodeStr, x, y, z);
        
    });
    spdlog::info("[FB] Transform.ApplyTranslate: queued actor=0x{:08X} node='{}' pos=({}, {}, {})", actor->formID,
                    nodeName, x, y, z);
    
}

bool FBTransform::TryGetTranslate(RE::Actor* actor, std::string_view nodeName, std::array<float, 3>& outTranslate) {
    if (!actor || nodeName.empty()) {
        return false;
        
    }
    auto* root = actor->Get3D1(false);
    if (!root) {
        return false;
        
    }
    const RE::BSFixedString bsName(std::string(nodeName).c_str());
    auto* obj = root->GetObjectByName(bsName);
    if (!obj) {
        return false;
        
    }
    outTranslate[0] = obj->local.translate.x;
    outTranslate[1] = obj->local.translate.y;
    outTranslate[2] = obj->local.translate.z;
    return true;
    
}