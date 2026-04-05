//
// Created by William on 2026-04-01.
//

#ifndef WILL_ENGINE_FILE_UTILS_H
#define WILL_ENGINE_FILE_UTILS_H

namespace Platform
{
/**
 * Creates all intermediate directories in path, similar to std::filesystem::create_directories.
 * @param path
 */
void CreateDirectories(const char* path);

void RemoveDirectories(const char* path);

// Copies src to dst, overwriting dst if it exists. Returns true on success.
bool FileCopy(const char* src, const char* dst);
} // Platform

#endif //WILL_ENGINE_FILE_UTILS_H
