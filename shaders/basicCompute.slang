float3 ray_color(float3 rayDirection, float3 color1, float3 color2)
{
    float3 unitDirection = normalize(rayDirection);
    float t = 0.5*(unitDirection.y + 1.0);
    return t * color1 + (1.0 - t) * color2;
}


struct PushConstants
{
    float4 color1;
    float4 color2;
    int2 renderSize;
};

[[vk::push_constant]]
PushConstants pc;

[[vk::binding(0, 0)]]
RWTexture2D<float4> renderTextures[];

[shader("compute")]
[numthreads(16, 16, 1)]
void computeMain(uint3 threadId : SV_DispatchThreadID)
{
    uint mipLevel;
    float width;
    float height;

    int2 texelCoord = int2(threadId.xy);
    int2 size;
    renderTextures[0].GetDimensions(size.x, size.y);

    if(texelCoord.x < size.x && texelCoord.y < size.y)
    {
        float focal_length = 1.0;
        float viewport_height = 2.0;
        float viewport_width = viewport_height * float(size.x) / float(size.y);
        float3 cam_center = float3(0, 0, 0);
        float3 viewport_u = float3(viewport_width, 0, 0);
        float3 viewport_v = float3(0, -viewport_height, 0);
        float pixel_delta_u = viewport_width / float(size.x);
        float pixel_delta_v = viewport_height / float(size.y);
        float3 upper_left = cam_center - viewport_u/2.0 + viewport_v/2.0 - float3(0, 0, focal_length);
        float3 pixel00_loc = upper_left + float3(0, 0, 0);
        float3 pixel_center = pixel00_loc + float3(float(texelCoord.x) * pixel_delta_u, float(texelCoord.y) * pixel_delta_v, 0);
        float3 ray_direction = pixel_center - cam_center;

        float3 color = ray_color(ray_direction, pc.color1.xyz, pc.color2.xyz);
        color = float3(float(texelCoord.x) / float(size.x), 1.0f - float(texelCoord.y) / float(size.y), 0.0);
        renderTextures[0][texelCoord] = float4(color, 1.0);
    };
}