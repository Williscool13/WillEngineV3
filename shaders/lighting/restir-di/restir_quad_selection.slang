// Precomputes the half-res 2x2 quad-corner selection (sel in [0,3]) once per frame; consumers read it via FetchFullPixel.
import common_interop;
import push_constant_interop;
import constants_interop;
import bindless_declarations;
import shader_functions;
import shader_constants;
import restir_common;

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeMain(uniform QuadSelectionPushConstant pc, uint3 dtid : SV_DispatchThreadID)
{
    uint2 pixel = dtid.xy;
    if (any(pixel >= pc.renderExtent)) {
        return;
    }

    SceneData scene = pc.sceneData[pc.sceneDataIndex];
    float2 invFullExtent = 1.0 / float2(scene.mainRenderTargetSize);
    uint2 base = pixel * pc.pixelScale;
    float2 uv = (float2(base) + 1.0) * invFullExtent;
    float4 z = rdgTexturesFloat[pc.depthIndex].GatherRed(rdgSamplers[RDG_POINT_SAMPLER_INDEX], uv);
    float4 zl = scene.depthLinearizeMult / (scene.depthLinearizeAdd - z);
    float zMin = min(min(zl.x, zl.y), min(zl.z, zl.w));
    float zMax = max(max(zl.x, zl.y), max(zl.z, zl.w));
    bool quadUniform = (zMax - zMin) <= QUAD_UNIFORM_DEPTH_THRESHOLD * max(zMax, 1e-4);
    if (quadUniform) {
        uint4 nOct = rdgTexturesUInt4[pc.normalIndex].GatherRed(rdgSamplers[RDG_POINT_SAMPLER_INDEX], uv);
        float3 n0 = UnpackNormalOct16(nOct.x);
        float minDot = min(min(dot(n0, UnpackNormalOct16(nOct.y)), dot(n0, UnpackNormalOct16(nOct.z))), dot(n0, UnpackNormalOct16(nOct.w)));
        quadUniform = minDot >= QUAD_UNIFORM_NORMAL_DOT_THRESHOLD;
    }
    uint sel;
    if (quadUniform) {
        // Interchangeable corners: rotate per frame so the temporal denoiser amortizes full-res coverage.
        sel = (pixel.x + pixel.y * 2u + pc.frameIndex) & 3u;
    } else {
        // Silhouette/crease: lock to the nearest-depth corner so the half-res sample stays on one coherent (foreground) surface.
        // Frame-stable so edges is at least a clean staircase instead of jagged/inconsistent
        sel = 3u;
        float best = zl.w;
        if (zl.z < best) { best = zl.z; sel = 2u; }
        if (zl.x < best) { best = zl.x; sel = 1u; }
        if (zl.y < best) { best = zl.y; sel = 0u; }
    }
    rdgStorageUInt[pc.outIndex][pixel] = sel;
}
