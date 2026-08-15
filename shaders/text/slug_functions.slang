// Slug glyph coverage (Lengyel 2017, JCGT), ported from HarfBuzz hb-gpu-fragment.glsl + hb-gpu-draw-fragment.glsl.
// Curve data is the hb-gpu blob: RGBA16I texels (8 bytes) read through BDA as uint2, offsets glyph-relative with a -32768 bias.
// renderCoord is in font units (the space the blob was encoded in); coordinates quantize at 4 steps per font unit.

static const float SLUG_INV_UNITS = 1.0 / 4.0;

int4 SlugFetch(uint2* texels, int loc)
{
    uint2 t = texels[loc];
    return int4((int(t.x) << 16) >> 16,
                int(t.x) >> 16,
                (int(t.y) << 16) >> 16,
                int(t.y) >> 16);
}

// Which of the two quadratic roots contribute winding, from the three control-point sign bits
uint SlugCalcRootCode(float y1, float y2, float y3)
{
    uint i1 = asuint(y1) >> 31u;
    uint i2 = asuint(y2) >> 30u;
    uint i3 = asuint(y3) >> 29u;

    uint shift = (i2 & 2u) | (i1 & ~2u);
    shift = (i3 & 4u) | (shift & ~4u);

    return (0x2E74u >> shift) & 0x0101u;
}

float2 SlugSolveHorizPoly(float2 a, float2 b, float2 p1)
{
    float ra = 1.0 / a.y;
    float rb = 0.5 / b.y;

    float d = sqrt(max(b.y * b.y - a.y * p1.y, 0.0));
    float t1 = (b.y - d) * ra;
    float t2 = (b.y + d) * ra;

    if (a.y == 0.0) {
        t1 = t2 = p1.y * rb;
    }

    return float2((a.x * t1 - b.x * 2.0) * t1 + p1.x,
                  (a.x * t2 - b.x * 2.0) * t2 + p1.x);
}

float2 SlugSolveVertPoly(float2 a, float2 b, float2 p1)
{
    float ra = 1.0 / a.x;
    float rb = 0.5 / b.x;

    float d = sqrt(max(b.x * b.x - a.x * p1.x, 0.0));
    float t1 = (b.x - d) * ra;
    float t2 = (b.x + d) * ra;

    if (a.x == 0.0) {
        t1 = t2 = p1.x * rb;
    }

    return float2((a.y * t1 - b.y * 2.0) * t1 + p1.y,
                  (a.y * t2 - b.y * 2.0) * t2 + p1.y);
}

// Weighted average of the two axis estimates, floored by the more conservative of the two
float SlugCombineCoverage(float xcov, float ycov, float xwgt, float ywgt)
{
    float coverage = max(abs(xcov * xwgt + ycov * ywgt) / max(xwgt + ywgt, 1.0 / 65536.0),
                         min(abs(xcov), abs(ycov)));
    return clamp(coverage, 0.0, 1.0);
}

struct SlugGlyphInfo
{
    int glyphLoc;
    int bandBase;
    int2 bandIndex;
    int numHBands;
    int numVBands;
    float2 scale;
};

SlugGlyphInfo SlugDecodeGlyph(uint2* texels, float2 renderCoord, uint glyphLoc_)
{
    SlugGlyphInfo gi;
    gi.glyphLoc = int(glyphLoc_);

    int4 header0 = SlugFetch(texels, gi.glyphLoc);
    int4 header1 = SlugFetch(texels, gi.glyphLoc + 1);
    float4 ext = float4(header0) * SLUG_INV_UNITS;
    // header1 layout is (numHBands, numVBands, x_scale, y_scale) in RGBA lanes
    gi.numHBands = header1.x;
    gi.numVBands = header1.y;
    gi.scale = float2(float(header1.z), float(header1.w));

    float2 extSize = ext.zw - ext.xy;
    float2 bandScale = float2(float(gi.numVBands), float(gi.numHBands)) / max(extSize, float2(1.0 / 65536.0));
    float2 bandOffset = -ext.xy * bandScale;

    gi.bandIndex = clamp(int2(renderCoord * bandScale + bandOffset),
                         int2(0, 0),
                         int2(gi.numVBands - 1, gi.numHBands - 1));

    gi.bandBase = gi.glyphLoc + 2;
    return gi;
}

// Pixels per em at this fragment, from the encoder-embedded font scale
float SlugPpem(SlugGlyphInfo gi, float2 pixelsPerEm)
{
    float2 emsPerPixel = 1.0 / pixelsPerEm;
    return min(gi.scale.x, gi.scale.y) / max(emsPerPixel.x, emsPerPixel.y);
}

