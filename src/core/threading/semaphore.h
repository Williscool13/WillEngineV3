//
// Created by William on 2026-09-06.
//

#ifndef WILL_ENGINE_SEMAPHORE_H
#define WILL_ENGINE_SEMAPHORE_H

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>

namespace Core
{
/**
 * Counting semaphore on mutex + CV.
 */
class Semaphore
{
public:
    Semaphore() = default;

    explicit Semaphore(uint32_t initialCount) : count(initialCount) {}

    Semaphore(const Semaphore&) = delete;

    Semaphore& operator=(const Semaphore&) = delete;

    void Release(uint32_t n = 1)
    {
        {
            std::lock_guard lock(mutex);
            count.fetch_add(n, std::memory_order_release);
        }
        if (n == 1) { cv.notify_one(); }
        else { cv.notify_all(); }
    }

    /** Blocks until a token is available, then takes it. */
    void Acquire()
    {
        std::unique_lock lock(mutex);
        cv.wait(lock, [this] { return count.load(std::memory_order_acquire) > 0; });
        count.fetch_sub(1, std::memory_order_relaxed);
    }

    /** Takes a token if one is available; never blocks. */
    bool TryAcquire()
    {
        std::lock_guard lock(mutex);
        if (count.load(std::memory_order_acquire) == 0) { return false; }
        count.fetch_sub(1, std::memory_order_relaxed);
        return true;
    }

    /** Blocks up to timeout for a token; returns false on timeout without taking one. */
    bool AcquireFor(std::chrono::steady_clock::duration timeout)
    {
        std::unique_lock lock(mutex);
        if (!cv.wait_for(lock, timeout, [this] { return count.load(std::memory_order_acquire) > 0; })) { return false; }
        count.fetch_sub(1, std::memory_order_relaxed);
        return true;
    }

    [[nodiscard]] uint32_t Count() const { return count.load(std::memory_order_acquire); }

private:
    std::atomic<uint32_t> count{0};
    std::mutex mutex;
    std::condition_variable cv;
};
} // Core

#endif //WILL_ENGINE_SEMAPHORE_H
