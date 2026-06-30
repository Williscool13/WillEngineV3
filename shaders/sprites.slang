import push_constant_interop;
import common_interop;
import constants_interop;
import packing_functions;
import bindless_declarations;

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv       : TEXCOORD0;
    float4 color    : COLOR0;
    nointerpolation uint2 stableId : COLOR1;
};

struct FragmentOutput
{
    float4 color   : SV_Target0;
    uint2 stableId : SV_Target1;
};

[shader("fragment")]
FragmentOutput FragmentSprites(VertexOutput input, uniform SpritePushConstant pc)
{
    float4 texSample = assetTextures[pc.textureIndex].Sample(assetSamplers[pc.samplerIndex], input.uv);
    float4 color = texSample * input.color;
    if (color.a < 0.5) { discard; }
    FragmentOutput output;
    output.color    = color;
    output.stableId = input.stableId;
    return output;
}

[shader("mesh")]
[outputtopology("triangle")]
[numthreads(32, 1, 1)]
void MeshSprites(
    out indices uint3 triangles[64], out vertices VertexOutput vertices[128],
    uint3 gid : SV_GroupID, uint3 gtid : SV_GroupThreadID,
    uniform SpritePushConstant pc)
{
    uint validThreads = min(pc.spriteCount - gid.x * 32, 32);
    SetMeshOutputCounts(validThreads * 4, validThreads * 2);

    if (gtid.x >= validThreads) { return; }

    uint spriteIndex = pc.spriteOffset + gid.x * 32 + gtid.x;
    SpriteData sprite = pc.sprites[spriteIndex];
    SceneData scene = pc.sceneData[pc.sceneDataIndex];

    float h = sprite.pixelSize * 0.5;

    uint vBase = gtid.x * 4;
    uint pBase = gtid.x * 2;

    float4 color = UnpackUnorm4x8(sprite.packedColor);
    uint2 sid = uint2(sprite.stableIdLo, sprite.stableIdHi);

    if ((sprite.flags & SPRITE_FLAG_BILLBOARD) != 0u)
    {
        float3 viewPos = mul(scene.view, float4(sprite.worldPosition, 1.0)).xyz;
        vertices[vBase + 0].position = mul(scene.proj, float4(viewPos + float3(-h,  h, 0.0), 1.0));
        vertices[vBase + 1].position = mul(scene.proj, float4(viewPos + float3( h,  h, 0.0), 1.0));
        vertices[vBase + 2].position = mul(scene.proj, float4(viewPos + float3(-h, -h, 0.0), 1.0));
        vertices[vBase + 3].position = mul(scene.proj, float4(viewPos + float3( h, -h, 0.0), 1.0));
    }
    else
    {
        float3 wp = sprite.worldPosition;
        vertices[vBase + 0].position = mul(scene.proj, mul(scene.view, float4(wp + float3(-h, 0.0,  h), 1.0)));
        vertices[vBase + 1].position = mul(scene.proj, mul(scene.view, float4(wp + float3( h, 0.0,  h), 1.0)));
        vertices[vBase + 2].position = mul(scene.proj, mul(scene.view, float4(wp + float3(-h, 0.0, -h), 1.0)));
        vertices[vBase + 3].position = mul(scene.proj, mul(scene.view, float4(wp + float3( h, 0.0, -h), 1.0)));
    }

    vertices[vBase + 0].uv = float2(0.0, 0.0);
    vertices[vBase + 1].uv = float2(1.0, 0.0);
    vertices[vBase + 2].uv = float2(0.0, 1.0);
    vertices[vBase + 3].uv = float2(1.0, 1.0);

    [unroll]
    for (int i = 0; i < 4; i++)
    {
        vertices[vBase + i].color    = color;
        vertices[vBase + i].stableId = sid;
    }

    triangles[pBase + 0] = uint3(vBase + 0, vBase + 1, vBase + 2);
    triangles[pBase + 1] = uint3(vBase + 1, vBase + 3, vBase + 2);
}
