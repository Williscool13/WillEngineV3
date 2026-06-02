import push_constant_interop;
import bindless_declarations;
import shader_functions;

[shader("compute")]
[numthreads(8, 8, 1)]
void SVGFRemodulateMain(uniform SVGFRemodulatePushConstant pc, uint3 dtid : SV_DispatchThreadID)
{
    const uint2 pixel = dtid.xy;
    if (pixel.x >= pc.width || pixel.y >= pc.height) {
        return;
    }

    const float depth = rdgTexturesFloat[pc.depthIndex].Load(int3(pixel, 0)).r;
    if (depth == 0.0) {
        rdgStorageFloat4[pc.outputIndex][pixel] = rdgTexturesFloat4[pc.irradianceIndex].Load(int3(pixel, 0));
        return;
    }

    const uint2 gt2 = rdgTexturesUInt2[pc.gbufferTwoIndex].Load(int3(pixel, 0));
    const float3 albedo = UnpackAlbedoRGB8(gt2.r).rgb;
    const float3 emissive = UnpackEmissiveRGBE(gt2.g);

    // Re-modulate the denoised diffuse irradiance with albedo, then add the (un-demodulated) emissive.
    const float3 irradiance = rdgTexturesFloat4[pc.irradianceIndex].Load(int3(pixel, 0)).rgb;
    rdgStorageFloat4[pc.outputIndex][pixel] = float4(irradiance * albedo + emissive, 1.0);
}
