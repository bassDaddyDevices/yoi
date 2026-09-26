//
//  BDDEnvelopes.hpp
//  Bass Daddy Devices shared DSP
//
//  Header-only, allocation-free and safe to use from the render thread.
//

#pragma once

#include <algorithm>
#include <cmath>

namespace bdd {

/// Attack-decay-sustain-release envelope with analogue-style exponential segments, after Nigel
/// Redmon's EarLevel design.
///
/// Every segment starts from wherever the level currently is, so a new note arriving during a
/// release picks up from that level rather than dropping to zero, and never clicks.
struct ADSREnvelope {
    enum class Stage { idle, attack, decay, sustain, release };

    /// Shapes the attack: higher is closer to a straight line. The segment aims past 1 and is cut
    /// off at 1, which gives the gently rounded rise of an analogue envelope.
    static constexpr double kAttackTargetRatio = 0.3;
    /// Shapes decay and release: they aim slightly below their target, so they arrive in finite
    /// time instead of approaching it forever.
    static constexpr double kDecayTargetRatio = 0.0001;
    /// Below this the release counts as finished (-100 dB).
    static constexpr double kSilence = 1.0e-5;

    Stage stage = Stage::idle;
    double level = 0.0;
    double sustainLevel = 1.0;

    double attackCoefficient = 0.0;
    double attackBase = 1.0;
    double decayCoefficient = 0.0;
    double decayBase = 0.0;
    double releaseCoefficient = 0.0;
    double releaseBase = 0.0;
    double sustainFollow = 0.001;

    double sampleRate = 48000.0;
    double decaySeconds = 0.3;

    void setSampleRate(double rate) {
        sampleRate = rate;
        // Lets a changed sustain level glide into place over about 20 ms instead of jumping.
        sustainFollow = 1.0 - std::exp(-1.0 / (0.02 * rate));
    }

    void reset() {
        stage = Stage::idle;
        level = 0.0;
    }

    void setAttack(double seconds) {
        attackCoefficient = coefficient(seconds, kAttackTargetRatio);
        attackBase = (1.0 + kAttackTargetRatio) * (1.0 - attackCoefficient);
    }

    void setDecay(double seconds) {
        decaySeconds = seconds;
        decayCoefficient = coefficient(seconds, kDecayTargetRatio);
        decayBase = (sustainLevel - kDecayTargetRatio) * (1.0 - decayCoefficient);
    }

    void setSustain(double newLevel) {
        sustainLevel = std::clamp(newLevel, 0.0, 1.0);
        setDecay(decaySeconds);
    }

    void setRelease(double seconds) {
        releaseCoefficient = coefficient(seconds, kDecayTargetRatio);
        releaseBase = -kDecayTargetRatio * (1.0 - releaseCoefficient);
    }

    void gateOn() { stage = Stage::attack; }

    void gateOff() {
        if (stage != Stage::idle) {
            stage = Stage::release;
        }
    }

    bool isActive() const { return stage != Stage::idle; }

    inline double next() {
        switch (stage) {
            case Stage::idle:
                break;
            case Stage::attack:
                level = attackBase + level * attackCoefficient;
                if (level >= 1.0) {
                    level = 1.0;
                    stage = Stage::decay;
                }
                break;
            case Stage::decay:
                level = decayBase + level * decayCoefficient;
                if (level <= sustainLevel) {
                    level = sustainLevel;
                    stage = Stage::sustain;
                }
                break;
            case Stage::sustain:
                level += (sustainLevel - level) * sustainFollow;
                break;
            case Stage::release:
                level = releaseBase + level * releaseCoefficient;
                if (level <= kSilence) {
                    level = 0.0;
                    stage = Stage::idle;
                }
                break;
        }
        return level;
    }

private:
    double coefficient(double seconds, double targetRatio) const {
        const double samples = std::max(1.0, seconds * sampleRate);
        return std::exp(-std::log((1.0 + targetRatio) / targetRatio) / samples);
    }
};

} // namespace bdd
