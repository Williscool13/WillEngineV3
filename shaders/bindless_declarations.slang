module bindless_declarations;


// Descriptor buffer for model textures
[[vk::binding(0, 0)]]
public SamplerState assetSamplers[];
[[vk::binding(1, 0)]]
public Texture2D assetTextures[];

// Descriptor buffer for RDG
[[vk::binding(0, 1)]] public SamplerState rdgSamplers[];
[[vk::binding(1, 1)]] public SamplerComparisonState rdgCompareSamplers[];
[[vk::binding(2, 1)]] public Texture2D rdgTextures[];
[[vk::binding(3, 1)]] public RWTexture2D<float4> rdgStorageFloat4[];
[[vk::binding(4, 1)]] public RWTexture2D<float2> rdgStorageFloat2[];
[[vk::binding(5, 1)]] public RWTexture2D<float>  rdgStorageFloat[];
[[vk::binding(6, 1)]] public RWTexture2D<uint4>  rdgStorageUInt4[];
[[vk::binding(7, 1)]] public RWTexture2D<uint>   rdgStorageUInt[];