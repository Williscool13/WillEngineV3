module light_tree;

import restir_interop;
import random_common;

/** Squared distance from a point to an AABB (0 when the point is inside). */
public float Dist2ToAABB(float3 p, float3 bmin, float3 bmax)
{
    float3 d = max(max(bmin - p, p - bmax), float3(0.0));
    return dot(d, d);
}

/**
 * Node importance: cluster power over inverse-square distance to its bounds, attenuated by the orientation cone (v2).
 *
 * Distance floor: max(Dist2ToAABB, bounding-sphere r2); without it Dist2ToAABB collapses to ~0 for points inside the
 * loose, scene-spanning boxes near the root, so the top splits would be driven by power alone and an extreme light
 * would drag every pixel down its branch. Padding nodes (power 0) still score 0.
 *
 * Cone (Conty-Estevez): thetaW is the angle from the cluster axis to p; thetaB the half-angle the bounds subtend from p;
 * thetaPrime = max(0, thetaW - thetaO - thetaB) measures how far p sits outside the combined orientation cone, hard-culled
 * past the emission angle thetaE. This culls clusters facing away from p, which the one-sided cosFacing/cosLight in
 * EvalCellLightTarget/EvalPHat already zero, so it only removes wasted candidates (variance, never bias). Inside the
 * bounding sphere the orientation is unreliable, so it falls back to the plain v1 importance.
 */
public float NodeImportance(LightBVHNode n, float3 p)
{
    float3 he = 0.5 * (n.bmax - n.bmin);
    float r2 = dot(he, he);
    float dist2 = max(Dist2ToAABB(p, n.bmin, n.bmax), r2);
    float imp = n.power / max(dist2, 1e-4);
    if (imp <= 0.0) { return 0.0; }

    float3 center = 0.5 * (n.bmin + n.bmax);
    float3 toP = p - center;
    float centerDist2 = dot(toP, toP);
    if (centerDist2 <= r2) { return imp; }

    float centerDist = sqrt(centerDist2);
    float thetaB = asin(clamp(sqrt(r2) / centerDist, 0.0, 1.0));
    float thetaW = acos(clamp(dot(n.coneAxis, toP / centerDist), -1.0, 1.0));
    float thetaPrime = max(0.0, thetaW - n.coneThetaO - thetaB);
    if (thetaPrime > n.coneThetaE) { return 0.0; }
    return imp * cos(thetaPrime);
}

public struct LightTreeSample
{
    public uint lightIdx; // ~0u if the descent hit a padding subtree (caller skips)
    public float pdf;      // analytic selection probability of lightIdx at p
};

/**
 * Stochastic root->leaf descent over the implicit complete tree. Picks each child with probability proportional to its
 * importance, accumulating the product as the analytic selection pdf. Stackless. numLeaves must be a power of two; 0 => no tree.
 */
public LightTreeSample SampleLightTree(LightBVHNode* nodes, uint numLeaves, float3 p, inout uint h)
{
    LightTreeSample s;
    s.lightIdx = ~0u;
    s.pdf = 0.0;
    if (numLeaves == 0u) { return s; }

    uint leafBase = numLeaves - 1u;
    uint node = 0u;
    float pdf = 1.0;
    while (node < leafBase) {
        uint left = 2u * node + 1u;
        float iL = NodeImportance(nodes[left], p);
        float iR = NodeImportance(nodes[2u * node + 2u], p);
        float total = iL + iR;
        float pL = (total > 0.0) ? (iL / total) : 0.5;
        if (HashStep(h) < pL) {
            node = left;
            pdf *= pL;
        } else {
            node = left + 1u;
            pdf *= (1.0 - pL);
        }
    }
    s.lightIdx = nodes[node].lightIdx;
    s.pdf = pdf;
    return s;
}

/**
 * Selection pdf SampleLightTree would assign to a specific light at p (for the BRDF stream's balance-heuristic MIS).
 * Walks the light's leaf up to the root to recover the path, then down multiplying the same child-selection probs.
 * The pL math MUST stay identical to SampleLightTree or the two MIS streams disagree and the estimator is biased.
 */
public float LightTreePdf(LightBVHNode* nodes, uint* lightLeaf, uint numLeaves, float3 p, uint lightIdx)
{
    if (numLeaves <= 1u) { return 1.0; }

    uint leafNode = (numLeaves - 1u) + lightLeaf[lightIdx];
    uint chain[24];
    uint depth = 0u;
    for (uint n = leafNode; n > 0u; n = (n - 1u) >> 1u) {
        chain[depth] = n;
        depth += 1u;
    }

    uint node = 0u;
    float pdf = 1.0;
    for (int k = int(depth) - 1; k >= 0; k--) {
        uint child = chain[k];
        uint left = 2u * node + 1u;
        float iL = NodeImportance(nodes[left], p);
        float iR = NodeImportance(nodes[2u * node + 2u], p);
        float total = iL + iR;
        float pL = (total > 0.0) ? (iL / total) : 0.5;
        pdf *= (child == left) ? pL : (1.0 - pL);
        node = child;
    }
    return pdf;
}
