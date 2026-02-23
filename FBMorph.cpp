#include "FBMorph.h"
#include "FBMaps.h"

#include <RE/Skyrim.h>
#include <RE/F/FunctionArguments.h>
#include <RE/S/SkyrimVM.h>
#include <SKSE/SKSE.h>

#include <spdlog/spdlog.h>
#include <string>
#include <string_view>
#include <cmath>


namespace
{
    static RE::BSScript::IVirtualMachine* GetVM() {
        if (auto* skyrimVM = RE::SkyrimVM::GetSingleton(); skyrimVM) {
            return skyrimVM->impl ? skyrimVM->impl.get() : nullptr;
        }
        return nullptr;
    }

    static float Normalize01(float v) {
        // Allow authoring 0..1 or 0..100
        if (v > 1.0f) {
            v /= 100.0f;
        }
        return std::clamp(v, 0.0f, 1.0f);
    }

    static std::int32_t NormalizeStrength100(float v) {
        // Allow 0..1 or 0..100
        if (v <= 1.0f) {
            v *= 100.0f;
        }
        auto i = static_cast<std::int32_t>(std::lround(v));
        return std::clamp(i, 0, 100);
    }

    static bool DispatchActorMethod2(RE::Actor* actor, const char* fnName, RE::BSScript::IFunctionArguments* args) {
        if (!actor || !fnName) {
            return false;
        }

        auto* vm = GetVM();
        if (!vm) {
            return false;
        }

        auto* policy = vm->GetObjectHandlePolicy();
        if (!policy) {
            return false;
        }

        const auto handle = policy->GetHandleForObject(actor->GetFormType(), actor);
        if (!handle || handle == policy->EmptyHandle()) {
            return false;
        }

        RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> result{};
        return vm->DispatchMethodCall(handle, RE::BSFixedString("Actor"), RE::BSFixedString(fnName), args, result);
    }

    static bool Actor_SetExpressionPhoneme(RE::Actor* actor, std::int32_t idx, float value01) {
        auto* args = RE::MakeFunctionArguments(static_cast<std::int32_t>(idx), static_cast<float>(value01));
        return DispatchActorMethod2(actor, "SetExpressionPhoneme", args);
    }

    static bool Actor_SetExpressionModifier(RE::Actor* actor, std::int32_t idx, float value01) {
        auto* args = RE::MakeFunctionArguments(static_cast<std::int32_t>(idx), static_cast<float>(value01));
        return DispatchActorMethod2(actor, "SetExpressionModifier", args);
    }

    static bool Actor_SetExpressionOverride(RE::Actor* actor, std::int32_t moodId, std::int32_t strength) {
        auto* args = RE::MakeFunctionArguments(static_cast<std::int32_t>(moodId), static_cast<std::int32_t>(strength));
        return DispatchActorMethod2(actor, "SetExpressionOverride", args);
    }



     static void Papyrus_SetMorph(RE::Actor* actor, const char* morphName, float value) {
        if (!actor || !morphName) {
            return;
        }

        auto* vm = GetVM();
        if (!vm) {
            spdlog::warn("[FB] Morph: VM not available; SetMorph skipped");
            return;
        }

        spdlog::info("[FB] Morph: dispatch {}.{} actor=0x{:08X} morph='{}' value={}", FB::Morph::kBridgeClass,
                     FB::Morph::kFnSetMorph, actor->formID, morphName, value);

        RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> result{};

        // FBMorphBridge.FBSetMorph(Actor akActor, String morphName, Float value)
        auto* args = RE::MakeFunctionArguments(static_cast<RE::Actor*>(actor), RE::BSFixedString(morphName),
                                               static_cast<float>(value));

        const bool ok = vm->DispatchStaticCall(RE::BSFixedString(FB::Morph::kBridgeClass),
                                               RE::BSFixedString(FB::Morph::kFnSetMorph), args, result);

        spdlog::debug("[FB] MorphBridgeCall: {}.{} ok={} morph='{}' value={}", FB::Morph::kBridgeClass,
                      FB::Morph::kFnSetMorph, ok, morphName, value);
     }

