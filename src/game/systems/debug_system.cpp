//
// Created by William on 2025-12-22.
//

#include "debug_system.h"

#include "core/include/engine_context.h"
#include "core/input/input_frame.h"
#include "engine/asset_manager.h"
#include "engine/engine_api.h"
#include "platform/paths.h"

namespace Game::System
{
//static Engine::WillModelHandle boxHandle = Engine::WillModelHandle::INVALID;

void DebugUpdate(Core::EngineContext* ctx, Engine::GameState* state)
{
    static Engine::WillModelHandle boxHandle = Engine::WillModelHandle::INVALID;
    if (state->inputFrame->GetKey(Key::F1).pressed) {
        if (!boxHandle.IsValid()) {
            boxHandle = ctx->assetManager->LoadModel(Platform::GetAssetPath() / "BoxTextured.willmodel");
        }
    }
    if (state->inputFrame->GetKey(Key::F2).pressed) {
        if (boxHandle.IsValid()) {
            ctx->assetManager->UnloadModel(boxHandle);
        }
    }
}

void DebugPrepareFrame(Core::EngineContext* ctx, Engine::GameState* state, Core::FrameBuffer* frameBuffer)
{
    /*if (state->inputFrame->GetKey(Key::F2).pressed) {
        if (boxHandle.IsValid()) {
            Render::WillModel* model = ctx->assetManager->GetModel(boxHandle);

            Render::ResourceManager* resourceManager = renderThread->GetResourceManager();
            Game::ModelInstance mi{};

            Render::WillModel* model = resourceManager->models.Get(boxModelHandle);
            if (model) {
                mi.transform = Transform::IDENTITY;
                mi.nodes.reserve(model->modelData.nodes.size());
                mi.nodeRemap = model->modelData.nodeRemap;

                // todo: skinned mesh support
                // size_t jointMatrixCount = model->modelData.inverseBindMatrices.size();
                // bool bHasSkinning = jointMatrixCount > 0;
                // if (bHasSkinning) {
                //     rm.jointMatrixAllocation = resourceManager->jointMatrixAllocator.allocate(jointMatrixCount * sizeof(Renderer::Model));
                //     rm.jointMatrixOffset = rm.jointMatrixAllocation.offset / sizeof(uint32_t);
                // }

                mi.modelEntryHandle = boxModelHandle;
                for (const Render::Node& n : model->modelData.nodes) {
                    mi.nodes.emplace_back(n);
                    Game::NodeInstance& rn = mi.nodes.back();
                    // if (n.inverseBindIndex != ~0u) {
                    //     rn.inverseBindMatrix = model->modelData.inverseBindMatrices[n.inverseBindIndex];
                    // }
                }

                for (Game::NodeInstance& node : mi.nodes) {
                    if (node.meshIndex != ~0u) {
                        node.modelMatrixHandle = resourceManager->modelEntryAllocator.Add();

                        for (const Render::PrimitiveProperty& primitiveProperty : model->modelData.meshes[node.meshIndex].primitiveIndices) {
                            Render::InstanceEntryHandle instanceEntry = resourceManager->instanceEntryAllocator.Add();
                            node.instanceEntryHandles.push_back(instanceEntry);

                            Core::InstanceOperation instanceOp{
                                .index = instanceEntry.index,
                                .modelIndex = node.modelMatrixHandle.index,
                                .primitiveIndex = primitiveProperty.index,
                                .materialIndex = primitiveProperty.materialIndex, // todo: should also generate material to use here
                                .jointMatrixOffset = 0,
                                .bIsAllocated = 1,
                            };
                            stagingFrameBuffer.instanceOperations.push_back(instanceOp);
                        }
                    }
                }

                // Update entire transform hierarchy
                {
                    glm::mat4 baseTopLevel = mi.transform.GetMatrix();
                    // Nodes are sorted
                    for (Game::NodeInstance& ni : mi.nodes) {
                        glm::mat4 localTransform = ni.transform.GetMatrix();

                        if (ni.parent == ~0u) {
                            ni.cachedWorldTransform = baseTopLevel * localTransform;
                        }
                        else {
                            ni.cachedWorldTransform = mi.nodes[ni.parent].cachedWorldTransform * localTransform;
                        }
                    }
                    mi.bNeedToSendToRender = true;
                }

                // Model matrix send to gpu
                {
                    if (mi.bNeedToSendToRender) {
                        for (Game::NodeInstance& node : mi.nodes) {
                            if (node.meshIndex != ~0u) {
                                stagingFrameBuffer.modelMatrixOperations.push_back({node.modelMatrixHandle.index, Transform::IDENTITY.GetMatrix()}); //node.cachedWorldTransform});
                            }

                            // if (node.jointMatrixIndex != ~0u) {
                            //     glm::mat4 jointMatrix = node.cachedWorldTransform * node.inverseBindMatrix;
                            //     uint32_t jointMatrixFinalIndex = node.jointMatrixIndex + runtimeMesh.jointMatrixOffset;
                            //     jointMatrixOperations.push_back({jointMatrixFinalIndex, jointMatrix});
                            // }
                        }

                        mi.bNeedToSendToRender = false;
                    }
                }


                bHasAdded = true;
            }
        }
    }*/
}
} // Game::System
