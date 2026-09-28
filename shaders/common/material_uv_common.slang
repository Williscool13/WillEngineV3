module material_uv_common;

import model_interop;

public float3 ModelScale(float4x4 modelMatrix)
{
    float3x3 m = (float3x3)modelMatrix;
    return float3(length(float3(m[0][0], m[1][0], m[2][0])), length(float3(m[0][1], m[1][1], m[2][1])), length(float3(m[0][2], m[1][2], m[2][2])));
}

/** Object-space position in metres: rotation and translation undone, entity scale kept. Assumes no shear. */
public float3 MetricObjectPosition(float4x4 modelMatrix, float3 worldPosition)
{
    float3x3 m = (float3x3)modelMatrix;
    float3 rel = worldPosition - float3(modelMatrix[0][3], modelMatrix[1][3], modelMatrix[2][3]);
    return float3(dot(normalize(float3(m[0][0], m[1][0], m[2][0])), rel), dot(normalize(float3(m[0][1], m[1][1], m[2][1])), rel), dot(normalize(float3(m[0][2], m[1][2], m[2][2])), rel));
}

public uint TriplanarDominantAxis(float3 normalOS)
{
    float3 a = abs(normalOS);
    return a.x >= a.y && a.x >= a.z ? 0u : (a.y >= a.z ? 1u : 2u);
}

/** Linear, so it also maps position derivatives. Matches the procedural box's per-face UV directions (not its offsets). */
public float2 TriplanarAxis(float3 p, float3 normalOS, uint axis)
{
    if (axis == 0u) {
        return float2(normalOS.x > 0.0 ? -p.z : p.z, p.y);
    }
    if (axis == 1u) {
        return float2(normalOS.y > 0.0 ? -p.x : p.x, p.z);
    }
    return float2(normalOS.z > 0.0 ? p.x : -p.x, p.y);
}

public static const float TRIPLANAR_SHARPNESS = 16.0;
public static const float TRIPLANAR_MIN_WEIGHT = 0.02;

/** Per-axis blend weights summing to 1. Sharp: flat faces get one axis, only near-45-degree normals blend. */
public float3 TriplanarWeights(float3 normalOS)
{
    float3 w = pow(abs(normalOS), TRIPLANAR_SHARPNESS);
    w /= w.x + w.y + w.z;
    w *= step(TRIPLANAR_MIN_WEIGHT, w);
    return w / (w.x + w.y + w.z);
}

/** Object-space directions of +U and +V for TriplanarAxis. */
public void TriplanarAxisFrame(float3 normalOS, uint axis, out float3 tangentOS, out float3 bitangentOS)
{
    if (axis == 0u) {
        tangentOS = float3(0.0, 0.0, normalOS.x > 0.0 ? -1.0 : 1.0);
        bitangentOS = float3(0.0, 1.0, 0.0);
    } else if (axis == 1u) {
        tangentOS = float3(normalOS.y > 0.0 ? -1.0 : 1.0, 0.0, 0.0);
        bitangentOS = float3(0.0, 0.0, 1.0);
    } else {
        tangentOS = float3(normalOS.z > 0.0 ? 1.0 : -1.0, 0.0, 0.0);
        bitangentOS = float3(0.0, 1.0, 0.0);
    }
}

/**
 * UV before the per-texture transform. Single sample on the dominant axis; for RT hits and anything without a blend.
 * @param metricPosOS MetricObjectPosition, or object position times ModelScale.
 */
public float2 MaterialBaseUV(MaterialProperties material, float2 meshUV, float3 metricPosOS, float3 normalOS)
{
    if (material.uvMode != MATERIAL_UV_TRIPLANAR) {
        return meshUV;
    }
    return TriplanarAxis(metricPosOS, normalOS, TriplanarDominantAxis(normalOS));
}
