import common_interop;
import model_interop;
import flags_interop;
import instance_mask_interop;
import push_constant_interop;

// =============== Shader Summary ============
// Writes one VkAccelerationStructureInstanceKHR per instance slot, replacing the CPU loop that used to fill the TLAS instance buffer.
// Dead slots get a null acceleration structure reference, which the build treats as an inactive instance, so no compaction is needed.

[shader("compute")]
[numthreads(64, 1, 1)]
void ComputeTLASInstances(uniform TLASInstancePushConstant pc, uint3 dtid : SV_DispatchThreadID)
{
    uint slot = dtid.x;
    if (slot >= pc.instanceCount) { return; }

    Instance inst = pc.instanceBuffer[slot];

    AccelerationStructureInstance entry;
    entry.transformRow0 = float4(0.0);
    entry.transformRow1 = float4(0.0);
    entry.transformRow2 = float4(0.0);
    entry.instanceCustomIndexAndMask = 0;
    entry.sbtOffsetAndFlags = 0;
    entry.accelerationStructureReference = 0;

    if (inst.blasDeviceAddress != 0) {
        float4x4 m = pc.modelBuffer[inst.modelIndex].modelMatrix;
        entry.transformRow0 = m[0];
        entry.transformRow1 = m[1];
        entry.transformRow2 = m[2];

        bool bLightProxy = inst.lightIndex != 0xFFFFFFFFu;
        uint mask = bLightProxy ? INSTANCE_MASK_LIGHT_PROXY : (INSTANCE_MASK_OCCLUDER | INSTANCE_MASK_SUN_OCCLUDER);
        if (!bLightProxy && (inst.flags & INSTANCE_FLAG_DDGI_VISIBLE) != 0) { mask |= INSTANCE_MASK_DDGI; }

        MaterialProperties material = pc.materialBuffer[inst.materialIndex];
        bool bCutout = (inst.flags & INSTANCE_FLAG_ALPHA_CUTOUT) != 0 && uint(material.alphaProperties.y) == MATERIAL_ALPHA_CUTOUT;
        uint flags = GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE | (bCutout ? GEOMETRY_INSTANCE_FORCE_NO_OPAQUE : 0u);

        entry.instanceCustomIndexAndMask = (slot & 0x00FFFFFFu) | ((mask & 0xFFu) << 24);
        entry.sbtOffsetAndFlags = (flags & 0xFFu) << 24;
        entry.accelerationStructureReference = inst.blasDeviceAddress;
    }

    pc.outInstances[slot] = entry;
}
