module procedural_texture_declarations;

import constants_interop;

[[vk::binding(0, 0)]] public SamplerState proceduralSamplers[];
[[vk::binding(1, 0)]] public RWTexture2D<float4> proceduralOutputTextures[];
