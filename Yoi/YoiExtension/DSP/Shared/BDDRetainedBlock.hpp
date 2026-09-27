//
//  BDDRetainedBlock.hpp
//  Bass Daddy Devices shared DSP (Apple platforms only)
//
//  Holds its own reference to an Objective-C block, such as a host's musical context or transport
//  state block, so the block cannot be freed while C++ code still means to call it.
//
//  Why this exists: a kernel that simply stores the block pointer it is handed does not keep the
//  block alive. When Swift passes a host block to C++ it passes a temporary wrapper that is freed
//  as soon as the call returns, so the stored pointer dangles and calling it later jumps into
//  whatever reused that memory. In YOI that crashed the plug-in on its render thread and could
//  take the host's connection down with it. Take blocks straight from the AUAudioUnit in C++ and
//  hold them here instead.
//
//  Works whether or not the including code is compiled with ARC. Retaining and releasing only
//  happen when a block is captured or dropped (allocate and deallocate render resources), never
//  on the render thread.
//

#pragma once

#if defined(__APPLE__)

#include <Block.h>

#if __has_feature(objc_arc)
#define BDD_BLOCK_AS_POINTER(block) ((__bridge const void*)(block))
#define BDD_POINTER_AS_BLOCK(Type, pointer) ((__bridge Type)(pointer))
#else
#define BDD_BLOCK_AS_POINTER(block) ((const void*)(block))
#define BDD_POINTER_AS_BLOCK(Type, pointer) ((Type)(pointer))
#endif

namespace bdd {

class RetainedBlock {
public:
    RetainedBlock() = default;

    RetainedBlock(const RetainedBlock& other)
        : mPointer(other.mPointer != nullptr ? _Block_copy(other.mPointer) : nullptr) {}

    RetainedBlock& operator=(const RetainedBlock& other) {
        if (this != &other) {
            reset(other.mPointer);
        }
        return *this;
    }

    ~RetainedBlock() {
        if (mPointer != nullptr) {
            _Block_release(mPointer);
        }
    }

    /// Takes a reference to `block` (given as a pointer, see `BDD_BLOCK_AS_POINTER`) and drops
    /// the previous one. Null clears it.
    void reset(const void* block) {
        const void* retained = (block != nullptr) ? _Block_copy(block) : nullptr;
        if (mPointer != nullptr) {
            _Block_release(mPointer);
        }
        mPointer = retained;
    }

    bool isSet() const { return mPointer != nullptr; }
    const void* pointer() const { return mPointer; }

private:
    const void* mPointer = nullptr;
};

} // namespace bdd

#endif // __APPLE__
