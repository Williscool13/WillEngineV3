// Port of Bend Studio's screen space shadows (bend_sss_gpu.h).
// Copyright 2023 Sony Interactive Entertainment.
// Licensed under the Apache License, Version 2.0: http://www.apache.org/licenses/LICENSE-2.0

import common_interop;
import push_constant_interop;
import bindless_declarations;

static const int WAVE_SIZE = 64;
static const int SAMPLE_COUNT = 60;
static const int HARD_SHADOW_SAMPLES = 4;
static const int FADE_OUT_SAMPLES = 8;
static const int READ_COUNT = SAMPLE_COUNT / WAVE_SIZE + 2;

// Reverse-Z
static const float NEAR_DEPTH_VALUE = 1.0;
static const float FAR_DEPTH_VALUE = 0.0;

groupshared float DepthData[READ_COUNT * WAVE_SIZE];
groupshared bool LdsEarlyOut;

// Off-screen reads return the far depth, so nothing outside the screen casts
static float ReadDepth(ContactShadowPushConstant pc, int2 texel)
{
    if (any(texel < 0) || any(texel >= int2(pc.extent))) {
        return FAR_DEPTH_VALUE;
    }
    return rdgTexturesFloat[pc.depthIndex].Load(int3(texel, 0)).r;
}

static void ComputeWavefrontExtents(ContactShadowPushConstant pc, int3 groupID, uint groupThreadID, out float2 outDeltaXY, out float2 outPixelXY, out float outPixelDistance, out bool outMajorAxisX)
{
    int2 xy = groupID.yz * WAVE_SIZE + pc.waveOffset;

    float2 light_xy = floor(pc.lightCoordinate.xy) + 0.5;
    float2 light_xy_fraction = pc.lightCoordinate.xy - light_xy;
    bool reverse_direction = pc.lightCoordinate.w > 0.0;

    int2 sign_xy = int2(sign(xy));
    bool horizontal = abs(xy.x + sign_xy.y) < abs(xy.y - sign_xy.x);

    int2 axis;
    axis.x = horizontal ? (+sign_xy.y) : 0;
    axis.y = horizontal ? 0 : (-sign_xy.x);

    xy = axis * groupID.x + xy;
    float2 xy_f = float2(xy);

    bool x_axis_major = abs(xy_f.x) > abs(xy_f.y);
    float major_axis = x_axis_major ? xy_f.x : xy_f.y;

    float major_axis_start = abs(major_axis);
    float major_axis_end = abs(major_axis) - float(WAVE_SIZE);

    float ma_light_frac = x_axis_major ? light_xy_fraction.x : light_xy_fraction.y;
    ma_light_frac = major_axis > 0 ? -ma_light_frac : ma_light_frac;

    float2 start_xy = xy_f + light_xy;

    // The innermost ring interpolates to a pixel-centered coordinate so rounding does not skip output pixels
    float2 end_xy = lerp(pc.lightCoordinate.xy, start_xy, (major_axis_end + ma_light_frac) / (major_axis_start + ma_light_frac));

    float2 xy_delta = start_xy - end_xy;

    float thread_step = float(groupThreadID ^ (reverse_direction ? 0u : uint(WAVE_SIZE - 1)));

    outPixelXY = lerp(start_xy, end_xy, thread_step / float(WAVE_SIZE));
    outPixelDistance = major_axis_start - thread_step + ma_light_frac;
    outDeltaXY = xy_delta;
    outMajorAxisX = x_axis_major;
}

