//
// Created by William on 2026-07-03.
//

#ifndef WILL_ENGINE_PHYSICS_COLLIDER_LOAD_SLOT_H
#define WILL_ENGINE_PHYSICS_COLLIDER_LOAD_SLOT_H

#include <TaskScheduler.h>

#include "asset-load/asset_load_types.h"
#include "core/containers/inline_function.h"

namespace Core
{
class MemoryManager;
}

namespace Engine
{
struct PhysicsColliderAsset;
}

namespace AssetLoad
{
class PhysicsColliderLoadSlot
{
public:
    PhysicsColliderLoadSlot();

    ~PhysicsColliderLoadSlot();

    void Initialize(enki::TaskScheduler* _scheduler, Core::MemoryManager* _memoryManager,
                    Core::InlineFunction<void(bool, PhysicsColliderSlotHandle)> _notifyCallback);

    void Launch(PhysicsColliderSlotHandle _slotHandle, Engine::PhysicsColliderAsset* _collider);

    void Clear();

    Engine::PhysicsColliderAsset* collider{nullptr};

private:
    struct BuildColliderTask : enki::ITaskSet
    {
        PhysicsColliderLoadSlot* loadSlot{nullptr};

        explicit BuildColliderTask() : ITaskSet(1)
        {
            m_Priority = enki::TASK_PRIORITY_LOW;
        }

        void ExecuteRange(enki::TaskSetPartition range, uint32_t threadNum) override;
    };

    bool Build();

    PhysicsColliderSlotHandle slotHandle{};

    BuildColliderTask task{};
    enki::TaskScheduler* scheduler{nullptr};
    Core::MemoryManager* memoryManager{nullptr};
    Core::InlineFunction<void(bool, PhysicsColliderSlotHandle)> notifyCallback;
};
} // AssetLoad

#endif //WILL_ENGINE_PHYSICS_COLLIDER_LOAD_SLOT_H
