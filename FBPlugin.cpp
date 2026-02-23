#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <utility>

#include "FBPlugin.h"
#include "FBConfig.h"
#include "FBEvents.h"
#include "FBActors.h"
#include "FBUpdate.h"
#include "FBUpdatePump.h"
#include "RE/Skyrim.h"
#include "SKSE/SKSE.h"
#include "FBHotkeys.h"

static FBConfig g_config;
static FBEvents g_events;
static std::unique_ptr<FBUpdate> g_update;
static std::unique_ptr<FBUpdatePump> g_pump;
FBUpdate* FB::GetUpdate() { return g_update.get(); }

namespace {
    FBConfig* g_config_ptr = nullptr;
    bool Papyrus_ReloadConfig(RE::StaticFunctionTag*);
    std::int32_t Papyrus_DrainEvents(RE::StaticFunctionTag*);
    std::int32_t Papyrus_TickOnce(RE::StaticFunctionTag*);

    // IMPORTANT:
    // Do NOT patch all vtables. Some entries in RE::VTABLE_* arrays are not Actor-layout
    // and will crash when you replace vfunc slots.
    //
    // Iterate this ONE value across runs: 0,1,2,... until you get periodic Hook tick logs.
    // If an index crashes at startup, revert and try the next.
    constexpr std::size_t kCharacterVtableIndex = 9;

    struct Actor_UpdateAnimation_Hook {
        static void thunk(RE::Actor* self, float delta) {
            static bool s_once = false;
            if (!s_once) {
                s_once = true;
                spdlog::info("[FB] Hook tick FIRST HIT (pre): self=0x{:08X}", self ? self->formID : 0);
            }

            static std::uint32_t s_count = 0;
            if ((++s_count % 60) == 0) {
                spdlog::info("[FB] Hook tick: UpdateAnimation self=0x{:08X}", self ? self->formID : 0);
            }

            func(self, delta);  // original

            auto* actor = self ? self->As<RE::Actor>() : nullptr;
            if (!actor) {
                return;
            }
            if (auto* up = FB::GetUpdate()) {
                up->ApplyPostAnimSustainForActor(actor, 0);
                up->ApplyPostAnimSustainForActor(actor, 1);
                up->ApplyPostAnimSustainForActor(actor, 2);
            }

            auto* taskInterface = SKSE::GetTaskInterface();
            if (taskInterface) {
                const RE::ActorHandle handle = actor->CreateRefHandle();
                taskInterface->AddTask([handle]() {
                    auto aPtr = handle.get();
                    RE::Actor* a = aPtr.get();
                    if (!a) {
                        return;
                    }
                    if (auto* up = FB::GetUpdate()) {
                        up->ApplyPostAnimSustainForActor(a, 2);
                    }
                });
            }

            static std::uint32_t s_lateTickCounter = 0;
            // optional phase 1 (second hit) – safe + helps fight flicker
            // Sustain: task-only, 2 hits (late this frame + next frame) to beat late writers.
            if ((++s_lateTickCounter % 1) == 0) {  // every ~3 UpdateAnimation calls
                if (auto* taskInterface = SKSE::GetTaskInterface()) {
                    const RE::ActorHandle handle = self ? self->CreateRefHandle() : RE::ActorHandle{};
                    taskInterface->AddTask([handle]() {
                        auto aPtr = handle.get();
                        RE::Actor* a = aPtr.get();
                        if (!a) {
                            return;
                        }

                        if (auto* up2 = FB::GetUpdate()) {
                            up2->ApplyPostAnimSustainForActor(a, 1);
                        }
                    });
                }
            }

        }

        static inline REL::Relocation<decltype(thunk)> func;
    };



    struct TESObjectREFR_UpdateAnimation_Hook {
        using Fn = void (*)(RE::TESObjectREFR*, float);
        static inline Fn func = nullptr;

        static void thunk(RE::TESObjectREFR* self, float delta) {
            // Call vanilla first
            if (func) {
                func(self, delta);
            }

            // Only actors
            auto* actor = self ? self->As<RE::Actor>() : nullptr;
            if (!actor) {
                return;
            }

            if (!actor->Get3D1(false)) {
                return;
            }

            // Defer sustain to task queue (later than this update call)
            auto* taskInterface = SKSE::GetTaskInterface();
            if (!taskInterface) {
                return;
            }

            RE::ActorHandle handle = actor->CreateRefHandle();
            taskInterface->AddTask([handle]() {
                auto aPtr = handle.get();
                RE::Actor* a = aPtr.get();
                if (!a || !a->Get3D1(false)) {
                    return;
                }

                if (auto* up = FB::GetUpdate()) {
                    up->ApplyPostAnimSustainForActor(a, 0);  // phase 0 (immediate)
                }

                // phase 1 (deferred): schedule one more task
                if (auto* taskInterface2 = SKSE::GetTaskInterface()) {
                    taskInterface2->AddTask([handle]() {
                        auto aPtr2 = handle.get();
                        RE::Actor* a2 = aPtr2.get();
                        if (!a2 || !a2->Get3D1(false)) {
                            return;
                        }

                        if (auto* up2 = FB::GetUpdate()) {
                            up2->ApplyPostAnimSustainForActor(a2, 1);  // phase 1
                        }
                    });
                }
            });


        }
    };

