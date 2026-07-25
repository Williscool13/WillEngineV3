import common_interop;
import constants_interop;
import push_constant_interop;
import pbr_functions;
import limits_declarations;
import bindless_declarations;

// =============== Shader Summary ============
// Editor preview sphere for a reflection probe: procedural UV-sphere (no vertex/index buffer) shaded per-pixel from the probe's
// cubemap. Specular mode samples along the mirror-ball reflection vector at the roughness-selected prefilter mip; irradiance mode
// samples along the normal at the diffuse mip. Raw radiance, no probe intensity, so the asset itself is being inspected.

static const float kPi = 3.14159265359;
static const uint kSphereStacks = 64;
static const uint kSphereSlices = 64;
// vertexCount = kSphereStacks * kSphereSlices * 6, mirrored by PROBE_PREVIEW_SPHERE_VERTEX_COUNT in debug_passes.cpp

static const uint2 kQuadCorners[6] = {
    uint2(0, 0), uint2(1, 0), uint2(1, 1),
    uint2(0, 0), uint2(1, 1), uint2(0, 1)
};

struct PreviewVertexOutput {
    float4 position : SV_Position;
    float3 normal : NORMAL0;
    float3 worldPos : TEXCOORD0;
};

[shader("vertex")]
PreviewVertexOutput VertexProbePreviewSphere(uint vid: SV_VertexID, uniform ProbePreviewSpherePushConstant pc)
{
    uint quadIndex = vid / 6;
    uint2 corner = kQuadCorners[vid % 6];
    uint stack = quadIndex / kSphereSlices;
    uint slice = quadIndex % kSphereSlices;

    float phi = (float(slice + corner.x) / float(kSphereSlices)) * 2.0 * kPi;
    float theta = (float(stack + corner.y) / float(kSphereStacks)) * kPi;
    float sinTheta = sin(theta);
    float3 normal = float3(sinTheta * cos(phi), cos(theta), sinTheta * sin(phi));

    SceneData scene = pc.sceneData[pc.sceneDataIndex];
    float3 worldPos = pc.centerRadius.xyz + normal * pc.centerRadius.w;

    PreviewVertexOutput output;
    output.position = mul(scene.viewProj, float4(worldPos, 1.0));
    output.normal = normal;
    output.worldPos = worldPos;
    return output;
}

[shader("fragment")]
float4 FragmentProbePreviewSphere(PreviewVertexOutput input, uniform ProbePreviewSpherePushConstant pc) : SV_Target0
{
    SceneData scene = pc.sceneData[pc.sceneDataIndex];
    float3 N = normalize(input.normal);
    float3 V = normalize(scene.cameraWorldPos.xyz - input.worldPos);

    float3 sampleDir;
    float mip;
    if (pc.bIrradiance != 0u) {
        sampleDir = N;
        mip = float(ENVIRONMENT_MAP_DIFFUSE_MIP);
    } else {
        sampleDir = reflect(-V, N);
        // Matches SampleReflectionProbeSpecular's roughness-to-mip mapping so the slider previews truthfully
        mip = sqrt(pc.roughness) * float(ENVIRONMENT_MAP_SPECULAR_MIPS - 1);
    }

    float3 radiance = assetCubemaps[pc.cubemapIndex].SampleLevel(assetSamplers[ASSET_SAMPLER_LINEAR_BINDLESS_INDEX], CubemapDirection(sampleDir), mip).rgb;
    return float4(radiance, 1.0);
}
