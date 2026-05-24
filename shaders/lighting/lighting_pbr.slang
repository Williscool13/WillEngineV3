import push_constant_interop;
import constants_interop;
import model_interop;
import lights_interop;

import shader_functions;
import pbr_functions;
import packing_functions;
import visibility_lighting_common;

import bindless_declarations;
import restir_common;

[numthreads(16, 16, 1)]
void ComputeMain(uniform LightingResolvePushConstant pc, uint3 threadID : SV_DispatchThreadID) {
    LightingDispatchParameters lightParams = pc.lightDispatchBuffer[pc.lightingIndex];
    uint2 pixel = threadID.xy + uint2(lightParams.minX, lightParams.minY);
    if (any(pixel >= uint2(lightParams.maxX, lightParams.maxY))) {
        return;
    }

    float depth = rdgTexturesFloat[pc.depthIndex].Load(int3(pixel, 0)).r;
    if (depth <= 0.0) {
        return;
    }

    LightingInputs li = ReconstructLightingInputs(pixel, pc.visibilityBufferIndex, pc.lightingIndex, pc.instanceBuffer, pc.gbufferOneIndex, pc.gbufferTwoIndex, pc.shadowsIndex);
    if (!li.valid) {
        return;
    }

    float2 uv = (float2(pixel) + 0.5) / float2(pc.sceneData[pc.sceneDataIndex].mainRenderTargetSize);
    float viewSpaceDepth = ScreenToViewSpaceDepth(depth, pc.sceneData[pc.sceneDataIndex].depthLinearizeMult, pc.sceneData[pc.sceneDataIndex].depthLinearizeAdd);
    float3 viewPosition = CheapReconstructViewSpacePosition(pc.sceneData[pc.sceneDataIndex].ndcToViewMul, pc.sceneData[pc.sceneDataIndex].ndcToViewAdd, uv, viewSpaceDepth);

    float3 N = normalize(li.viewNormal);
    float3 V = normalize(-viewPosition);

    // Clamp roughness so D_GGX never produces 0/0 at NdotH=1 (degenerate delta lobe)
    float roughness = max(li.roughness, 0.045);

    float3 F0 = lerp(float3(0.04, 0.04, 0.04), li.albedo.xyz, li.metallic);
    float nDotV = max(dot(N, V), 0.0);

    float gtao = li.shadows.y;

    float3 directLight = float3(0.0, 0.0, 0.0);

    // Basic ReSTIR DI
    {
        const int M = 2;
        int lightCount = pc.lightData->areaLightCount;
        if (lightCount > 0) {
            uint h = pixel.x * 1973u ^ pixel.y * 9277u;

            Reservoir r = ReservoirEmpty();
            ReservoirSample winner = { -1, float3(0.0, 0.0, 0.0), 0.0 };

            for (int i = 0; i < M; i++) {
                h = h ^ (h >> 17u); h *= 0xbf324c81u; h = h ^ (h >> 13u); h *= 0x9c7d2d49u; h = h ^ (h >> 16u);
                float uLight = float(h & 0xFFFFu) / 65536.0;
                h = h ^ (h >> 17u); h *= 0xbf324c81u; h = h ^ (h >> 13u); h *= 0x9c7d2d49u; h = h ^ (h >> 16u);
                float uU = float(h & 0xFFFFu) / 65536.0;
                h = h ^ (h >> 17u); h *= 0xbf324c81u; h = h ^ (h >> 13u); h *= 0x9c7d2d49u; h = h ^ (h >> 16u);
                float uV = float(h & 0xFFFFu) / 65536.0;
                h = h ^ (h >> 17u); h *= 0xbf324c81u; h = h ^ (h >> 13u); h *= 0x9c7d2d49u; h = h ^ (h >> 16u);
                float uSelect = float(h & 0xFFFFu) / 65536.0;

                int lightIdx = clamp(int(uLight * float(lightCount)), 0, lightCount - 1);
                AreaLightData al = pc.lightData->areaLights[lightIdx];

                float3 alCenter = mul(pc.sceneData[pc.sceneDataIndex].view, float4(al.position.xyz, 1.0)).xyz;
                float3 alNormal = normalize(mul((float3x3)pc.sceneData[pc.sceneDataIndex].view, al.normal.xyz));
                float3 alRight = normalize(mul((float3x3)pc.sceneData[pc.sceneDataIndex].view, al.right.xyz));
                float3 alUp = normalize(mul((float3x3)pc.sceneData[pc.sceneDataIndex].view, al.up.xyz));
                float halfWidth = al.right.w;
                float halfHeight = al.up.w;
                float area = (2.0 * halfWidth) * (2.0 * halfHeight);

                float3 samplePos = alCenter + (uU * 2.0 - 1.0) * halfWidth * alRight + (uV * 2.0 - 1.0) * halfHeight * alUp;

                float centerDist = length(alCenter - viewPosition);
                float attenuation = 1.0 - smoothstep(0.0, al.range, centerDist);
                attenuation *= attenuation;

                float3 toSample = samplePos - viewPosition;
                float dist = length(toSample);
                float3 Lal = toSample / dist;
                float cosShade = max(dot(N, Lal), 0.0);
                float cosLight = max(dot(-alNormal, Lal), 0.0);

                float pHat = 0.0;
                if (cosShade > 0.0 && cosLight > 0.0 && attenuation > 0.0)
                {
                    float3 Hal = normalize(V + Lal);
                    float3 Fal = F_SCHLICK(V, Hal, F0);
                    float3 kDal = (1.0 - Fal) * (1.0 - li.metallic);
                    float3 alColor = UnpackUnorm4x8(al.packedColor).rgb;
                    float3 brdfCos = (kDal * li.albedo.xyz + Fal) * cosShade * al.intensity * alColor * attenuation;
                    pHat = dot(brdfCos, float3(0.2126, 0.7152, 0.0722));
                }

                float risWeight = (1.0 / float(M)) * pHat * float(lightCount) * area;
                if (ReservoirAddSample(r, i, risWeight, uSelect))
                {
                    winner = { lightIdx, samplePos, attenuation };
                }
            }

            if (r.y >= 0 && winner.lightIdx >= 0)
            {
                AreaLightData al = pc.lightData->areaLights[winner.lightIdx];
                float3 alCenter = mul(pc.sceneData[pc.sceneDataIndex].view, float4(al.position.xyz, 1.0)).xyz;
                float3 alNormal = normalize(mul((float3x3)pc.sceneData[pc.sceneDataIndex].view, al.normal.xyz));

                float3 toSample = winner.samplePos - viewPosition;
                float dist2 = dot(toSample, toSample);
                float dist = sqrt(dist2);
                float3 Lal = toSample / dist;
                float cosShade = max(dot(N, Lal), 0.0);
                float cosLight = max(dot(-alNormal, Lal), 0.0);

                if (cosShade > 0.0 && cosLight > 0.0)
                {
                    float3 Hal = normalize(V + Lal);
                    float NDFal = D_GGX(N, Hal, roughness);
                    float Gal   = G_SMITH_HEIGHT_CORRELATED(N, V, Lal, roughness);
                    float3 Fal  = F_SCHLICK(V, Hal, F0);
                    float3 specularAl = (NDFal * Gal * Fal) / max(4.0 * nDotV * cosShade, 0.001);
                    float3 kDal = (1.0 - Fal) * (1.0 - li.metallic);
                    float orenNayarAl = Diffuse_OrenNayar(cosShade, nDotV, Lal, V, roughness);
                    float3 diffuseAl = kDal * li.albedo.xyz * orenNayarAl;
                    float3 alColor = UnpackUnorm4x8(al.packedColor).rgb;

                    float3 brdfCos = (kDal * li.albedo.xyz + Fal) * cosShade * al.intensity * alColor * winner.attenuation;
                    float pHatWinner = dot(brdfCos, float3(0.2126, 0.7152, 0.0722));
                    ReservoirFinalise(r, pHatWinner);

                    float geometryTerm = cosLight / dist2;
                    directLight = (diffuseAl + specularAl) * geometryTerm * al.intensity * alColor * winner.attenuation * r.W;
                }
            }
        }
    }

    float3 finalColor = directLight;
    rdgStorageFloat4[pc.outputImageIndex][pixel] = float4(finalColor, 1.0);
}
