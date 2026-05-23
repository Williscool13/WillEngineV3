module packing_functions;

public float4 UnpackUnorm4x8(uint packed)
{
    return float4(
        float((packed >> 0) & 0xFF) / 255.0,
        float((packed >> 8) & 0xFF) / 255.0,
        float((packed >> 16) & 0xFF) / 255.0,
        float((packed >> 24) & 0xFF) / 255.0
    );
}

public uint PackUnorm4x8(float4 v)
{
    uint r = uint(clamp(v.x, 0.0, 1.0) * 255.0 + 0.5);
    uint g = uint(clamp(v.y, 0.0, 1.0) * 255.0 + 0.5);
    uint b = uint(clamp(v.z, 0.0, 1.0) * 255.0 + 0.5);
    uint a = uint(clamp(v.w, 0.0, 1.0) * 255.0 + 0.5);
    return r | (g << 8) | (b << 16) | (a << 24);
}

public float4 UnpackSnorm4x8(uint packed)
{
    return float4(
        clamp(float(int8_t((packed >> 0) & 0xFF)) / 127.0, -1.0, 1.0),
        clamp(float(int8_t((packed >> 8) & 0xFF)) / 127.0, -1.0, 1.0),
        clamp(float(int8_t((packed >> 16) & 0xFF)) / 127.0, -1.0, 1.0),
        clamp(float(int8_t((packed >> 24) & 0xFF)) / 127.0, -1.0, 1.0)
    );
}

public uint PackSnorm4x8(float4 v)
{
    uint r = uint(int8_t(clamp(v.x, -1.0, 1.0) * 127.0 + 0.5));
    uint g = uint(int8_t(clamp(v.y, -1.0, 1.0) * 127.0 + 0.5));
    uint b = uint(int8_t(clamp(v.z, -1.0, 1.0) * 127.0 + 0.5));
    uint a = uint(int8_t(clamp(v.w, -1.0, 1.0) * 127.0 + 0.5));
    return r | (g << 8) | (b << 16) | (a << 24);
}

public float2 UnpackUnorm2x16(uint packed)
{
    return float2(
        float((packed >> 0) & 0xFFFF) / 65535.0,
        float((packed >> 16) & 0xFFFF) / 65535.0
    );
}

public uint PackUnorm2x16(float2 v)
{
    uint x = uint(clamp(v.x, 0.0, 1.0) * 65535.0 + 0.5);
    uint y = uint(clamp(v.y, 0.0, 1.0) * 65535.0 + 0.5);
    return x | (y << 16);
}

public float2 UnpackSnorm2x16(uint packed)
{
    return float2(
        clamp(float(int16_t((packed >> 0) & 0xFFFF)) / 32767.0, -1.0, 1.0),
        clamp(float(int16_t((packed >> 16) & 0xFFFF)) / 32767.0, -1.0, 1.0)
    );
}

public uint PackSnorm2x16(float2 v)
{
    uint x = uint(int16_t(clamp(v.x, -1.0, 1.0) * 32767.0 + 0.5));
    uint y = uint(int16_t(clamp(v.y, -1.0, 1.0) * 32767.0 + 0.5));
    return x | (y << 16);
}

public float4 UnpackHalf2x16Pair(uint2 packed)
{
    return float4(
        f16tof32(packed.x & 0xFFFF),
        f16tof32(packed.x >> 16),
        f16tof32(packed.y & 0xFFFF),
        f16tof32(packed.y >> 16)
    );
}

public uint2 PackHalf2x16Pair(float4 v)
{
    return uint2(
        f32tof16(v.x) | (f32tof16(v.y) << 16),
        f32tof16(v.z) | (f32tof16(v.w) << 16)
    );
}

public uint2 PackNormalOctahedral(float3 n)
{
    float3 p = n / (abs(n.x) + abs(n.y) + abs(n.z));
    float2 e = p.z >= 0.0 ? p.xy : (1.0 - abs(p.yx)) * float2(p.x >= 0.0 ? 1.0 : -1.0, p.y >= 0.0 ? 1.0 : -1.0);
    e = e * 0.5 + 0.5;
    return uint2(PackUnorm2x16(e), 0u);
}

public float3 UnpackNormalOctahedral(uint packed)
{
    float2 e = UnpackUnorm2x16(packed) * 2.0 - 1.0;
    float3 n = float3(e, 1.0 - abs(e.x) - abs(e.y));
    float t = clamp(-n.z, 0.0, 1.0);
    n.x += n.x >= 0.0 ? -t : t;
    n.y += n.y >= 0.0 ? -t : t;
    return normalize(n);
}
