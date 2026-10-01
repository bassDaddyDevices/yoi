//
//  BDDParameterRamps.hpp
//  Bass Daddy Devices shared DSP
//
//  Parameter ramps a host asks for (an Audio Unit's ramp events, a VST3 automation segment):
//  "move this parameter from where it is to here over this many frames". The render thread owns
//  them; each block, every running ramp moves on and hands its parameter the new value. Stepping
//  once per block is enough because the kernel smooths every parameter on the way in.
//
//  Fixed capacity, no allocation, so it is safe on the render thread.
//

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace bdd {

template <typename Address, size_t Capacity = 16>
class ParameterRamps {
public:
    /// Starts a ramp from `from` to `to` over `frames`, replacing any ramp already running on
    /// that parameter. Returns false if every slot is busy; the caller should then just set `to`.
    bool start(Address address, float from, float to, uint32_t frames) {
        Ramp* slot = find(address);
        if (slot == nullptr) {
            for (auto& ramp : mRamps) {
                if (!ramp.active) {
                    slot = &ramp;
                    break;
                }
            }
        }
        if (slot == nullptr) {
            return false;
        }
        *slot = { address, from, to, frames, 0, true };
        return true;
    }

    /// Stops the ramp on `address`, if any, where it is. A plain parameter change does this, so
    /// the newest instruction from the host wins.
    void cancel(Address address) {
        if (Ramp* ramp = find(address)) {
            ramp->active = false;
        }
    }

    void clear() {
        for (auto& ramp : mRamps) {
            ramp.active = false;
        }
    }

    /// Moves every running ramp on by `frames` and calls `apply(address, value)` with where each
    /// is at the end of them. A ramp that reaches its end applies exactly its end value and stops.
    template <typename Apply>
    void advance(uint32_t frames, Apply&& apply) {
        for (auto& ramp : mRamps) {
            if (!ramp.active) {
                continue;
            }
            ramp.elapsed = (frames >= ramp.duration - ramp.elapsed) ? ramp.duration : ramp.elapsed + frames;
            if (ramp.elapsed >= ramp.duration) {
                ramp.active = false;
                apply(ramp.address, ramp.to);
            } else {
                const float progress = float(double(ramp.elapsed) / double(ramp.duration));
                apply(ramp.address, ramp.from + (ramp.to - ramp.from) * progress);
            }
        }
    }

    bool isRamping(Address address) const {
        for (const auto& ramp : mRamps) {
            if (ramp.active && ramp.address == address) {
                return true;
            }
        }
        return false;
    }

private:
    struct Ramp {
        Address address {};
        float from = 0.0f;
        float to = 0.0f;
        uint32_t duration = 0;
        uint32_t elapsed = 0;
        bool active = false;
    };

    Ramp* find(Address address) {
        for (auto& ramp : mRamps) {
            if (ramp.active && ramp.address == address) {
                return &ramp;
            }
        }
        return nullptr;
    }

    std::array<Ramp, Capacity> mRamps {};
};

} // namespace bdd
