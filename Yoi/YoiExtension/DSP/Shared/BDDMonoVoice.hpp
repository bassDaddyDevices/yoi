//
//  BDDMonoVoice.hpp
//  Bass Daddy Devices shared DSP
//
//  Note handling for monophonic instruments: which key is sounding, and how the pitch moves
//  between keys.
//
//  Header-only, allocation-free and safe to use from the render thread.
//

#pragma once

#include <array>
#include <cstdint>

namespace bdd {

/// The keys currently held, oldest first, so the newest is the one that sounds.
///
/// Releasing the newest key hands the voice back to the most recent key still held, which is
/// what players expect from a mono synth when they trill or roll between notes.
struct NoteStack {
    static constexpr int kCapacity = 32;

    std::array<uint8_t, kCapacity> notes{};
    int count = 0;

    void clear() { count = 0; }
    bool empty() const { return count == 0; }

    /// The sounding key, or -1 when none is held.
    int newest() const { return count > 0 ? int(notes[size_t(count - 1)]) : -1; }

    void push(int note) {
        remove(note);
        if (count == kCapacity) {
            removeAt(0);
        }
        notes[size_t(count++)] = uint8_t(note & 0x7F);
    }

    void remove(int note) {
        for (int i = 0; i < count; ++i) {
            if (int(notes[size_t(i)]) == note) {
                removeAt(i);
                return;
            }
        }
    }

private:
    void removeAt(int index) {
        for (int i = index; i + 1 < count; ++i) {
            notes[size_t(i)] = notes[size_t(i + 1)];
        }
        --count;
    }
};

/// Portamento: slides the pitch, in semitones, to a new note over a fixed time.
///
/// The slide is a straight line in pitch, so every interval takes the same time and moves at an
/// even musical speed, the way a player expects a glide control to behave.
struct Glide {
    double current = 60.0;
    double target = 60.0;
    double step = 0.0;
    int64_t remaining = 0;

    void jump(double pitch) {
        current = pitch;
        target = pitch;
        remaining = 0;
    }

    void glideTo(double pitch, double seconds, double sampleRate) {
        target = pitch;
        remaining = int64_t(seconds * sampleRate);
        if (remaining <= 0) {
            jump(pitch);
            return;
        }
        step = (target - current) / double(remaining);
    }

    bool isGliding() const { return remaining > 0; }

    inline double next() {
        if (remaining > 0) {
            current += step;
            if (--remaining == 0) {
                current = target;
            }
        }
        return current;
    }
};

} // namespace bdd
