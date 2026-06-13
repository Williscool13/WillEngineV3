/*
 * RELAX: AntiFirefly pass. Cross-bilateral RCRS filter to suppress bright outliers.
 * Adapted from NVIDIA Real-Time Denoisers (NRD). Copyright (c) 2022-2024 NVIDIA Corporation. All rights reserved.
 * https://github.com/NVIDIA-RTX/NRD -- NVIDIA RTX SDKs LICENSE
 */

import push_constant_interop;
import bindless_declarations;
import constants_interop;
import relax_interop;
import relax_utils;

static const int AF_BORDER = 1;
static const int AF_GROUP_X = 8;
static const int AF_GROUP_Y = 8;
static const int AF_BUFFER_X = AF_GROUP_X + AF_BORDER * 2;
static const int AF_BUFFER_Y = AF_GROUP_Y + AF_BORDER * 2;

groupshared float4 s_Spec[AF_BUFFER_Y][AF_BUFFER_X];
groupshared float4 s_Diff[AF_BUFFER_Y][AF_BUFFER_X];
groupshared float s_MaterialID[AF_BUFFER_Y][AF_BUFFER_X];

void Preload(uint2 sharedPos, int2 globalPos, RelaxDiffuseSpecularConstants* rc,
    uint normalRoughnessIndex, uint specIndex, uint diffIndex)
{
    globalPos = clamp(globalPos, int2(0, 0), rc.gRectSize - 1);
    s_MaterialID[sharedPos.y][sharedPos.x] = 0.0; // material ID not used; always match
    s_Spec[sharedPos.y][sharedPos.x] = rdgTexturesFloat4[specIndex].Load(int3(globalPos, 0));
    s_Diff[sharedPos.y][sharedPos.x] = rdgTexturesFloat4[diffIndex].Load(int3(globalPos, 0));
}

[shader("compute")]
[numthreads(AF_GROUP_X, AF_GROUP_Y, 1)]
void RelaxAntiFireflyMain(uniform RelaxAntiFireflyPushConstant pc, int2 threadPos : SV_GroupThreadID, uint2 groupPos : SV_GroupID, int2 pixelPos : SV_DispatchThreadID, uint threadIndex : SV_GroupIndex)
{
    RelaxDiffuseSpecularConstants* rc = pc.constants;

    // Preload into shared memory
    float isSky = rdgTexturesFloat[pc.tilesIndex].Load(int3(pixelPos >> 4, 0)).r;
    if (isSky == 0.0)
    {
        int2 groupBase = pixelPos - threadPos - AF_BORDER;
        uint stageNum = (AF_BUFFER_X * AF_BUFFER_Y + AF_GROUP_X * AF_GROUP_Y - 1) / (AF_GROUP_X * AF_GROUP_Y);
        for (uint stage = 0; stage < stageNum; stage++)
        {
            uint virtualIndex = threadIndex + stage * AF_GROUP_X * AF_GROUP_Y;
            uint2 newId = uint2(virtualIndex % AF_BUFFER_X, virtualIndex / AF_BUFFER_X);
            if (stage == 0 || virtualIndex < AF_BUFFER_X * AF_BUFFER_Y)
                Preload(newId, groupBase + int2(newId), rc, pc.normalRoughnessIndex, pc.specIndex, pc.diffIndex);
        }
    }
    GroupMemoryBarrierWithGroupSync();

    if (isSky != 0.0 || pixelPos.x >= rc.gRectSize.x || pixelPos.y >= rc.gRectSize.y)
        return;

    int2 fullPixel = RelaxQuadFullPixel(pixelPos, pc.pixelScale, rc.gFrameIndex, pc.viewZIndex, rc.gRectSizeInv / float(pc.pixelScale), rc.depthLinearizeMult, rc.depthLinearizeAdd);
    float centerViewZ = RelaxLoadViewZ(pc.viewZIndex, fullPixel, rc.depthLinearizeMult, rc.depthLinearizeAdd);
    if (!RelaxIsInDenoisingRange(centerViewZ, rc.gDenoisingRange))
        return;

    uint2 sharedCenter = uint2(threadPos) + AF_BORDER;

    float4 specCenter = s_Spec[sharedCenter.y][sharedCenter.x];
    float4 diffCenter = s_Diff[sharedCenter.y][sharedCenter.x];
    float specLumCenter = MLLuminance(specCenter.rgb);
    float diffLumCenter = MLLuminance(diffCenter.rgb);

    float maxSpecLum = -1.0, minSpecLum = 1e6;
    float maxDiffLum = -1.0, minDiffLum = 1e6;
    uint2 maxSpecCoords = sharedCenter, minSpecCoords = sharedCenter;
    uint2 maxDiffCoords = sharedCenter, minDiffCoords = sharedCenter;

    [unroll]
    for (int yy = -1; yy <= 1; yy++)
    {
        [unroll]
        for (int xx = -1; xx <= 1; xx++)
        {
            if (xx == 0 && yy == 0) continue;
            int2 p = pixelPos + int2(xx, yy);
            if (any(p < int2(0, 0)) || any(p >= rc.gRectSize)) continue;

            uint2 si = uint2(threadPos) + AF_BORDER + uint2(xx, yy);

            float specLum = MLLuminance(s_Spec[si.y][si.x].rgb);
            float diffLum = MLLuminance(s_Diff[si.y][si.x].rgb);

            if (specLum > maxSpecLum) { maxSpecLum = specLum; maxSpecCoords = si; }
            if (specLum < minSpecLum) { minSpecLum = specLum; minSpecCoords = si; }
            if (diffLum > maxDiffLum) { maxDiffLum = diffLum; maxDiffCoords = si; }
            if (diffLum < minDiffLum) { minDiffLum = diffLum; minDiffCoords = si; }
        }
    }

    uint2 specCoords = sharedCenter;
    if (specLumCenter > maxSpecLum) specCoords = maxSpecCoords;
    if (specLumCenter < minSpecLum) specCoords = minSpecCoords;

    uint2 diffCoords = sharedCenter;
    if (diffLumCenter > maxDiffLum) diffCoords = maxDiffCoords;
    if (diffLumCenter < minDiffLum) diffCoords = minDiffCoords;

    rdgStorageFloat4[pc.outSpecIndex][pixelPos] = float4(s_Spec[specCoords.y][specCoords.x].rgb, specCenter.a);
    rdgStorageFloat4[pc.outDiffIndex][pixelPos] = float4(s_Diff[diffCoords.y][diffCoords.x].rgb, diffCenter.a);
}
