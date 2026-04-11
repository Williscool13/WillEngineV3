//
// Created by William on 2026-04-01.
//

#ifndef WILL_ENGINE_FILE_UTILS_H
#define WILL_ENGINE_FILE_UTILS_H

#include "core/containers/inline_path.h"
#include "core/containers/vector.h"

namespace Platform
{
/**
 * Creates all intermediate directories in path, similar to std::filesystem::create_directories.
 * @param path
 */
void CreateDirectories(const char* path);

void RemoveDirectories(const char* path);

// Creates all intermediate directories for path, then creates the file. Returns true on success.
bool CreateEmptyFile(const char* path);

// Deletes the file at path. Returns true on success.
bool DeleteSingleFile(const char* path);

// Copies src to dst, overwriting dst if it exists. Returns true on success.
bool FileCopy(const char* src, const char* dst);

// Returns the last-write time of path as a uint64_t (packed FILETIME on Win32). Returns 0 on failure.
uint64_t GetFileWriteTime(const char* path);

// Recursively enumerates all files under path and appends their paths to out.
void RecursiveDirectoryIterator(const char* path, Core::Vector<Core::Path>& out);
void RecursiveDirectoryIterator(const Core::Path& path, Core::Vector<Core::Path>& out);
} // Platform

#endif //WILL_ENGINE_FILE_UTILS_H
