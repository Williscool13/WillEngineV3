//
// Created by William on 2026-09-10.
//

#include "png_write.h"

#include <cstdio>
#include <miniz/miniz.h>

namespace Utils
{
static constexpr size_t STORED_BLOCK_MAX = 65535;

static void PutBE32(uint8_t* p, uint32_t v)
{
    p[0] = static_cast<uint8_t>(v >> 24);
    p[1] = static_cast<uint8_t>(v >> 16);
    p[2] = static_cast<uint8_t>(v >> 8);
    p[3] = static_cast<uint8_t>(v);
}

bool WritePngRgba8(const char* path, uint32_t width, uint32_t height, const uint8_t* rgba, bool bFlipRows)
{
    FILE* f = fopen(path, "wb");
    if (f == nullptr) { return false; }

    uint8_t head[33] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A, 0, 0, 0, 13, 'I', 'H', 'D', 'R'};
    PutBE32(head + 16, width);
    PutBE32(head + 20, height);
    head[24] = 8;
    head[25] = 6;
    PutBE32(head + 29, static_cast<uint32_t>(mz_crc32(MZ_CRC32_INIT, head + 12, 17)));
    fwrite(head, 1, sizeof(head), f);

    const size_t rowBytes = static_cast<size_t>(width) * 4;
    const size_t blocksPerRow = (rowBytes + 1 + STORED_BLOCK_MAX - 1) / STORED_BLOCK_MAX;
    const size_t idatSize = 2 + (rowBytes + 1) * height + blocksPerRow * height * 5 + 4;
    uint8_t idat[8] = {0, 0, 0, 0, 'I', 'D', 'A', 'T'};
    PutBE32(idat, static_cast<uint32_t>(idatSize));
    fwrite(idat, 1, sizeof(idat), f);

    mz_ulong crc = mz_crc32(MZ_CRC32_INIT, idat + 4, 4);
    mz_ulong adler = MZ_ADLER32_INIT;
    auto put = [&](const uint8_t* p, size_t n) {
        fwrite(p, 1, n, f);
        crc = mz_crc32(crc, p, n);
    };
    auto putPayload = [&](const uint8_t* p, size_t n) {
        put(p, n);
        adler = mz_adler32(adler, p, n);
    };

    const uint8_t zlibHeader[2] = {0x78, 0x01};
    put(zlibHeader, 2);
    const uint8_t filterNone = 0;
    for (uint32_t row = 0; row < height; ++row) {
        const uint8_t* src = rgba + (bFlipRows ? height - 1 - row : row) * rowBytes;
        size_t rowLeft = rowBytes;
        bool bFirst = true;
        while (bFirst || rowLeft > 0) {
            const size_t filterBytes = bFirst ? 1 : 0;
            const size_t payload = rowLeft < STORED_BLOCK_MAX - filterBytes ? rowLeft : STORED_BLOCK_MAX - filterBytes;
            const uint32_t len = static_cast<uint32_t>(payload + filterBytes);
            const bool bLast = row + 1 == height && payload == rowLeft;
            const uint8_t block[5] = {static_cast<uint8_t>(bLast), static_cast<uint8_t>(len), static_cast<uint8_t>(len >> 8), static_cast<uint8_t>(~len), static_cast<uint8_t>(~len >> 8)};
            put(block, 5);
            if (bFirst) { putPayload(&filterNone, 1); }
            putPayload(src, payload);
            src += payload;
            rowLeft -= payload;
            bFirst = false;
        }
    }

    uint8_t tail[8];
    PutBE32(tail, static_cast<uint32_t>(adler));
    put(tail, 4);
    PutBE32(tail + 4, static_cast<uint32_t>(crc));
    fwrite(tail + 4, 1, 4, f);

    static const uint8_t iend[12] = {0, 0, 0, 0, 'I', 'E', 'N', 'D', 0xAE, 0x42, 0x60, 0x82};
    fwrite(iend, 1, sizeof(iend), f);
    fclose(f);
    return true;
}
} // Utils
