//
//  BDDDenormals.hpp
//  Bass Daddy Devices shared DSP
//
//  Flushes denormal floats to zero for the length of a scope, then puts the thread's floating
//  point mode back as the host had it. Feedback paths (filters, envelopes and detectors decaying
//  towards silence) otherwise spend their tails in denormals, which on x86 can cost a hundred
//  times the CPU of a normal number. Apple silicon handles them at full speed, so there this is
//  insurance; the Windows VST3 build is where it counts.
//
//  Results change only for numbers below about 1e-38, far under anything audible.
//

#pragma once

#include <cstdint>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <xmmintrin.h>
#define BDD_DENORMALS_SSE 1
#elif defined(__aarch64__) && (defined(__clang__) || defined(__GNUC__))
#define BDD_DENORMALS_AARCH64 1
#endif

namespace bdd {

class ScopedFlushDenormals {
public:
    ScopedFlushDenormals() {
#if defined(BDD_DENORMALS_SSE)
        mSaved = _mm_getcsr();
        _mm_setcsr(mSaved | kFlushToZero | kDenormalsAreZero);
#elif defined(BDD_DENORMALS_AARCH64)
        uint64_t fpcr;
        __asm__ __volatile__("mrs %0, fpcr" : "=r"(fpcr));
        mSaved = fpcr;
        if ((fpcr & kFlushToZero) == 0) {
            fpcr |= kFlushToZero;
            __asm__ __volatile__("msr fpcr, %0" : : "r"(fpcr));
        }
#endif
    }

    ~ScopedFlushDenormals() {
#if defined(BDD_DENORMALS_SSE)
        _mm_setcsr(static_cast<unsigned int>(mSaved));
#elif defined(BDD_DENORMALS_AARCH64)
        if ((mSaved & kFlushToZero) == 0) {
            const uint64_t fpcr = mSaved;
            __asm__ __volatile__("msr fpcr, %0" : : "r"(fpcr));
        }
#endif
    }

    ScopedFlushDenormals(const ScopedFlushDenormals&) = delete;
    ScopedFlushDenormals& operator=(const ScopedFlushDenormals&) = delete;

private:
#if defined(BDD_DENORMALS_SSE)
    static constexpr unsigned int kFlushToZero = 0x8000;       // MXCSR FTZ
    static constexpr unsigned int kDenormalsAreZero = 0x0040;  // MXCSR DAZ
#elif defined(BDD_DENORMALS_AARCH64)
    static constexpr uint64_t kFlushToZero = uint64_t(1) << 24;   // FPCR FZ
#endif
    uint64_t mSaved = 0;
};

} // namespace bdd
