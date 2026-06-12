/*
 * RELAX: GenerateViewZ pass. Downsamples full-res raw depth to half-res linearized viewZ.
 * Run once per frame before temporal accumulation so that relax_viewz_history is always
 * a half-res texture that matches gResourceSizeInvPrev.
 */

import push_constant_interop;
import bindless_declarations;
import relax_interop;
import relax_utils;
import shader_functions;

[shader("compute")]
[numthreads(8, 8, 1)]
void RelaxGenerateViewZMain(uniform RelaxGenerateViewZPushConstant pc, uint3 dtid : SV_DispatchThreadID)
{
    RelaxDiffuseSpecularConstants* rc = pc.constants;
    uint2 pixelPos = dtid.xy;
    if (pixelPos.x >= uint(rc.gRectSize.x) || pixelPos.y >= uint(rc.gRectSize.y)) {
        return;
    }

    int2 fullPixel = RelaxQuadFullPixel(int2(pixelPos), pc.pixelScale, rc.gFrameIndex);
    float raw = rdgTexturesFloat[pc.viewZIndex].Load(int3(fullPixel, 0)).r;
    float viewZ = ScreenToViewSpaceDepth(raw, rc.depthLinearizeMult, rc.depthLinearizeAdd);
    rdgStorageFloat[pc.outViewZIndex][pixelPos] = viewZ;
}