// Single-sample analytic coverage in [0, 1]
float SlugCoverageSingle(uint2* texels, float2 renderCoord, float2 pixelsPerEm, uint glyphLoc_)
{
    SlugGlyphInfo gi = SlugDecodeGlyph(texels, renderCoord, glyphLoc_);
    int glyphLoc = gi.glyphLoc;
    int bandBase = gi.bandBase;
    int numHBands = gi.numHBands;

    float xcov = 0.0;
    float xwgt = 0.0;

    int4 hbandData = SlugFetch(texels, bandBase + gi.bandIndex.y);
    int hCurveCount = hbandData.x;
    // Symmetric rays: shoot right (descending list) or left (ascending list) of the split coordinate
    float hSplit = float(hbandData.w) * SLUG_INV_UNITS;
    bool hLeftRay = renderCoord.x < hSplit;
    int hDataOffset = (hLeftRay ? hbandData.z : hbandData.y) + 32768;

    [loop]
    for (int ci = 0; ci < hCurveCount; ci++) {
        int curveOffset = SlugFetch(texels, glyphLoc + hDataOffset + ci).x + 32768;

        int4 raw12 = SlugFetch(texels, glyphLoc + curveOffset);
        int4 raw3 = SlugFetch(texels, glyphLoc + curveOffset + 1);

        float4 q12 = float4(raw12) * SLUG_INV_UNITS;
        float2 q3 = float2(raw3.xy) * SLUG_INV_UNITS;

        float4 p12 = q12 - float4(renderCoord, renderCoord);
        float2 p3 = q3 - renderCoord;

        if (hLeftRay) {
            if (min(min(p12.x, p12.z), p3.x) * pixelsPerEm.x > 0.5) { break; }
        } else {
            if (max(max(p12.x, p12.z), p3.x) * pixelsPerEm.x < -0.5) { break; }
        }

        uint code = SlugCalcRootCode(p12.y, p12.w, p3.y);
        if (code != 0u) {
            float2 a = q12.xy - q12.zw * 2.0 + q3;
            float2 b = q12.xy - q12.zw;
            float2 r = SlugSolveHorizPoly(a, b, p12.xy) * pixelsPerEm.x;
            float2 cov = hLeftRay ? clamp(float2(0.5, 0.5) - r, 0.0, 1.0)
                                  : clamp(r + float2(0.5, 0.5), 0.0, 1.0);

            if ((code & 1u) != 0u) {
                xcov += cov.x;
                xwgt = max(xwgt, clamp(1.0 - abs(r.x) * 2.0, 0.0, 1.0));
            }
            if (code > 1u) {
                xcov -= cov.y;
                xwgt = max(xwgt, clamp(1.0 - abs(r.y) * 2.0, 0.0, 1.0));
            }
        }
    }

    float ycov = 0.0;
    float ywgt = 0.0;

    int4 vbandData = SlugFetch(texels, bandBase + numHBands + gi.bandIndex.x);
    int vCurveCount = vbandData.x;
    float vSplit = float(vbandData.w) * SLUG_INV_UNITS;
    bool vLeftRay = renderCoord.y < vSplit;
    int vDataOffset = (vLeftRay ? vbandData.z : vbandData.y) + 32768;

    [loop]
    for (int ci = 0; ci < vCurveCount; ci++) {
        int curveOffset = SlugFetch(texels, glyphLoc + vDataOffset + ci).x + 32768;

        int4 raw12 = SlugFetch(texels, glyphLoc + curveOffset);
        int4 raw3 = SlugFetch(texels, glyphLoc + curveOffset + 1);

        float4 q12 = float4(raw12) * SLUG_INV_UNITS;
        float2 q3 = float2(raw3.xy) * SLUG_INV_UNITS;

        float4 p12 = q12 - float4(renderCoord, renderCoord);
        float2 p3 = q3 - renderCoord;

        if (vLeftRay) {
            if (min(min(p12.y, p12.w), p3.y) * pixelsPerEm.y > 0.5) { break; }
        } else {
            if (max(max(p12.y, p12.w), p3.y) * pixelsPerEm.y < -0.5) { break; }
        }

        uint code = SlugCalcRootCode(p12.x, p12.z, p3.x);
        if (code != 0u) {
            float2 a = q12.xy - q12.zw * 2.0 + q3;
            float2 b = q12.xy - q12.zw;
            float2 r = SlugSolveVertPoly(a, b, p12.xy) * pixelsPerEm.y;
            float2 cov = vLeftRay ? clamp(float2(0.5, 0.5) - r, 0.0, 1.0)
                                  : clamp(r + float2(0.5, 0.5), 0.0, 1.0);

            if ((code & 1u) != 0u) {
                ycov -= cov.x;
                ywgt = max(ywgt, clamp(1.0 - abs(r.x) * 2.0, 0.0, 1.0));
            }
            if (code > 1u) {
                ycov += cov.y;
                ywgt = max(ywgt, clamp(1.0 - abs(r.y) * 2.0, 0.0, 1.0));
            }
        }
    }

    return SlugCombineCoverage(xcov, ycov, xwgt, ywgt);
}

// Coverage with the small-ppem quality path: below 16 ppem blend in 4 extra samples at +/- 1/3 pixel
float SlugCoverage(uint2* texels, float2 renderCoord, float2 pixelsPerEm, uint glyphLoc_)
{
    float c = SlugCoverageSingle(texels, renderCoord, pixelsPerEm, glyphLoc_);

    SlugGlyphInfo gi = SlugDecodeGlyph(texels, renderCoord, glyphLoc_);
    float ppem = SlugPpem(gi, pixelsPerEm);

    if (ppem < 16.0) {
        float2 emsPerPixel = 1.0 / pixelsPerEm;
        float2 d = emsPerPixel * (1.0 / 3.0);
        float msaa = 0.25 *
            (SlugCoverageSingle(texels, renderCoord + float2(-d.x, -d.y), pixelsPerEm, glyphLoc_) +
             SlugCoverageSingle(texels, renderCoord + float2(d.x, -d.y), pixelsPerEm, glyphLoc_) +
             SlugCoverageSingle(texels, renderCoord + float2(-d.x, d.y), pixelsPerEm, glyphLoc_) +
             SlugCoverageSingle(texels, renderCoord + float2(d.x, d.y), pixelsPerEm, glyphLoc_));

        c = lerp(c, msaa, smoothstep(16.0, 8.0, ppem));
    }

    return c;
}

// Fragment entry helper: derivatives at uniform control flow, then coverage
float SlugDraw(uint2* texels, float2 renderCoord, uint glyphLoc_)
{
    float2 pixelsPerEm = 1.0 / fwidth(renderCoord);
    return SlugCoverage(texels, renderCoord, pixelsPerEm, glyphLoc_);
}
