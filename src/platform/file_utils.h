//
// Created by William on 2026-04-01.
//

#ifndef WILL_ENGINE_FILE_UTILS_H
#define WILL_ENGINE_FILE_UTILS_H

#include <string_view>

#include "core/containers/inline_path.h"
#include "core/containers/vector.h"

namespace Platform
{
/**
 * Creates all intermediate directories in path, similar to std::filesystem::create_directories.
 * @param path
 */
void CreateDirectories(const char* path);

void CreateDirectories(const Core::Path& path);

void RemoveDirectories(const char* path);

void RemoveDirectories(const Core::Path& path);

/**
 * Creates all intermediate directories for path, then creates the file. Returns true on success.
 * @param path
 * @return
 */
bool CreateEmptyFile(const char* path);

bool CreateEmptyFile(const Core::Path& path);

/**
 * Deletes the file at path. Returns true on success.
 * @param path
 * @return
 */
bool DeleteSingleFile(const char* path);

bool DeleteSingleFile(const Core::Path& path);

/**
 * Copies src to dst, overwriting dst if it exists. Returns true on success.
 * @param src
 * @param dst
 * @return
 */
bool FileCopy(const char* src, const char* dst);

bool FileCopy(const Core::Path& src, const Core::Path& dst);

/**
 * Returns the last-write time of path as a uint64_t (packed FILETIME on Win32). Returns 0 on failure.
 * @param path
 * @return
 */
uint64_t GetFileWriteTime(const char* path);

uint64_t GetFileWriteTime(const Core::Path& path);

/**
 * Returns the size of the file at path in bytes. Returns 0 on failure.
 * @param path
 * @return
 */
uint64_t GetFileSize(const char* path);

uint64_t GetFileSize(const Core::Path& path);

/**
 * Read-only memory-mapped view of an entire file. data is null on failure (including empty files). Release with UnmapFile.
 */
struct FileMapping
{
    const uint8_t* data{nullptr};
    uint64_t size{0};
    void* fileHandle{nullptr};
    void* mappingHandle{nullptr};
};

FileMapping MapFileReadOnly(const Core::Path& path);

void UnmapFile(FileMapping& mapping);

/** RAII FileMapping */
struct ScopedFileMapping : FileMapping
{
    explicit ScopedFileMapping(const Core::Path& path) : FileMapping(MapFileReadOnly(path)) {}
    ~ScopedFileMapping() { UnmapFile(*this); }
    ScopedFileMapping(const ScopedFileMapping&) = delete;
    ScopedFileMapping& operator=(const ScopedFileMapping&) = delete;
};

/**
 * Recursively enumerates all files under path and appends their paths to out.
 * @param path
 * @param out
 */
void RecursiveDirectoryIterator(const char* path, Core::Vector<Core::Path>& out);

void RecursiveDirectoryIterator(const Core::Path& path, Core::Vector<Core::Path>& out);

/**
 * Recursively finds all files with the given extension under dir.
 * Writes up to maxPaths paths into outPaths. Returns the count written.
 * @param dir
 * @param ext
 * @param outPaths
 * @param maxPaths
 * @return
 */
uint32_t FindFilesByExtension(const Core::Path& dir, const char* ext, Core::Path* outPaths, uint32_t maxPaths);

/**
 * Writes data to path, creating or overwriting the file. Creates intermediate directories. Returns true on success.
 * @param path
 * @param data
 * @param size
 */
bool WriteFile(const Core::Path& path, const void* data, size_t size);

bool WriteFile(const Core::Path& path, std::string_view data);

/**
 * Appends data to path, creating the file if it does not exist. Returns true on success.
 * @param path
 * @param data
 * @param size
 */
bool AppendFile(const Core::Path& path, const void* data, size_t size);

bool AppendFile(const Core::Path& path, std::string_view data);

/**
 * Renames (moves) src to dst. Returns true on success.
 * @param src
 * @param dst
 * @return
 */
bool RenameFile(const char* src, const char* dst);

bool RenameFile(const Core::Path& src, const Core::Path& dst);
} // Platform

#endif //WILL_ENGINE_FILE_UTILS_H
