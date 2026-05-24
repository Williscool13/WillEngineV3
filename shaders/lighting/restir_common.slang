module restir_common;

public struct ReservoirSample
{
    public int lightIdx;
    public float3 samplePos;
    public float attenuation;
}

/**
 * A single ReSTIR reservoir storing one selected light sample.
 *
 * @param y     Selected sample index, or -1 if empty.
 * @param wSum  Running sum of candidate weights during WRS streaming update.
 * @param M     Number of candidates seen so far.
 * @param W     Unbiased contribution weight: wSum / (M * targetPdf(y)). Multiply shading result by W to get the unbiased estimate.
 */
public struct Reservoir
{
    public int32_t y;
    public float wSum;
    public int32_t M;
    public float W;
}

public Reservoir ReservoirEmpty()
{
    Reservoir r;
    r.y = -1;
    r.wSum = 0.0;
    r.M = 0;
    r.W = 0.0;
    return r;
}

/**
 * Streaming WRS update for a single new candidate.
 *
 * @param r Reservoir to update.
 * @param candidateY Index of the incoming candidate.
 * @param candidateWeight w_i = targetPdf(candidateY) / sourcePdf(candidateY).
 * @param u Uniform random float in [0, 1).
 * @return True if the candidate was selected.
 */
public bool ReservoirAddSample(inout Reservoir r, int candidateY, float candidateWeight, float u)
{
    r.wSum += candidateWeight;
    r.M += 1;
    if (u < candidateWeight / r.wSum)
    {
        r.y = candidateY;
        return true;
    }
    return false;
}

/**
 * Merges another reservoir into this one (temporal/spatial reuse). The incoming reservoir contributes its full M to the confidence count.
 *
 * @param r Reservoir to merge into.
 * @param other Reservoir to merge in.
 * @param targetPdfY p_hat evaluated at other.y from this pixel's perspective.
 * @param u Uniform random float in [0, 1).
 * @return True if other.y was selected.
 */
public bool ReservoirMerge(inout Reservoir r, Reservoir other, float targetPdfY, float u)
{
    float candidateWeight = targetPdfY * other.W * float(other.M);
    r.wSum += candidateWeight;
    r.M += other.M;
    if (u < candidateWeight / r.wSum)
    {
        r.y = other.y;
        return true;
    }
    return false;
}

/**
 * Finalises W after all candidates/merges have been streamed.
 *
 * @param r Reservoir to finalise.
 * @param targetPdfY p_hat evaluated at the selected sample y.
 */
public void ReservoirFinalise(inout Reservoir r, float targetPdfY)
{
    r.W = (targetPdfY > 0.0) ? (r.wSum / targetPdfY) : 0.0;
}
