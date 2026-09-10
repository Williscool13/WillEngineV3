//
// Created by William on 2026-09-10.
//

#ifndef WILL_ENGINE_PNG_WRITE_H
#define WILL_ENGINE_PNG_WRITE_H

#include <cstdint>

namespace Utils
{
/**
 * Writes 8-bit RGBA pixels as a PNG made of stored (uncompressed) deflate blocks: memcpy speed, about 4 bytes per pixel on disk.
 * Rows are read top-down unless bFlipRows, which reads them bottom-up as a GPU readback lands.
 * @return false if the file could not be opened
 */
bool WritePngRgba8(const char* path, uint32_t width, uint32_t height, const uint8_t* rgba, bool bFlipRows);
} // Utils

#endif //WILL_ENGINE_PNG_WRITE_H
