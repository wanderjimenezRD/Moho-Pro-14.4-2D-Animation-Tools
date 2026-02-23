#include "FBHotkeys.h"

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>
#include <spdlog/spdlog.h>
#include "FBPlugin.h"
#include "FBConfig.h"
#include <utility>

namespace {
    constexpr std::uint32_t kKey_H = 35;
    FBHotkeys::ReloadFn g_reload;

    class HotkeySink : public RE::BSTEventSink<RE::InputEvent*> {
    public:
        RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_event,
                                              RE::BSTEventSource<RE::InputEvent*>*) override {
            if (!a_event) {
                return RE::BSEventNotifyControl::kContinue;
            }

            for (auto* e = *a_event; e; e = e->next) {
                if (e->eventType != RE::INPUT_EVENT_TYPE::kButton) {
                    continue;
                }

                auto* btn = e->AsButtonEvent();
                if (!btn) {
                    continue;
                }

                if (btn->device.get() != RE::INPUT_DEVICE::kKeyboard) {
                    continue;
                }

                if (btn->idCode != kKey_H) {
                    continue;
                }

                // initial press only
                if (btn->value > 0.0f && btn->heldDownSecs == 0.0f) {
                    spdlog::info("[FB] Hotkey: H pressed -> Reload()");
                    if (g_reload) {
                        g_reload();
                    }
                }
            }

            return RE::BSEventNotifyControl::kContinue;
        }
    };

    HotkeySink g_sink;
}


void FBHotkeys::Install(ReloadFn reload) {
    g_reload = std::move(reload);

    auto* input = RE::BSInputDeviceManager::GetSingleton();
    if (!input) {
        spdlog::warn("[FB] Hotkeys: BSInputDeviceManager not available");
        return;
    }

    input->AddEventSink(&g_sink);
    spdlog::info("[FB] Hotkeys: installed (H=reload)");
}
