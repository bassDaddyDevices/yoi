//
//  BDDAtomics.hpp
//  Bass Daddy Devices shared DSP
//
//  Atomic access to plain members, for handing data between the UI and render threads: helpers
//  for `uint32_t` slots, and `bdd::Relaxed<T>` for values such as parameter targets.
//
//  Kernels keep these as ordinary integers rather than `std::atomic` members so that they stay
//  copyable, which Swift's C++ interop needs to hold a kernel as a value. Clang and GCC reach them
//  through their atomic builtins; MSVC, which has none, goes through `std::atomic_ref` instead.
//  Both lower to the same instructions. (Carried over from Graphite.)
//

#pragma once

#include <cstdint>
#include <type_traits>

#if defined(_MSC_VER) && !defined(__clang__)
#include <atomic>
#endif

namespace bdd::atomics {

#if defined(_MSC_VER) && !defined(__clang__)
inline uint32_t loadAcquire(uint32_t& slot) {
    return std::atomic_ref<uint32_t>(slot).load(std::memory_order_acquire);
}
inline uint32_t loadRelaxed(uint32_t& slot) {
    return std::atomic_ref<uint32_t>(slot).load(std::memory_order_relaxed);
}
inline void storeRelaxed(uint32_t& slot, uint32_t value) {
    std::atomic_ref<uint32_t>(slot).store(value, std::memory_order_relaxed);
}
inline void incrementAcquireRelease(uint32_t& slot) {
    std::atomic_ref<uint32_t>(slot).fetch_add(1u, std::memory_order_acq_rel);
}
inline uint32_t exchangeAcquireRelease(uint32_t& slot, uint32_t value) {
    return std::atomic_ref<uint32_t>(slot).exchange(value, std::memory_order_acq_rel);
}
#else
inline uint32_t loadAcquire(uint32_t& slot) {
    return __atomic_load_n(&slot, __ATOMIC_ACQUIRE);
}
inline uint32_t loadRelaxed(uint32_t& slot) {
    return __atomic_load_n(&slot, __ATOMIC_RELAXED);
}
inline void storeRelaxed(uint32_t& slot, uint32_t value) {
    __atomic_store_n(&slot, value, __ATOMIC_RELAXED);
}
inline void incrementAcquireRelease(uint32_t& slot) {
    __atomic_fetch_add(&slot, 1u, __ATOMIC_ACQ_REL);
}
inline uint32_t exchangeAcquireRelease(uint32_t& slot, uint32_t value) {
    return __atomic_exchange_n(&slot, value, __ATOMIC_ACQ_REL);
}
#endif

} // namespace bdd::atomics

namespace bdd {

/// A value one thread sets and another reads, such as a parameter target written by the host or
/// the UI and read by the render thread. Every read and write is a relaxed atomic, so it is never
/// a data race, and it compiles to the same plain loads and stores. It reads and assigns like a
/// `T`, and unlike `std::atomic` it can be copied, so a kernel holding these stays copyable.
///
/// Relaxed means each value arrives whole but in no particular order with others: fine for
/// independent targets that are smoothed anyway, not for handing over buffers (use a seqlock).
template <typename T>
class Relaxed {
    static_assert(std::is_trivially_copyable_v<T>
                  && (sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4 || sizeof(T) == 8),
                  "Relaxed holds small, plain values that are lock-free on every target");
public:
    constexpr Relaxed() = default;
    constexpr Relaxed(T value) : mValue(value) {}
    Relaxed(const Relaxed& other) : mValue(other.load()) {}
    Relaxed& operator=(const Relaxed& other) { store(other.load()); return *this; }
    Relaxed& operator=(T value) { store(value); return *this; }
    operator T() const { return load(); }

    T load() const {
#if defined(_MSC_VER) && !defined(__clang__)
        return std::atomic_ref<T>(mValue).load(std::memory_order_relaxed);
#else
        T value;
        __atomic_load(&mValue, &value, __ATOMIC_RELAXED);
        return value;
#endif
    }

    void store(T value) {
#if defined(_MSC_VER) && !defined(__clang__)
        std::atomic_ref<T>(mValue).store(value, std::memory_order_relaxed);
#else
        __atomic_store(&mValue, &value, __ATOMIC_RELAXED);
#endif
    }

private:
    mutable T mValue {};
};

} // namespace bdd
