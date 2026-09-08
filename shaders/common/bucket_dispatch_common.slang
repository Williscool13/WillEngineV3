module bucket_dispatch_common;

import constants_interop;

// tileSlot = workgroup index into the bucket's slice of the tile list
public uint2 BucketPixel(uint32_t* tileListBuffer, uint tileCapacity, uint bucket, uint tileSlot, uint2 pixelInTile)
{
    uint tile = tileListBuffer[bucket * tileCapacity + tileSlot];
    return uint2(tile & 0xFFFFu, tile >> 16) * BUCKET_TILE_SIZE + pixelInTile;
}

public bool BucketTileEdge(uint2 pixelInTile)
{
    return pixelInTile.x == 0u || pixelInTile.y == 0u || pixelInTile.x == BUCKET_TILE_SIZE - 1u || pixelInTile.y == BUCKET_TILE_SIZE - 1u;
}
