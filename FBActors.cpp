#include "FBActors.h"
#include "FBTransform.h"


namespace {

    static RE::Actor* ResolveNearestOtherActor(RE::Actor* caster, float maxDist) {
        if (!caster) {
            return nullptr;
        }

        const auto casterPos = caster->GetPosition();
        RE::Actor* best = nullptr;
        float bestDist2 = maxDist * maxDist;

        auto* lists = RE::ProcessLists::GetSingleton();
        if (!lists) {
            return nullptr;
        }

        // highActorHandles is the usual way to iterate loaded actors
        for (auto& h : lists->highActorHandles) {
            auto aPtr = h.get();        // NiPointer<Actor>
            RE::Actor* a = aPtr.get();  // raw Actor*
            if (!a || a == caster) {
                continue;
            }
            if (!a->Is3DLoaded()) {
                continue;
            }

            const auto p = a->GetPosition();
            const float dx = p.x - casterPos.x;
            const float dy = p.y - casterPos.y;
            const float dz = p.z - casterPos.z;
            const float d2 = dx * dx + dy * dy + dz * dz;

            if (d2 < bestDist2) {
                bestDist2 = d2;
                best = a;
            }
        }

        return best;
    }
}

namespace FB::Actors {
    RE::Actor* ResolveActorForEvent(const FBEvent& e, ActorRole role) {
        if (!e.actor.IsValid()) {
            return nullptr;
        }

        auto* caster = RE::TESForm::LookupByID(e.actor.formID)->As<RE::Actor>();
        if (!caster) {
            return nullptr;
        }

        if (role == ActorRole::Caster) {
            return caster;
        }

        constexpr float kMaxTargetDist = 250.0f;
        return ResolveNearestOtherActor(caster, kMaxTargetDist);
    }

}


