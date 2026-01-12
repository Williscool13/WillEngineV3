import common_interop;

// Returns false if it has been culled
bool FrustumCullSphere(Frustum frustum, float4 sphere) {
    // sphere.xyz = center, sphere.w = radius
    for (int i = 0; i < 6; i++) {
        float4 plane = frustum.planes[i];
        float dist = dot(plane.xyz, sphere.xyz) + plane.w;
        if (dist < -sphere.w) {
            return false;
        }
    }
    return true;
}

// Returns false if backface culled
bool BackfaceCullCone(float3 coneAxis, float coneCutoff, float3 center, float radius, float3 cameraPos, float3x3 modelMatrix) {
    float3 worldConeAxis = normalize(mul(modelMatrix, coneAxis));
    float3 cameraToCenter = center - cameraPos;
    float centerDist = length(cameraToCenter);

    // Cone is backfacing if: dot(center - camera, axis) >= cutoff * dist + radius
    return dot(cameraToCenter, worldConeAxis) < coneCutoff * centerDist + radius;
}

float3x3 Adjugate(float3x3 m) {
    return float3x3(
        cross(m[1], m[2]),
        cross(m[2], m[0]),
        cross(m[0], m[1])
    );
}

// Reconstructs position of a pixel based on inverse project and depth buffer
float3 ReconstructViewPosition(float4x4 invProj, float2 uv, float depth) {

    float2 ndc = uv * 2.0 - 1.0;
    // Note: NDC Y-flip required because viewport uses negative height
    //uv.y = 1 - uv.y;
    ndc.y = -ndc.y;

    float4 positionVS = mul(invProj, float4(ndc, depth, 1.0));
    return positionVS.xyz / positionVS.w;
}
float3 RGBToYCoCg(float3 rgb) {
    float Y  = dot(rgb, float3( 0.25,  0.5,   0.25));
    float Co = dot(rgb, float3( 0.5,   0.0,  -0.5));
    float Cg = dot(rgb, float3(-0.25,  0.5,  -0.25));
    return float3(Y, Co, Cg);
}

float3 YCoCgToRGB(float3 ycocg) {
    float Y  = ycocg.x;
    float Co = ycocg.y;
    float Cg = ycocg.z;

    float tmp = Y - Cg;
    float r = tmp + Co;
    float g = Y + Cg;
    float b = tmp - Co;

    return float3(r, g, b);
}

uint GetAlbedoIndex(uint packed) { return packed & 0xFF; }
uint GetNormalIndex(uint packed) { return (packed >> 8) & 0xFF; }
uint GetPBRIndex(uint packed) { return (packed >> 16) & 0xFF; }
uint GetDepthIndex(uint packed) { return (packed >> 24) & 0xFF; }