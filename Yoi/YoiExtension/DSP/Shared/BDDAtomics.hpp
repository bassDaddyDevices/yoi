//
//  BDDAtomics.hpp
//  Bass Daddy Devices shared DSP
//
//  Atomic access to plain `uint32_t` members, for handing data between the UI and render threads.
//
//  Kernels keep these as ordinary integers rather than `std::atomic` members so that they stay
//  copyable, which Swift's C++ interop needs to hold a kernel as a value. Clang and GCC reach them
//  through their atomic builtins; MSVC, which has none, goes through `std::atomic_ref` instead.
//  Both lower to the same instructions. (Carried over from Graphite.)
//

#pragma once

#include <cstdint>

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
#endif

} // namespace bdd::atomics
