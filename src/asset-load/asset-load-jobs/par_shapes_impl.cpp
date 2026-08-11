//
// Created by William on 2026-03-14.
//

#include "core/memory/tlsf_allocator.h"

#include <cassert>
#include <cstring>

static Core::TlsfAllocator* gParShapesAllocator = nullptr;

static void* ParShapesAlloc(size_t bytes)
{
    assert(gParShapesAllocator && "par_shapes_set_allocator not called");
    return gParShapesAllocator->Alloc(bytes, Core::AllocTag::ParShapes);
}

static void* ParShapesCalloc(size_t bytes)
{
    void* ptr = gParShapesAllocator->Alloc(bytes, Core::AllocTag::ParShapes);
    if (ptr) { memset(ptr, 0, bytes); }
    return ptr;
}

static void* ParShapesRealloc(void* ptr, size_t bytes)
{
    return gParShapesAllocator->Realloc(ptr, bytes, Core::AllocTag::ParShapes);
}

static void ParShapesFree(void* ptr)
{
    gParShapesAllocator->Free(ptr);
}

#define PAR_MALLOC(T, N) ((T*) ParShapesAlloc((N) * sizeof(T)))
#define PAR_CALLOC(T, N) ((T*) ParShapesCalloc((N) * sizeof(T)))
#define PAR_REALLOC(T, BUF, N) ((T*) ParShapesRealloc(BUF, sizeof(T) * (N)))
#define PAR_FREE(BUF) ParShapesFree(BUF)

#define PAR_SHAPES_IMPLEMENTATION
#include "par/par_shapes.h"

void par_shapes_set_allocator(Core::TlsfAllocator* allocator)
{
    gParShapesAllocator = allocator;
}

extern "C" par_shapes_mesh* par_shapes_create_staircase(int steps, float width, float total_depth, float total_height, float step_height, int closed)
{
    if (steps < 1) { return nullptr; }

    const float stepDepth  = total_depth / (float) steps;
    const float uniformH   = total_height / (float) steps;
    const float usedStepH  = (step_height > 0.0f) ? step_height : uniformH;

    // Cross-section polygon in the Z-Y plane (CW when viewed from +X).
    // Layout: (0,0), [riser top, tread end] x steps, (totalDepth, totalHeight), (totalDepth, 0)
    const int npts = 2 * steps + 2;
    float* pz = PAR_MALLOC(float, npts);
    float* py = PAR_MALLOC(float, npts);

    pz[0] = 0.0f;
    py[0] = 0.0f;
    for (int i = 0; i < steps; i++) {
        // All steps except the last use usedStepH; the last takes the remainder so
        // total_height is preserved exactly (handles non-integer step count case).
        float yTop = (i < steps - 1) ? (i + 1) * usedStepH : total_height;
        pz[2 * i + 1] = i * stepDepth;
        py[2 * i + 1] = yTop;
        pz[2 * i + 2] = (i + 1) * stepDepth;
        py[2 * i + 2] = yTop;
    }
    pz[npts - 1] = total_depth;
    py[npts - 1] = 0.0f;

    // npts front verts [0, npts) + npts back verts [npts, 2*npts)
    // closed: caps (npts-2)*2 + side quads 2*npts
    // open:   no caps, no back wall (j=npts-2) -> 2*(npts-1) side quads
    const int nverts = 2 * npts;
    const int ntris  = closed ? (npts - 2) + (npts - 2) + 2 * npts : 2 * (npts - 1);

    par_shapes_mesh* m = PAR_CALLOC(par_shapes_mesh, 1);
    m->npoints = nverts;
    m->ntriangles = ntris;
    m->points = PAR_MALLOC(float, nverts * 3);
    m->triangles = PAR_MALLOC(PAR_SHAPES_T, ntris * 3);

    const float xf = 0.0f;
    const float xb = width;

    for (int i = 0; i < npts; i++) {
        m->points[i * 3 + 0] = xf;
        m->points[i * 3 + 1] = py[i];
        m->points[i * 3 + 2] = pz[i];
        m->points[(npts + i) * 3 + 0] = xb;
        m->points[(npts + i) * 3 + 1] = py[i];
        m->points[(npts + i) * 3 + 2] = pz[i];
    }

    PAR_SHAPES_T* t = m->triangles;

    if (closed) {
        // Front cap (-X face): fan from back-bottom vertex (npts-1); that vertex sees all
        // diagonals inside the concave staircase polygon, avoiding guard-rail artifacts.
        for (int i = 0; i < npts - 2; i++) {
            *t++ = (PAR_SHAPES_T) (npts - 1);
            *t++ = (PAR_SHAPES_T) (i + 1);
            *t++ = (PAR_SHAPES_T) (i);
        }
        // Back cap (+X face): same, fan from the corresponding back-bottom vertex (2*npts-1)
        for (int i = 0; i < npts - 2; i++) {
            *t++ = (PAR_SHAPES_T) (2 * npts - 1);
            *t++ = (PAR_SHAPES_T) (npts + i);
            *t++ = (PAR_SHAPES_T) (npts + i + 1);
        }
    }

    // Side quads: one per profile edge, winding gives outward normals.
    // When open, skip j=npts-2 (the back wall at z=total_depth).
    for (int j = 0; j < npts; j++) {
        if (!closed && j == npts - 2) { continue; }
        PAR_SHAPES_T fj = (PAR_SHAPES_T) j, fj1 = (PAR_SHAPES_T) ((j + 1) % npts);
        PAR_SHAPES_T bj = (PAR_SHAPES_T) (npts + j), bj1 = (PAR_SHAPES_T) (npts + (j + 1) % npts);
        *t++ = fj;
        *t++ = fj1;
        *t++ = bj1;
        *t++ = fj;
        *t++ = bj1;
        *t++ = bj;
    }

    PAR_FREE(pz);
    PAR_FREE(py);
    return m;
}
