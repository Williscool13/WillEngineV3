module light_index_common;

import lights_interop;

/** Triangles start at the fixed MAX_ANALYTIC_LIGHTS boundary so BRDF-hit bases survive analytic churn; the gap between holds nothing. */

public uint LiveLightCount(int lightCount, int analyticCount)
{
    return uint(analyticCount) + uint(max(lightCount - int(MAX_ANALYTIC_LIGHTS), 0));
}

/** Compacted [0, LiveLightCount) -> real index into LightData::lights. */
public uint LiveLightIndex(uint i, int analyticCount)
{
    return (i < uint(analyticCount)) ? i : (i - uint(analyticCount)) + uint(MAX_ANALYTIC_LIGHTS);
}

public bool IsLiveLight(uint li, int lightCount, int analyticCount)
{
    if (li >= uint(lightCount)) { return false; }
    return li < uint(analyticCount) || li >= uint(MAX_ANALYTIC_LIGHTS);
}
