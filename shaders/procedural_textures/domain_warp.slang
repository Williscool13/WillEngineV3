import constants_interop;
import push_constant_interop;

import shader_constants;
import procedural_texture_declarations;

// frac-based hash: stable at large coordinates unlike sin-based variants
float hash(float2 p)
{
    p = frac(p * float2(0.1031f, 0.1030f));
    p += dot(p, p.yx + 33.33f);
    return frac((p.x + p.y) * p.x);
}

float valueNoise(float2 p)
{
    float2 i = floor(p);
    float2 f = frac(p);
    float2 u = f * f * (3.0f - 2.0f * f);
    float a = hash(i);
    float b = hash(i + float2(1.0f, 0.0f));
    float c = hash(i + float2(0.0f, 1.0f));
    float d = hash(i + float2(1.0f, 1.0f));
    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}

float fbm(float2 p)
{
    float v = 0.0f;
    float amp = 0.5f;
    float2x2 rot = float2x2(0.8f, 0.6f, -0.6f, 0.8f);
    for (int i = 0; i < 6; i++) {
        v += amp * valueNoise(p);
        p = mul(rot, p) * 2.0f + float2(1.7f, 9.2f);
        amp *= 0.5f;
    }
    return v;
}

float3 palette(float t)
{
    float3 a = float3(0.5f, 0.5f, 0.5f);
    float3 b = float3(0.5f, 0.5f, 0.5f);
    float3 c = float3(1.0f, 0.7f, 0.4f);
    float3 d = float3(0.0f, 0.15f, 0.20f);
    return a + b * cos(2.0f * PI * (c * t + d));
}

[numthreads(PROCEDURAL_TEXTURE_DISPATCH_X, PROCEDURAL_TEXTURE_DISPATCH_Y, 1)]
void ComputeMain(uniform ProceduralTextureBasePushConstant pc, uint3 tid : SV_DispatchThreadID)
{
    uint2 size;
    proceduralOutputTextures[pc.outputIndex].GetDimensions(size.x, size.y);
    if (tid.x >= size.x || tid.y >= size.y) { return; }

    float2 p = float2(tid.xy) / float2(size) * 3.0f;

    float2 q = float2(fbm(p), fbm(p + float2(5.2f, 1.3f)));
    float2 r = float2(fbm(p + 2.5f * q + float2(1.7f, 9.2f)), fbm(p + 2.5f * q + float2(8.3f, 2.8f)));
    float f = fbm(p + 2.5f * r);

    // smooth the length-based weights so spikes don't cause hard edges
    float wq = smoothstep(0.0f, 1.4f, length(q));
    float wr = smoothstep(0.0f, 1.0f, length(r) * length(r));

    float3 color = lerp(float3(0.1f, 0.05f, 0.2f), float3(0.9f, 0.4f, 0.1f), clamp(f * f * 4.0f, 0.0f, 1.0f));
    color = lerp(color, float3(1.0f, 0.85f, 0.3f), wq);
    color = lerp(color, float3(0.03f, 0.01f, 0.08f), wr);

    proceduralOutputTextures[pc.outputIndex][tid.xy] = float4(color, 1.0f);
}
