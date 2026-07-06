module ddgi_common;

import ddgi_interop;

// =============== Shader Summary ============
// Rolling-grid math for the DDGI probe volume. Probes live on an infinite world-space lattice (cell * spacing); the volume is a window of probeCount cells starting at baseCell that follows the camera in whole-cell steps.
// Storage slots map to cells via Euclidean modulo so a scroll only invalidates the newly exposed planes; every other slot keeps its probe.

public int3 DDGIEuclideanMod(int3 v, int3 m)
{
    return ((v % m) + m) % m;
}

/** Lattice cell held by a storage slot for the current window. */
public int3 DDGICellFromSlot(int3 slot, DDGIVolumeParams volume)
{
    return volume.baseCell + DDGIEuclideanMod(slot - volume.baseCell, int3(volume.probeCount));
}

/** Storage slot holding a lattice cell; only meaningful for cells inside the window. */
public int3 DDGISlotFromCell(int3 cell, DDGIVolumeParams volume)
{
    return DDGIEuclideanMod(cell, int3(volume.probeCount));
}

public float3 DDGIProbeWorldPosition(int3 cell, DDGIVolumeParams volume)
{
    return float3(cell) * volume.probeSpacing;
}
