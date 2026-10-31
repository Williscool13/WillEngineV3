//
// Created by William on 2026-09-28.
//

#ifndef WILL_ENGINE_EDIT_CONTEXT_H
#define WILL_ENGINE_EDIT_CONTEXT_H

#include <algorithm>
#include <iterator>
#include <type_traits>

#include "engine/core/registry.h"

#include "core/containers/span.h"
#include "core/string_id.h"
#include "engine/components/component_types.h"
#include "engine/reflection/reflection.h"
#include "engine/reflection/reflection_diff.h"

namespace Engine
{
struct EngineState;

template<typename V>
void AssignValue(V& dst, const V& src)
{
    if constexpr (std::is_array_v<V>) {
        std::copy(std::begin(src), std::end(src), std::begin(dst));
    }
    else {
        dst = src;
    }
}

template<typename V>
bool ValuesEqual(const V& a, const V& b)
{
    return DeepEqual(a, b);
}

/** Writes go to every target that has the component; Primary() is the one panels display. */
class EditContext
{
public:
    EditContext(EngineContext* ctx, EngineState* state, Engine::SystemRegistry& registry, Core::Span<const entt::entity> targets);

    [[nodiscard]] entt::entity Primary() const { return targets[0]; }
    [[nodiscard]] Core::Span<const entt::entity> Targets() const { return targets; }
    [[nodiscard]] bool IsMulti() const { return targets.Size() > 1; }

    template<typename C>
    [[nodiscard]] C& Get() const { return registry.template GetMut<C>(Primary()); }

    template<typename C, typename Fn>
    void Preview(Fn&& fn)
    {
        BeginUndo(TypeSID<C>());
        for (uint32_t i = 0; i < targets.Size(); ++i) {
            const entt::entity e = targets[i];
            C* c = registry.TryGetMut<C>(e);
            if (c == nullptr) { continue; }
            if constexpr (std::is_invocable_v<Fn&, C&, uint32_t>) {
                fn(*c, i);
            }
            else {
                fn(*c);
            }
            if constexpr (HasSanitize<C>) {
                C::Sanitize(*c);
            }
            if constexpr (HasOnEditPreview<C>) {
                C::OnEditPreview(ctx, state, registry, e);
            }
        }
    }

    template<typename C>
    void Commit()
    {
        if constexpr (HasOnEditCommit<C>) {
            for (const entt::entity e : targets) {
                if (registry.Has<C>(e)) {
                    C::OnEditCommit(ctx, state, registry, e);
                }
            }
        }
        EndUndo(TypeSID<C>());
    }

    template<typename C, typename Fn>
    void Modify(Fn&& fn)
    {
        Preview<C>(fn);
        Commit<C>();
    }

    /** Writes only what differs between before and after, so other targets keep their own values elsewhere. */
    template<typename C>
    void PreviewDiff(const C& before, const C& after)
    {
        if (DeepEqual(before, after)) { return; }
        Preview<C>([&before, &after](C& c) { ApplyDiff(c, before, after); });
    }

    template<typename C, typename V>
    void PreviewSet(V C::* member, const V& value)
    {
        Preview<C>([member, &value](C& c) { AssignValue(c.*member, value); });
    }

    template<typename C, typename V>
    void Set(V C::* member, const V& value)
    {
        PreviewSet(member, value);
        Commit<C>();
    }

    /** Not undoable. */
    template<typename C, typename Fn>
    void ForEachTarget(Fn&& fn) const
    {
        for (const entt::entity e : targets) {
            if (registry.Has<C>(e)) {
                fn(e);
            }
        }
    }

    template<typename C, typename V>
    [[nodiscard]] bool IsMixed(V C::* member) const
    {
        const C* first = registry.TryGet<C>(Primary());
        if (first == nullptr) { return false; }
        for (const entt::entity e : targets) {
            const C* c = registry.TryGet<C>(e);
            if (c != nullptr && !ValuesEqual(c->*member, first->*member)) { return true; }
        }
        return false;
    }

private:
    void BeginUndo(StringID typeId);
    void EndUndo(StringID typeId);

    EngineContext* ctx{nullptr};
    EngineState* state{nullptr};
    Engine::SystemRegistry& registry;
    Core::Span<const entt::entity> targets{};
};
}

#endif //WILL_ENGINE_EDIT_CONTEXT_H
