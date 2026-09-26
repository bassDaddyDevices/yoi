//
//  yoi_dsp_tests.cpp
//  YOI DSP tests
//
//  Plain C++ checks that render real audio through the shared kernel: it stays silent when it
//  should, plays in tune, handles overlapping notes the way the voice decisions say, glides,
//  bends, and never leaves full scale however hard it is pushed.
//
//  No test framework: each check prints what failed and the process exits non-zero.
//

#include "YoiExtensionDSPKernel.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

namespace {

int failures = 0;
int checks = 0;

#define CHECK(condition, ...)                                                 \
    do {                                                                      \
        ++checks;                                                             \
        if (!(condition)) {                                                   \
            ++failures;                                                       \
            std::printf("FAIL %s:%d: %s - ", __FILE__, __LINE__, #condition); \
            std::printf(__VA_ARGS__);                                         \
            std::printf("\n");                                                \
        }                                                                     \
    } while (0)

constexpr double kSampleRate = 48000.0;
constexpr double kPi = 3.14159265358979323846;

using Kernel = YoiExtensionDSPKernel;

std::unique_ptr<Kernel> makeKernel(double sampleRate = kSampleRate) {
    auto kernel = std::make_unique<Kernel>();
    kernel->initialize(2, sampleRate);
    return kernel;
}

int frames(double seconds, double sampleRate = kSampleRate) {
    return int(seconds * sampleRate);
}

/// Renders in host-sized blocks and returns the first channel. Fails the run if any channel
/// differs from the first, since the voice is mono.
std::vector<float> render(Kernel& kernel, int frameCount, int channels = 2, int block = 256) {
    std::vector<float> output;
    output.reserve(size_t(frameCount));
    std::vector<std::vector<float>> buffers(static_cast<size_t>(channels), std::vector<float>(static_cast<size_t>(block)));
    std::vector<float*> pointers;
    for (auto& buffer : buffers) {
        pointers.push_back(buffer.data());
    }

    bool channelsMatch = true;
    for (int start = 0; start < frameCount; start += block) {
        const int count = std::min(block, frameCount - start);
        kernel.process(std::span<float*>(pointers.data(), pointers.size()), 0, uint32_t(count));
        for (int channel = 1; channel < channels; ++channel) {
            channelsMatch = channelsMatch && std::equal(buffers[0].begin(), buffers[0].begin() + count,
                                                        buffers[size_t(channel)].begin());
        }
        output.insert(output.end(), buffers[0].begin(), buffers[0].begin() + count);
    }
    CHECK(channelsMatch, "output channels differ");
    return output;
}

double rms(const std::vector<float>& samples, size_t from = 0, size_t to = 0) {
    to = (to == 0) ? samples.size() : std::min(to, samples.size());
    double sum = 0.0;
    for (size_t index = from; index < to; ++index) {
        sum += double(samples[index]) * double(samples[index]);
    }
    return (to > from) ? std::sqrt(sum / double(to - from)) : 0.0;
}

float peak(const std::vector<float>& samples) {
    float highest = 0.0f;
    for (float sample : samples) {
        highest = std::max(highest, std::fabs(sample));
    }
    return highest;
}

bool allFinite(const std::vector<float>& samples) {
    return std::all_of(samples.begin(), samples.end(), [](float sample) { return std::isfinite(sample); });
}

/// Fundamental frequency from upward zero crossings, interpolated between samples. Needs a signal
/// with one upward crossing per cycle, which a saw through a low cutoff is.
double measuredFrequency(const std::vector<float>& samples, size_t from, size_t to, double sampleRate = kSampleRate) {
    double first = -1.0;
    double last = -1.0;
    int crossings = 0;
    for (size_t index = std::max<size_t>(from, 1); index < to && index < samples.size(); ++index) {
        const float before = samples[index - 1];
        const float after = samples[index];
        if (before < 0.0f && after >= 0.0f) {
            const double position = double(index - 1) + double(-before / (after - before));
            if (first < 0.0) {
                first = position;
            }
            last = position;
            ++crossings;
        }
    }
    return (crossings > 1) ? double(crossings - 1) * sampleRate / (last - first) : 0.0;
}

/// Energy at one frequency (Goertzel), normalised by length.
double energyAt(const std::vector<float>& samples, size_t from, size_t to, double frequency, double sampleRate = kSampleRate) {
    const double coefficient = 2.0 * std::cos(2.0 * kPi * frequency / sampleRate);
    double s1 = 0.0;
    double s2 = 0.0;
    for (size_t index = from; index < to; ++index) {
        const double s0 = double(samples[index]) + coefficient * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    const double power = s1 * s1 + s2 * s2 - coefficient * s1 * s2;
    return std::sqrt(std::max(0.0, power)) / double(to - from);
}

double noteHertz(double note) {
    return 440.0 * std::pow(2.0, (note - 69.0) / 12.0);
}

/// Settings that make pitch easy to measure: saw only, no sub, a low clean cutoff, instant glide.
void setUpForPitch(Kernel& kernel, double note) {
    kernel.setParameter(oscShape, 0.0f);
    kernel.setParameter(subLevel, 0.0f);
    kernel.setParameter(resonance, 0.0f);
    kernel.setParameter(cutoff, float(noteHertz(note) * 1.5));
    kernel.setParameter(glideTime, 0.0f);
}

// MARK: - Parameters

void testDefaultsMatchParameterTree() {
    // The same values Parameters.swift gives hosts.
    auto kernel = makeKernel();
    struct Expected { AUParameterAddress address; float value; };
    const Expected expected[] = {
        { outputLevel, 0.0f }, { glideTime, 60.0f }, { glideMode, 0.0f }, { bendRange, 2.0f },
        { oscShape, 0.0f }, { subLevel, 50.0f }, { subShape, 0.0f }, { subOctave, 0.0f },
        { filterMode, 0.0f }, { cutoff, 800.0f }, { resonance, 30.0f },
        { ampAttack, 3.0f }, { ampDecay, 300.0f }, { ampSustain, 100.0f }, { ampRelease, 150.0f },
    };
    for (const auto& item : expected) {
        const float actual = kernel->getParameter(item.address);
        CHECK(std::fabs(actual - item.value) < 1e-4f, "address %llu defaults to %f, expected %f",
              (unsigned long long)item.address, actual, item.value);
    }
}

void testParametersRoundTrip() {
    auto kernel = makeKernel();
    struct Item { AUParameterAddress address; float value; };
    const Item items[] = {
        { outputLevel, -12.0f }, { glideTime, 250.0f }, { glideMode, 1.0f }, { bendRange, 12.0f },
        { oscShape, 40.0f }, { subLevel, 75.0f }, { subShape, 60.0f }, { subOctave, 1.0f },
        { filterMode, 1.0f }, { cutoff, 1234.0f }, { resonance, 85.0f },
        { ampAttack, 20.0f }, { ampDecay, 900.0f }, { ampSustain, 40.0f }, { ampRelease, 700.0f },
    };
    for (const auto& item : items) {
        kernel->setParameter(item.address, item.value);
        const float actual = kernel->getParameter(item.address);
        CHECK(std::fabs(actual - item.value) < 1e-3f, "address %llu round-tripped %f as %f",
              (unsigned long long)item.address, item.value, actual);
    }
}

// MARK: - Silence and sound

void testSilentWithoutNotes() {
    auto kernel = makeKernel();
    const auto output = render(*kernel, frames(1.0));
    CHECK(peak(output) == 0.0f, "no notes, but peak was %f", peak(output));
}

void testNotePlaysAndReleasesToSilence() {
    auto kernel = makeKernel();
    kernel->noteOn(36, 100);
    const auto held = render(*kernel, frames(0.5));
    CHECK(rms(held, frames(0.1)) > 0.05, "held note is too quiet: %f RMS", rms(held, frames(0.1)));
    CHECK(allFinite(held), "held note produced a non-finite sample");

    kernel->noteOff(36);
    // Release is 150 ms; exponential, so allow plenty of time to reach -100 dB.
    render(*kernel, frames(3.0));
    CHECK(!kernel->isSounding(), "voice still sounding 3 s after release");
    const auto after = render(*kernel, frames(0.2));
    CHECK(peak(after) == 0.0f, "voice not silent after release: peak %f", peak(after));
}

void testAllNotesOffAndAllSoundOff() {
    auto kernel = makeKernel();
    kernel->noteOn(40, 100);
    kernel->noteOn(43, 100);
    render(*kernel, frames(0.2));
    kernel->handleControlChange(123);   // all notes off: fades out
    render(*kernel, frames(3.0));
    CHECK(!kernel->isSounding(), "all notes off did not release the voice");

    kernel->noteOn(40, 100);
    render(*kernel, frames(0.2));
    kernel->handleControlChange(120);   // all sound off: stops at once
    const auto output = render(*kernel, frames(0.01));
    CHECK(peak(output) == 0.0f, "all sound off left a peak of %f", peak(output));
}

// MARK: - Pitch and note handling

void testPlaysInTune() {
    for (double rate : { 44100.0, 48000.0, 96000.0, 192000.0 }) {
        auto kernel = makeKernel(rate);
        setUpForPitch(*kernel, 45.0);
        kernel->noteOn(45, 100);
        const auto output = render(*kernel, frames(1.0, rate));
        const double measured = measuredFrequency(output, frames(0.2, rate), output.size(), rate);
        CHECK(std::fabs(measured - 110.0) < 0.5, "A2 at %.0f Hz sample rate measured %f Hz", rate, measured);
        CHECK(peak(output) <= 1.0f, "peak %f at %.0f Hz sample rate", peak(output), rate);
    }
}

void testNewestNoteWinsAndOlderKeyReturns() {
    auto kernel = makeKernel();
    setUpForPitch(*kernel, 40.0);
    kernel->setParameter(cutoff, 300.0f);

    kernel->noteOn(40, 100);
    render(*kernel, frames(0.2));
    kernel->noteOn(47, 100);
    const auto newest = render(*kernel, frames(0.5));
    const double newestHz = measuredFrequency(newest, frames(0.1), newest.size());
    CHECK(std::fabs(newestHz - noteHertz(47)) < 0.5, "newest key should sound: measured %f Hz, expected %f",
          newestHz, noteHertz(47));

    kernel->noteOff(47);
    CHECK(kernel->isSounding(), "releasing the newest key stopped the voice while another was held");
    const auto returned = render(*kernel, frames(0.5));
    const double returnedHz = measuredFrequency(returned, frames(0.1), returned.size());
    CHECK(std::fabs(returnedHz - noteHertz(40)) < 0.5, "should return to the held key: measured %f Hz, expected %f",
          returnedHz, noteHertz(40));
    CHECK(rms(returned, 0, frames(0.02)) > 0.05, "gap when returning to the held key");

    // Releasing a key that isn't sounding changes nothing.
    kernel->noteOn(47, 100);
    kernel->noteOff(40);
    CHECK(kernel->currentPitch() == 47.0, "releasing a background key moved the pitch to %f", kernel->currentPitch());
}

void testLegatoNotesDoNotRestartTheEnvelope() {
    auto kernel = makeKernel();
    setUpForPitch(*kernel, 40.0);
    kernel->setParameter(cutoff, 2000.0f);
    kernel->setParameter(ampAttack, 1.0f);
    kernel->setParameter(ampDecay, 30.0f);
    kernel->setParameter(ampSustain, 20.0f);

    kernel->noteOn(40, 100);
    const auto first = render(*kernel, frames(0.4));
    const double settled = rms(first, frames(0.3), frames(0.4));

    // Overlapping note: the envelope stays at its sustain level.
    kernel->noteOn(43, 100);
    const auto legato = render(*kernel, frames(0.03));
    CHECK(rms(legato) < settled * 1.6, "legato note restarted the envelope: %f RMS against %f sustained",
          rms(legato), settled);

    // A fresh note after letting go attacks again, well above the sustain level.
    kernel->noteOff(43);
    kernel->noteOff(40);
    render(*kernel, frames(0.02));
    kernel->noteOn(45, 100);
    const auto fresh = render(*kernel, frames(0.03));
    CHECK(rms(fresh, 0, frames(0.01)) > settled * 2.0, "fresh note did not attack: %f RMS against %f sustained",
          rms(fresh, 0, frames(0.01)), settled);
}

void testGlideModes() {
    // Legato mode, overlapping notes: a straight line in pitch over the glide time.
    {
        auto kernel = makeKernel();
        kernel->setParameter(glideTime, 100.0f);
        kernel->setParameter(glideMode, 0.0f);
        kernel->noteOn(40, 100);
        render(*kernel, frames(0.1));
        kernel->noteOn(52, 100);
        render(*kernel, frames(0.05));
        CHECK(std::fabs(kernel->currentPitch() - 46.0) < 0.05, "halfway through the glide the pitch is %f, expected 46",
              kernel->currentPitch());
        render(*kernel, frames(0.06));
        CHECK(kernel->currentPitch() == 52.0, "glide did not land on the note: %f", kernel->currentPitch());
    }
    // Legato mode, separate notes: no glide.
    {
        auto kernel = makeKernel();
        kernel->setParameter(glideTime, 100.0f);
        kernel->setParameter(glideMode, 0.0f);
        kernel->noteOn(40, 100);
        render(*kernel, frames(0.1));
        kernel->noteOff(40);
        kernel->noteOn(52, 100);
        CHECK(kernel->currentPitch() == 52.0, "legato mode glided between separate notes: %f", kernel->currentPitch());
    }
    // Always mode, separate notes: glides.
    {
        auto kernel = makeKernel();
        kernel->setParameter(glideTime, 100.0f);
        kernel->setParameter(glideMode, 1.0f);
        kernel->noteOn(40, 100);
        render(*kernel, frames(0.1));
        kernel->noteOff(40);
        kernel->noteOn(52, 100);
        render(*kernel, frames(0.05));
        CHECK(std::fabs(kernel->currentPitch() - 46.0) < 0.05, "always mode did not glide: %f", kernel->currentPitch());
    }
    // The very first note has nowhere to glide from.
    {
        auto kernel = makeKernel();
        kernel->setParameter(glideTime, 100.0f);
        kernel->setParameter(glideMode, 1.0f);
        kernel->noteOn(52, 100);
        CHECK(kernel->currentPitch() == 52.0, "first note glided in from %f", kernel->currentPitch());
    }
}

void testPitchBend() {
    auto kernel = makeKernel();
    setUpForPitch(*kernel, 45.0);
    kernel->setParameter(cutoff, 250.0f);
    kernel->setParameter(bendRange, 2.0f);
    kernel->noteOn(45, 100);
    kernel->setPitchBend(1.0);
    const auto output = render(*kernel, frames(1.0));
    const double measured = measuredFrequency(output, frames(0.2), output.size());
    CHECK(std::fabs(measured - noteHertz(47)) < 0.5, "full bend up by 2 measured %f Hz, expected %f",
          measured, noteHertz(47));
}

// MARK: - Tone

void testSubOscillatorOctaves() {
    auto measure = [](float level, float octave, double frequency) {
        auto kernel = makeKernel();
        kernel->setParameter(subLevel, level);
        kernel->setParameter(subOctave, octave);
        kernel->setParameter(cutoff, 20000.0f);
        kernel->setParameter(resonance, 0.0f);
        kernel->noteOn(45, 100);   // 110 Hz
        const auto output = render(*kernel, frames(1.0));
        return energyAt(output, frames(0.2), output.size(), frequency);
    };
    const double without = measure(0.0f, 0.0f, 55.0);
    const double oneDown = measure(100.0f, 0.0f, 55.0);
    const double twoDown = measure(100.0f, 1.0f, 27.5);
    CHECK(oneDown > without * 50.0 + 1e-4, "sub one octave down adds too little at 55 Hz: %g against %g", oneDown, without);
    CHECK(twoDown > without * 50.0 + 1e-4, "sub two octaves down adds too little at 27.5 Hz: %g", twoDown);
}

void testCutoffDarkensTheSound() {
    auto brightness = [](float hertz, float mode) {
        auto kernel = makeKernel();
        kernel->setParameter(cutoff, hertz);
        kernel->setParameter(filterMode, mode);
        kernel->setParameter(subLevel, 0.0f);
        kernel->noteOn(45, 100);
        const auto output = render(*kernel, frames(0.5));
        // Energy in the sample-to-sample difference rises with high-frequency content.
        std::vector<float> difference(output.size());
        for (size_t index = 1; index < output.size(); ++index) {
            difference[index] = output[index] - output[index - 1];
        }
        return rms(difference, frames(0.1)) / std::max(1e-9, rms(output, frames(0.1)));
    };
    CHECK(brightness(200.0f, 0.0f) < brightness(5000.0f, 0.0f) * 0.5, "low-pass cutoff barely changes the tone");
    CHECK(brightness(2000.0f, 1.0f) > brightness(2000.0f, 0.0f), "band-pass is not brighter than low-pass at the same cutoff");
}

void testExtremesStayBoundedAndFinite() {
    auto kernel = makeKernel();
    kernel->setParameter(outputLevel, 6.0f);
    kernel->setParameter(oscShape, 100.0f);
    kernel->setParameter(subLevel, 100.0f);
    kernel->setParameter(resonance, 100.0f);
    kernel->setParameter(bendRange, 24.0f);
    kernel->noteOn(33, 127);
    kernel->setPitchBend(-1.0);

    float highest = 0.0f;
    bool finite = true;
    for (float hertz : { 20.0f, 55.0f, 110.0f, 440.0f, 3000.0f, 20000.0f }) {
        kernel->setParameter(cutoff, hertz);
        for (float mode : { 0.0f, 1.0f }) {
            kernel->setParameter(filterMode, mode);
            const auto output = render(*kernel, frames(0.3));
            highest = std::max(highest, peak(output));
            finite = finite && allFinite(output);
        }
    }
    kernel->noteOn(127, 127);
    kernel->setPitchBend(1.0);
    const auto high = render(*kernel, frames(0.3));
    highest = std::max(highest, peak(high));
    finite = finite && allFinite(high);

    CHECK(highest <= 1.0f, "output left full scale: peak %f", highest);
    CHECK(finite, "a non-finite sample was produced at the extremes");
}

void testMonoOutput() {
    auto kernel = makeKernel();
    kernel->noteOn(40, 100);
    const auto output = render(*kernel, frames(0.3), 1);
    CHECK(rms(output, frames(0.1)) > 0.05, "mono output is too quiet: %f RMS", rms(output, frames(0.1)));
}

} // namespace

int main() {
    testDefaultsMatchParameterTree();
    testParametersRoundTrip();
    testSilentWithoutNotes();
    testNotePlaysAndReleasesToSilence();
    testAllNotesOffAndAllSoundOff();
    testPlaysInTune();
    testNewestNoteWinsAndOlderKeyReturns();
    testLegatoNotesDoNotRestartTheEnvelope();
    testGlideModes();
    testPitchBend();
    testSubOscillatorOctaves();
    testCutoffDarkensTheSound();
    testExtremesStayBoundedAndFinite();
    testMonoOutput();

    std::printf("%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