    static void Papyrus_ClearMorph(RE::Actor* actor, const char* morphName) {
        if (!actor || !morphName) {
            return;
        }

        auto* vm = GetVM();
        if (!vm) {
            spdlog::warn("[FB] Morph: VM not available; ClearMorph skipped");
            return;
        }

        RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> result{};

        auto* args = RE::MakeFunctionArguments(static_cast<RE::Actor*>(actor), RE::BSFixedString(morphName));

        spdlog::info("[FB] Morph: dispatch {}.{} actor=0x{:08X} morph='{}'", FB::Morph::kBridgeClass,
                     FB::Morph::kFnClear, actor->formID, morphName);

        const bool ok = vm->DispatchStaticCall(RE::BSFixedString(FB::Morph::kBridgeClass),
                                               RE::BSFixedString(FB::Morph::kFnClear), args, result);

        spdlog::info("[FB] Morph: dispatch returned ok={}", ok);
    }
}

namespace FB::Morph {
    void Set(RE::Actor* actor, std::string_view morphName, float value) {
        if (!actor || morphName.empty()) {
            return;
        }

        // 1) Resolve aliases (pass-through if not found)
        const auto resolved = FB::Maps::ResolveMorph(morphName);

        // 2) Expression routing (phoneme / modifier / mood)
        if (auto idx = FB::Maps::TryGetPhonemeIndex(resolved); idx) {
            const float v01 = Normalize01(value);
            Actor_SetExpressionPhoneme(actor, *idx, v01);
            spdlog::info("[FB] Morph: expression phoneme '{}' idx={} value01={}", resolved, *idx, v01);
            return;
        }

        if (auto midx = FB::Maps::TryGetModifierIndex(resolved); midx) {
            const float v01 = Normalize01(value);
            Actor_SetExpressionModifier(actor, *midx, v01);
            spdlog::info("[FB] Morph: expression modifier '{}' idx={} value01={}", resolved, *midx, v01);
            return;
        }

        if (auto mood = FB::Maps::TryGetMoodId(resolved); mood) {
            const auto strength = NormalizeStrength100(value);
            Actor_SetExpressionOverride(actor, *mood, strength);
            spdlog::info("[FB] Morph: expression mood '{}' id={} strength={}", resolved, *mood, strength);
            return;
        }

        // 3) Otherwise treat as RaceMenu morph name
        std::string nameCopy(resolved);
        Papyrus_SetMorph(actor, nameCopy.c_str(), value);
    }


    void Clear_MainThread(RE::Actor* actor, std::string_view morphName) {
        if (!actor || morphName.empty()) {
            return;
        }

        const auto resolved = FB::Maps::ResolveMorph(morphName);

        // Expressions: set back to neutral
        if (auto idx = FB::Maps::TryGetPhonemeIndex(resolved); idx) {
            Actor_SetExpressionPhoneme(actor, *idx, 0.0f);
            return;
        }

        if (auto midx = FB::Maps::TryGetModifierIndex(resolved); midx) {
            Actor_SetExpressionModifier(actor, *midx, 0.0f);
            return;
        }

        if (auto mood = FB::Maps::TryGetMoodId(resolved); mood) {
            // neutralize mood; safest is set strength 0 on Neutral
            Actor_SetExpressionOverride(actor, 7, 0);
            return;
        }

        // RaceMenu morph
        std::string nameCopy(resolved);
        Papyrus_ClearMorph(actor, nameCopy.c_str());
    }


    void Clear(RE::Actor* actor, std::string_view morphName) {

            Clear_MainThread(actor, morphName);
        }
    //    if (!actor || morphName.empty()) {
     //       return;
      //  }

    //    auto* task = SKSE::GetTaskInterface();
     //   if (!task) {
      //      spdlog::warn("[FB] Morph.Clear: task interface missing");
    //        return;
    //    }

   //     const RE::ActorHandle handle = actor->CreateRefHandle();
   //     const std::string nameCopy(morphName);

   //     task->AddTask([handle, nameCopy]() {
    //        auto aPtr = handle.get();
     //       auto* a = aPtr.get();
    //        if (!a) {
     //           spdlog::info("[FB] Morph.Clear(task): actor handle resolved to null");
    //            return;
    //        }

            // extra safety: actor might unload between queue and execution
  //          if (!a->Get3D1(false)) {
  //              spdlog::info("[FB] Morph.Clear(task): skip (3D not loaded) actor=0x{:08X}", a->formID);
     //           return;
   //         }

  //          Clear_MainThread(a, nameCopy);
  //      });
 //   }
}
