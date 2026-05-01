module shadow_functions;

import common_interop;
import constants_interop;
import shadows_interop;
import shader_constants;

float DepthToZ(float depth, float n, float f) {
	// return n * f / (depth * (n - f) + f); (normal depth buffer)
	return n * f / (f - depth * (f - n));
}

public float PCF(Texture2D<float> shadowMap, SamplerComparisonState comparisonSampler, float2 uv, float receiverDepth, float filterRadius, int32_t sampleCount) {
	float sum = 0.0;
	for (int i = 0; i < sampleCount; ++i) {
		float2 offset;
		switch (sampleCount) {
		    case 8:
    		    offset = POISSON_DISK_8[i];
    		    break;
		    case 16:
		        offset = POISSON_DISK_16[i];
		        break;
    		case 32:
    		    offset = POISSON_DISK_32[i];
    		    break;
    		case 64:
    		    offset = POISSON_DISK_64[i];
    		    break;
            default:
                offset = POISSON_DISK_64[i];
                break;
		}

		sum += shadowMap.SampleCmpLevelZero(comparisonSampler, uv + (offset * filterRadius), receiverDepth);
	}
	return sum / sampleCount;
}

float FindBlockerDistance(Texture2D<float> shadowMap, SamplerState sampler, float2 uv, float receiverDepth, float nearPlane, float farPlane, float searchRadius, float lightSize)
{
    float blockerSum = 0.0;
    int blockerCount = 0;

	for (int i = 0; i < 16; ++i) {
        float2 offset = POISSON_DISK_16[i] * searchRadius;
        float shadowDepth = shadowMap.SampleLevel(sampler, uv + offset, 0).r;

        if (shadowDepth >= receiverDepth) {
            float blockerViewDepth = DepthToZ(shadowDepth, nearPlane, farPlane);
            blockerSum += blockerViewDepth;
            blockerCount++;
        }
    }

    if (blockerCount == 0) {
        return -1.0;
    }

    return blockerSum / float(blockerCount);
}

float PCSS(Texture2D<float> shadowMap, SamplerState sampler, SamplerComparisonState comparisonSampler, float2 uv, float receiverDepth, float receiverViewSpaceDepth, float lightSize, float nearPlane, float farPlane, int pcfSamples)
{
    // Blocker search
    float searchRadius = lightSize * (receiverViewSpaceDepth - nearPlane) / receiverViewSpaceDepth;
    float avgBlockerDepth = FindBlockerDistance(shadowMap, sampler, uv, receiverDepth, nearPlane, farPlane, searchRadius, lightSize);

    if (avgBlockerDepth < 0.0) {
        return 1.0f;
    }

    // Penumbra estimation
    float penumbra = (receiverViewSpaceDepth - avgBlockerDepth) / avgBlockerDepth;
    float filterRadius = penumbra * lightSize * nearPlane / receiverViewSpaceDepth;
    filterRadius = clamp(filterRadius, 0.001, 0.05);


    return PCF(shadowMap, comparisonSampler, uv, receiverDepth, filterRadius, pcfSamples);
}
