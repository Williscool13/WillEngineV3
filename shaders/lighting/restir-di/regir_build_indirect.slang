import push_constant_interop;
import restir_interop;

[shader("compute")]
[numthreads(1, 1, 1)]
void ComputeMain(uniform ReGIRBuildIndirectPushConstant pc)
{
    pc.indirectArgs[0] = min(pc.activeCount[0], REGIR_HASH_CAPACITY);
    pc.indirectArgs[1] = 1u;
    pc.indirectArgs[2] = 1u;
}
