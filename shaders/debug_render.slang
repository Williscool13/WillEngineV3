import common_interop;
import constants_interop;
import model_interop;
import push_constant_interop;
import bindless_declarations;

// =============== Shader Summary ============
/* Simple debug line renderer using vertex shader.
 * Draws lines and boxes with alpha blending for visualization.
 * No culling - assumes CPU has already culled primitives.
 */

struct VertexOutput
{
    float4 position : SV_Position;
    float4 color : COLOR0;
};

// =============== Vertex Shader ===============
[shader("vertex")]
VertexOutput VertexMain(
    uint vertexID : SV_VertexID,
    uniform DebugDrawPushConstant pc)
{
    DebugVertex v = pc.vertexBuffer[vertexID];

    VertexOutput output;
    output.position = mul(pc.sceneData[pc.sceneDataIndex].unjitteredViewProj, float4(v.position, 1.0));
    output.color = v.color;

    return output;
}

// =============== Fragment Shader ===============
[shader("fragment")]
float4 FragmentMain(VertexOutput input) : SV_Target0
{
    return input.color;
}