import push_constant_interop;
import ui_functions;

struct VertexOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float2 clayPx; // Clay pixel coords (y=0 top), interpolated across quad
};

[shader("vertex")]
VertexOutput VertexUIBorderDefault(uint vid : SV_VertexID, uniform UIBorderPushConstant pc)
{
    float px = (vid & 1u) != 0u ? pc.ndcMax.x : pc.ndcMin.x;
    float py = (vid & 2u) != 0u ? pc.ndcMax.y : pc.ndcMin.y;
    float cpx = (vid & 1u) != 0u ? pc.pxMax.x : pc.pxMin.x;
    float cpy = (vid & 2u) != 0u ? pc.pxMax.y : pc.pxMin.y;
    VertexOutput o;
    o.position = float4(px, py, 0.0, 1.0);
    o.clayPx = float2(cpx, cpy);
    return o;
}

[shader("fragment")]
float4 FragmentUIBorderDefault(VertexOutput input, uniform UIBorderPushConstant pc) : SV_Target0
{
    float2 center = (pc.pxMin + pc.pxMax) * 0.5;
    float2 halfSize = (pc.pxMax - pc.pxMin) * 0.5;
    float2 p = input.clayPx - center;

    float dOuter = sdRoundedBoxPerCorner(p, halfSize, pc.cornerRadius);

    // Inner rect inset by per-side border widths (x=left, y=right, z=top, w=bottom).
    float2 innerCenterOffset = float2(
        (pc.borderWidths.x - pc.borderWidths.y) * 0.5,
        (pc.borderWidths.z - pc.borderWidths.w) * 0.5
    );
    float2 innerHalfSize = max(
        halfSize - float2(
            (pc.borderWidths.x + pc.borderWidths.y) * 0.5,
            (pc.borderWidths.z + pc.borderWidths.w) * 0.5
        ),
        float2(0.0, 0.0)
    );
    float maxBorder = max(max(pc.borderWidths.x, pc.borderWidths.y), max(pc.borderWidths.z, pc.borderWidths.w));
    float4 innerCornerRadius = max(pc.cornerRadius - maxBorder, float4(0.0, 0.0, 0.0, 0.0));
    float dInner = sdRoundedBoxPerCorner(p - innerCenterOffset, innerHalfSize, innerCornerRadius);

    // Border region: inside outer boundary, outside inner boundary.
    float alpha = smoothstep(1.0, -1.0, dOuter) * smoothstep(-1.0, 1.0, dInner);
    if (alpha < 0.001) { discard; }
    return float4(pc.color.rgb, pc.color.a * alpha);
}
