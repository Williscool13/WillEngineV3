module bindless_declarations;


// Descriptor buffer for model textures
[[vk::binding(0, 0)]] public SamplerState assetSamplers[];
[[vk::binding(1, 0)]] public Texture2D assetTextures[];
[[vk::binding(2, 0)]] public TextureCube assetCubemaps[];

// Descriptor buffer for RDG
[[vk::binding(0, 1)]] public SamplerState rdgSamplers[];
[[vk::binding(1, 1)]] public SamplerComparisonState rdgCompareSamplers[];
[[vk::binding(2, 1)]] public Texture2D<float4> rdgTexturesFloat4[];
[[vk::binding(3, 1)]] public RWTexture2D<float4> rdgStorageFloat4[];
[[vk::binding(4, 1)]] public RWTexture2D<float2> rdgStorageFloat2[];
[[vk::binding(5, 1)]] public RWTexture2D<float>  rdgStorageFloat[];
[[vk::binding(6, 1)]] public RWTexture2D<uint4>  rdgStorageUInt4[];
[[vk::binding(7, 1)]] public RWTexture2D<uint2>  rdgStorageUInt2[];
[[vk::binding(8, 1)]] public RWTexture2D<uint>   rdgStorageUInt[];
[[vk::binding(9, 1)]]  public Texture2D<float2>  rdgTexturesFloat2[];
[[vk::binding(10, 1)]] public Texture2D<float>   rdgTexturesFloat[];
[[vk::binding(11, 1)]] public Texture2D<uint4>   rdgTexturesUInt4[];
[[vk::binding(12, 1)]] public Texture2D<uint2>   rdgTexturesUInt2[];
[[vk::binding(13, 1)]] public Texture2D<uint>    rdgTexturesUInt[];