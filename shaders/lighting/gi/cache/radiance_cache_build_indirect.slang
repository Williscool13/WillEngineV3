import push_constant_interop;
import radiance_cache_interop;

[shader("compute")]
[numthreads(1, 1, 1)]
void ComputeRadianceCacheBuildIndirect(uniform RadianceCacheBuildIndirectPushConstant pc)
{
    uint count = min(pc.activeCount[0], RADIANCE_CACHE_SHADE_BUDGET);
    pc.activeCount[0] = count;
    pc.indirectArgs[0] = (count + 63u) / 64u;
    pc.indirectArgs[1] = 1u;
    pc.indirectArgs[2] = 1u;
}