[shader("compute")]
[numthreads(WAVE_SIZE, 1, 1)]
void ComputeContactShadows(uniform ContactShadowPushConstant pc, uint3 groupID : SV_GroupID, uint3 groupThreadID : SV_GroupThreadID)
{
    float2 xy_delta;
    float2 pixel_xy;
    float pixel_distance;
    bool x_axis_major;
    ComputeWavefrontExtents(pc, int3(groupID), groupThreadID.x, xy_delta, pixel_xy, pixel_distance, x_axis_major);

    float sampling_depth[READ_COUNT];
    float shadowing_depth[READ_COUNT];
    float depth_thickness_scale[READ_COUNT];
    float sample_distance[READ_COUNT];

    const float direction = -pc.lightCoordinate.w;
    const float z_sign = NEAR_DEPTH_VALUE > FAR_DEPTH_VALUE ? -1.0 : 1.0;

    bool is_edge = false;
    bool skip_pixel = false;
    float2 write_xy = floor(pixel_xy);

    [unroll]
    for (int i = 0; i < READ_COUNT; i++) {
        // Two reads per sample, interpolated on the ray's minor axis with an edge-detect fallback to point
        int2 read_xy = int2(floor(pixel_xy));
        float minor_axis = x_axis_major ? pixel_xy.y : pixel_xy.x;

        const float edge_skip = 1e20;

        float bilinear = frac(minor_axis) - 0.5;
        int bias = bilinear > 0 ? 1 : -1;
        int2 offset_xy = int2(x_axis_major ? 0 : bias, x_axis_major ? bias : 0);

        float2 depths;
        depths.x = ReadDepth(pc, read_xy);
        depths.y = ReadDepth(pc, read_xy + offset_xy);

        // Thresholds are a fraction of the depth remaining to the far plane
        depth_thickness_scale[i] = abs(FAR_DEPTH_VALUE - depths.x);

        bool use_point_filter = abs(depths.x - depths.y) > depth_thickness_scale[i] * pc.bilinearThreshold;
        if (i == 0) {
            is_edge = use_point_filter;
        }

        if (pc.bBilinearSamplingOffsetMode != 0u) {
            bilinear = use_point_filter ? 0 : bilinear;
            sampling_depth[i] = lerp(depths.x, depths.y, abs(bilinear));
            shadowing_depth[i] = (pc.bIgnoreEdgePixels != 0u && use_point_filter) ? edge_skip : sampling_depth[i];
        }
        else {
            sampling_depth[i] = depths.x;
            float edge_depth = pc.bIgnoreEdgePixels != 0u ? edge_skip : depths.x;
            // Samples may interpolate towards the neighbour, so shadow from the further of the two
            float shadow_depth = depths.x + abs(depths.x - depths.y) * z_sign;
            shadowing_depth[i] = use_point_filter ? edge_depth : shadow_depth;
        }

        sample_distance[i] = pixel_distance + (WAVE_SIZE * i) * direction;
        pixel_xy += xy_delta * direction;
    }

    if (pc.bUseEarlyOut != 0u) {
        // Sky (far) and out-of-bounds depths need no shadow
        skip_pixel = sampling_depth[0] >= 1.0 || sampling_depth[0] <= 0.0;
        bool early_out = !WaveActiveAnyTrue(!skip_pixel);
        if (WaveGetLaneCount() == WAVE_SIZE) {
            if (early_out) {
                return;
            }
        }
        else {
            // The 64-wide wavefront spans several hardware waves; they must leave together
            LdsEarlyOut = true;
            GroupMemoryBarrierWithGroupSync();
            if (!early_out) {
                LdsEarlyOut = false;
            }
            GroupMemoryBarrierWithGroupSync();
            if (LdsEarlyOut) {
                return;
            }
        }
    }

    [unroll]
    for (int i = 0; i < READ_COUNT; i++) {
        // Perspective-correct so all light rays are parallel
        float stored_depth = (shadowing_depth[i] - pc.lightCoordinate.z) / sample_distance[i];
        if (i != 0) {
            // Extended reads near the light can overshoot it
            stored_depth = sample_distance[i] > 0 ? stored_depth : 1e10;
        }
        DepthData[i * WAVE_SIZE + int(groupThreadID.x)] = stored_depth;
    }

    GroupMemoryBarrierWithGroupSync();

    if (skip_pixel) {
        return;
    }

    float start_depth = (sampling_depth[0] - pc.lightCoordinate.z) / sample_distance[0];
    int sample_index = int(groupThreadID.x) + 1;

    float4 shadow_value = 1.0;
    float hard_shadow = 1.0;

    // Window width scale: undo the projection divide, apply the user thickness (a fraction of depth remaining to far), with a minimum width near the light
    float depth_scale = min(sample_distance[0] + direction, 1.0 / pc.surfaceThickness) * sample_distance[0] / depth_thickness_scale[0];
    start_depth = start_depth * depth_scale - z_sign;

    [unroll]
    for (int i = 0; i < HARD_SHADOW_SAMPLES; i++) {
        float depth_delta = abs(start_depth - DepthData[sample_index + i] * depth_scale);
        hard_shadow = min(hard_shadow, depth_delta);
    }

    // Remaining samples average over 4 accumulators so one sample cannot fully shadow
    [unroll]
    for (int i = HARD_SHADOW_SAMPLES; i < SAMPLE_COUNT - FADE_OUT_SAMPLES; i++) {
        float depth_delta = abs(start_depth - DepthData[sample_index + i] * depth_scale);
        shadow_value[i & 3] = min(shadow_value[i & 3], depth_delta);
    }

    [unroll]
    for (int i = SAMPLE_COUNT - FADE_OUT_SAMPLES; i < SAMPLE_COUNT; i++) {
        float depth_delta = abs(start_depth - DepthData[sample_index + i] * depth_scale);
        const float fade_out = float(i + 1 - (SAMPLE_COUNT - FADE_OUT_SAMPLES)) / float(FADE_OUT_SAMPLES + 1) * 0.75;
        shadow_value[i & 3] = min(shadow_value[i & 3], depth_delta + fade_out);
    }

    shadow_value = saturate(shadow_value * pc.shadowContrast + (1.0 - pc.shadowContrast));
    hard_shadow = saturate(hard_shadow * pc.shadowContrast + (1.0 - pc.shadowContrast));

    float result = min(hard_shadow, dot(shadow_value, 0.25));
    if (pc.bDebugEdgeMask != 0u) {
        result = is_edge ? 1.0 : 0.0;
    }

    int2 out_xy = int2(write_xy);
    if (all(out_xy >= 0) && all(out_xy < int2(pc.extent))) {
        rdgStorageFloat[pc.outputIndex][out_xy] = result;
    }
}
