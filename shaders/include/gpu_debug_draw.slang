module gpu_debug_draw;

import model_interop;

// =============== Shader Summary ============
// Append helpers for GPU-initiated debug draw. Producers atomically reserve slots in the shared segment buffer; the build-indirect pass converts the count into mesh-task args.

static const uint2 kGPUDebugBoxEdges[12] = {
    uint2(0, 1), uint2(1, 2), uint2(2, 3), uint2(3, 0),
    uint2(4, 5), uint2(5, 6), uint2(6, 7), uint2(7, 4),
    uint2(0, 4), uint2(1, 5), uint2(2, 6), uint2(3, 7)
};

/** Reserves count segment slots; returns the first index, or 0xFFFFFFFF when the buffer is full. */
public uint GPUDebugAllocSegments(GPUDebugDrawArgs* args, uint count)
{
    uint first;
    InterlockedAdd(args[0].segmentCount, count, first);
    if (first + count > args[0].capacity) {
        return 0xFFFFFFFF;
    }
    return first;
}

public void GPUDebugAddLine(GPUDebugDrawArgs* args, DebugLineSegment* segments, float3 a, float3 b, float width, float4 color)
{
    uint first = GPUDebugAllocSegments(args, 1);
    if (first == 0xFFFFFFFF) {
        return;
    }
    DebugLineSegment seg;
    seg.a = a;
    seg.width = width;
    seg.b = b;
    seg.pad = 0.0;
    seg.color = color;
    segments[first] = seg;
}

void GPUDebugWriteBoxSegments(DebugLineSegment* segments, uint first, float3 corners[8], float width, float4 color)
{
    for (uint i = 0; i < 12; ++i) {
        DebugLineSegment seg;
        seg.a = corners[kGPUDebugBoxEdges[i].x];
        seg.width = width;
        seg.b = corners[kGPUDebugBoxEdges[i].y];
        seg.pad = 0.0;
        seg.color = color;
        segments[first + i] = seg;
    }
}

public void GPUDebugAddAABB(GPUDebugDrawArgs* args, DebugLineSegment* segments, float3 boxMin, float3 boxMax, float width, float4 color)
{
    uint first = GPUDebugAllocSegments(args, 12);
    if (first == 0xFFFFFFFF) {
        return;
    }
    float3 corners[8] = {
        float3(boxMin.x, boxMin.y, boxMin.z),
        float3(boxMax.x, boxMin.y, boxMin.z),
        float3(boxMax.x, boxMax.y, boxMin.z),
        float3(boxMin.x, boxMax.y, boxMin.z),
        float3(boxMin.x, boxMin.y, boxMax.z),
        float3(boxMax.x, boxMin.y, boxMax.z),
        float3(boxMax.x, boxMax.y, boxMax.z),
        float3(boxMin.x, boxMax.y, boxMax.z)
    };
    GPUDebugWriteBoxSegments(segments, first, corners, width, color);
}

/** SH payload is L1 per channel, pre-scaled: shaded color = sh.x + dot(sh.yzw, normal). */
public void GPUDebugAddSphere(GPUDebugSphereArgs* sphereArgs, DebugSphereInstance* instances, float3 center, float radius, float4 shR, float4 shG, float4 shB)
{
    uint slot;
    InterlockedAdd(sphereArgs[0].instanceCount, 1, slot);
    if (slot >= sphereArgs[0].capacity) {
        return;
    }
    DebugSphereInstance inst;
    inst.center = center;
    inst.radius = radius;
    inst.shR = shR;
    inst.shG = shG;
    inst.shB = shB;
    instances[slot] = inst;
}

public void GPUDebugAddSphereFlat(GPUDebugSphereArgs* sphereArgs, DebugSphereInstance* instances, float3 center, float radius, float3 color)
{
    GPUDebugAddSphere(sphereArgs, instances, center, radius, float4(color.r, 0.0, 0.0, 0.0), float4(color.g, 0.0, 0.0, 0.0), float4(color.b, 0.0, 0.0, 0.0));
}

/** Axes are half-extent-scaled: center +- ax +- ay +- az spans the box. */
public void GPUDebugAddOBB(GPUDebugDrawArgs* args, DebugLineSegment* segments, float3 center, float3 ax, float3 ay, float3 az, float width, float4 color)
{
    uint first = GPUDebugAllocSegments(args, 12);
    if (first == 0xFFFFFFFF) {
        return;
    }
    float3 corners[8] = {
        center - ax - ay - az,
        center + ax - ay - az,
        center + ax + ay - az,
        center - ax + ay - az,
        center - ax - ay + az,
        center + ax - ay + az,
        center + ax + ay + az,
        center - ax + ay + az
    };
    GPUDebugWriteBoxSegments(segments, first, corners, width, color);
}