    void InstallHooks() {
        REL::Relocation<std::uintptr_t> vtbl{RE::VTABLE_TESObjectREFR[0]};
        const std::uintptr_t orig = vtbl.write_vfunc(0x7D, &TESObjectREFR_UpdateAnimation_Hook::thunk);

        TESObjectREFR_UpdateAnimation_Hook::func = reinterpret_cast<TESObjectREFR_UpdateAnimation_Hook::Fn>(orig);

        spdlog::info("[FB] Hook: TESObjectREFR::UpdateAnimation vfunc installed (orig=0x{:016X})", orig);
    }

    void SetupLogging() {
        auto path = SKSE::log::log_directory();
        if (!path) {
            return;
        }

        *path /= "FullBodiedLog.log";

        auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
        auto logger = std::make_shared<spdlog::logger>("global", std::move(sink));

        spdlog::set_default_logger(std::move(logger));
        spdlog::set_level(spdlog::level::info);
        spdlog::flush_on(spdlog::level::info);
        spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%1] %v");
    }

    bool Papyrus_ReloadConfig(RE::StaticFunctionTag*) {
        spdlog::info("[FB] Papyrus: ReloadConfig() called");

        const bool ok = g_config.Reload();
        if (!ok) {
            spdlog::warn("[FB] Papyrus: ReloadConfig failed; keeping existing snapshot");
            return false;
        }

        spdlog::info("[FB] Papyrus: ReloadConfig ok; gen={}", g_config.GetGeneration());
        return true;
    }

    bool RegisterPapyrus() {
        g_config_ptr = std::addressof(g_config);

        auto* papyrus = SKSE::GetPapyrusInterface();
        if (!papyrus) {
            spdlog::error("[FB] Papyrus interface unavailable");
            return false;
        }

        return papyrus->Register([](RE::BSScript::IVirtualMachine* vm) {
            vm->RegisterFunction("ReloadConfig", "FullBodiedQuestScript", Papyrus_ReloadConfig);
            vm->RegisterFunction("DrainEvents", "FullBodiedQuestScript", Papyrus_DrainEvents);
            vm->RegisterFunction("TickOnce", "FullBodiedQuestScript", Papyrus_TickOnce);
            return true;
        });
    }

    std::int32_t Papyrus_DrainEvents(RE::StaticFunctionTag*) {
        auto drained = g_events.Drain();
        spdlog::info("[FB] DrainEvents: drained count={}", drained.size());

        if (!drained.empty()) {
            const auto& e = drained.front();
            spdlog::info("[FB] DrainEvents: first tag='{}' actorFormID=0x{:08X}", e.tag, e.actor.formID);
        }
        return static_cast<std::int32_t>(drained.size());
    }

    std::int32_t Papyrus_TickOnce(RE::StaticFunctionTag*) {
        if (!g_update) {
            spdlog::error("[FB] TickOnce called but FBUpdate not initialized");
            return 0;
        }

        g_update->Tick(1.0f / 60.0f);
        return 1;
    }
}  // namespace


SKSEPluginLoad(const SKSE::LoadInterface* skse) {
    SKSE::Init(skse);
    SetupLogging();
    //SKSE::AllocTrampoline(256);
    spdlog::info("[FB] Plugin loaded");
    InstallHooks();

    if (!g_config.LoadInitial()) {
        spdlog::error("[FB] Config LoadInitial failed");
        return false;
    }

    spdlog::info("[FB] Config generation: {}", g_config.GetGeneration());

    g_update = std::make_unique<FBUpdate>(g_config, g_events);
    spdlog::info("[FB] FBUpdate Initialized");

    g_pump = std::make_unique<FBUpdatePump>(*g_update);
    // Removed SetTickHz(): not part of current FBUpdatePump surface.
    g_pump->Start();

    SKSE::GetMessagingInterface()->RegisterListener([](SKSE::MessagingInterface::Message* msg) {
        if (!msg) {
            return;
        }

        if (msg->type == SKSE::MessagingInterface::kDataLoaded) {
            if (!RegisterPapyrus()) {
                spdlog::error("[FB] Papyrus Registration failed");
            } else {
                spdlog::info("[FB] Papyrus registered: FullBodiedQuestScript.ReloadConfig()");
                g_config.Reload();
            }
            FBHotkeys::Install([]() {
                const bool ok = g_config.Reload();
                spdlog::info("[FB] Hotkey: Reload result={} gen={}", ok, g_config.GetGeneration());
            });


            g_events.OnDataLoaded();  // new
        }

        if (msg->type == SKSE::MessagingInterface::kPostLoadGame || msg->type == SKSE::MessagingInterface::kNewGame) {
          
            g_events.OnPostLoadOrNewGame();  // new
        }

    });

    return true;
}
