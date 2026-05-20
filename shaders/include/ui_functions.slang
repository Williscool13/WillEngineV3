// Inigo Quilez rounded-box SDF.
float sdRoundedBox(float2 p, float2 halfSize, float r)
{
    float2 q = abs(p) - halfSize + r;
    return length(max(q, float2(0.0, 0.0))) + min(max(q.x, q.y), 0.0) - r;
}

/**
 * @param p Fragment position relative to box center, in Clay pixel space (y=0 top).
 * @param halfSize Half-extents of the box in pixels.
 * @param cornerRadius x=TL, y=TR, z=BL, w=BR in pixels.
 * @return SDF value: negative inside, positive outside.
 */
float sdRoundedBoxPerCorner(float2 p, float2 halfSize, float4 cornerRadius)
{
    float r = (p.x >= 0.0)
        ? ((p.y >= 0.0) ? cornerRadius.w : cornerRadius.y)
        : ((p.y >= 0.0) ? cornerRadius.z : cornerRadius.x);
    return sdRoundedBox(p, halfSize, r);
}
