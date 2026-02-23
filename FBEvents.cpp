#include "FBEvents.h"

#include <spdlog/spdlog.h>

#include "RE/B/BSAnimationGraphEvent.h"
#include "RE/P/PlayerCharacter.h"
#include "SKSE/SKSE.h"
#include "RE/B/BSAnimationGraphManager.h"


namespace {
    class FBAnimEventSink final : public RE::BSTEventSink<RE::BSAnimationGraphEvent> {
    public:
        explicit FBAnimEventSink(FBEvents* owner) : _owner(owner) {}

        RE::BSEventNotifyControl ProcessEvent(const RE::BSAnimationGraphEvent* evn,
                                              RE::BSTEventSource<RE::BSAnimationGraphEvent>*) override {
            if (evn && _owner) {
                _owner->HandleAnimEvent(*evn);
            }
            return RE::BSEventNotifyControl::kContinue;
        }

        void SetOwner(FBEvents* owner) { _owner = owner; }

    private:
        FBEvents* _owner = nullptr;
    };

    static FBAnimEventSink g_animSink{nullptr};
}




void FBEvents::OnDataLoaded() {
    // First safe point where PlayerCharacter may exist
    TryRegisterToPlayer();
}

void FBEvents::OnPostLoadOrNewGame() {
    // Graph is guaranteed to exist here
    TryRegisterToPlayer();
}

void FBEvents::TryRegisterToPlayer() {
    auto* pc = RE::PlayerCharacter::GetSingleton();
    if (!pc) {
        return;
    }

    RE::BSTSmartPointer<RE::BSAnimationGraphManager> manager;
    if (!pc->GetAnimationGraphManager(manager) || !manager) {
        spdlog::warn("[FB] AnimEvt: no animation graph manager (yet)");
        return;
    }

    g_animSink.SetOwner(this);

    bool attached = false;
    for (auto& graph : manager->graphs) {
        if (!graph) {
            continue;
        }
        graph->AddEventSink(&g_animSink);
        attached = true;
    }

    if (attached) {
        if (!_registered.exchange(true)) {
            spdlog::info("[FB] AnimEvt: registered sinks to player graphs");
            spdlog::info("[FB] AnimEvt: ready (logAllTags={})", _logAllAnimTags.load());

        }
    } else {
        spdlog::warn("[FB] AnimEvt: no graphs on player manager");
    }
}


void FBEvents::HandleAnimEvent(const RE::BSAnimationGraphEvent& evn) {
    _sawAnyEvent.store(true);

    const RE::Actor* actor = nullptr;
    if (auto* holder = evn.holder) {
        actor = holder->As<RE::Actor>();
    }

    if (!actor) {
        if (_logAllAnimTags.load()) {
            spdlog::info("[FB] AnimEvt: tag='{}' actor=<null>", evn.tag.c_str());
        }
        return;
    }

    if (_logAllAnimTags.load()) {
        spdlog::info("[FB] AnimEvt: tag='{}' actor=0x{:08X}", evn.tag.c_str(), actor->formID);
    }

    if (evn.tag == "FBEvent") {
        FBEvent e{};
        e.tag = "FBEvent";
        e.actor.formID = actor->formID;
        Push(e);
        spdlog::info("[FB] AnimEvt: queued FBEvent for actor=0x{:08X}", e.actor.formID);
    } else if (evn.tag == "PairEnd") {
        FBEvent e{};
        e.tag = "PairEnd";
        e.actor.formID = actor->formID;
        Push(e);
        spdlog::info("[FB] AnimEvt: queued PairEnd for actor=0x{:08X}", e.actor.formID);
    }
}



void FBEvents::Push(const FBEvent& event) 
{
    std::lock_guard<std::mutex> lock(_mutex);
    _queue.push_back(event);
}

std::vector<FBEvent> FBEvents::Drain()
{
    std::vector<FBEvent> out;

    {
        std::lock_guard<std::mutex> lock(_mutex);
        out.swap(_queue);
    }

    return out;
}

void FBEvents::Clear()
{ 
    std::lock_guard<std::mutex> lock(_mutex);
    _queue.clear();
}

std::size_t FBEvents::Size() const 
{ 
    std::lock_guard<std::mutex> lock(_mutex);
    return _queue.size();
}