import shader_constants;

[shader("vertex")]
FullscreenPassVertexOutput FullscreenPassVertexMain(uint vertexID : SV_VertexID) {
    FullscreenPassVertexOutput output;

    float2 uv = float2((vertexID << 1) & 2, vertexID & 2);
    output.position = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    output.uv = uv;
    return output;
}
