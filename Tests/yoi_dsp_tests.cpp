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
#include <array>
#include <memory>
#include <string>
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

/// A kernel prepared the way the audio unit prepares one. Unless asked for, the drawn envelope
/// (Amount 0) and the downsampler (Off) are taken out, so each test hears only what it is testing.
std::unique_ptr<Kernel> makeKernel(double sampleRate = kSampleRate, bool withEnvelope = false,
                                   bool withDownsampler = false, bool withCleanup = false) {
    auto kernel = std::make_unique<Kernel>();
    kernel->initialize(2, sampleRate);
    if (!withEnvelope) {
        kernel->setParameter(envAmount, 0.0f);
    }
    if (!withDownsampler) {
        kernel->setParameter(dsMode, 0.0f);
    }
    if (!withCleanup) {
        kernel->setParameter(cleanupMode, 0.0f);
    }
    return kernel;
}

int frames(double seconds, double sampleRate = kSampleRate) {
    return int(seconds * sampleRate);
}

/// Renders in host-sized blocks and returns the first channel. Fails the run if any channel
/// differs from the first, since the voice is mono. `sampleTime`, when given, is the running
/// sample count the host would pass, and is advanced.
std::vector<float> render(Kernel& kernel, int frameCount, int channels = 2, int block = 256,
                          int64_t* sampleTime = nullptr) {
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
        const int64_t now = (sampleTime != nullptr) ? *sampleTime : 0;
        kernel.process(std::span<float*>(pointers.data(), pointers.size()), now, uint32_t(count));
        if (sampleTime != nullptr) {
            *sampleTime += count;
        }
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
/// X-OVER goes to its lowest, because the oscillator gives up everything below it even with no
/// sub, and the fundamental is what the pitch is counted from.
void setUpForPitch(Kernel& kernel, double note) {
    kernel.setParameter(oscShape, 0.0f);
    kernel.setParameter(subLevel, 0.0f);
    kernel.setParameter(subCrossover, 50.0f);
    kernel.setParameter(resonance, 0.0f);
    kernel.setParameter(cutoff, float(noteHertz(note) * 1.5));
    kernel.setParameter(glideTime, 0.0f);
}

// MARK: - Parameters

void testDefaultsMatchParameterTree() {
    // The same values Parameters.swift gives hosts, on an untouched kernel.
    auto kernel = std::make_unique<Kernel>();
    kernel->initialize(2, kSampleRate);
    struct Expected { AUParameterAddress address; float value; };
    const Expected expected[] = {
        { outputLevel, 0.0f }, { glideTime, 60.0f }, { glideMode, 0.0f }, { bendRange, 2.0f },
        { oscShape, 0.0f }, { subLevel, 75.0f }, { subShape, 0.0f }, { subOctave, 0.0f },
        { subCrossover, 130.0f }, { filterMode, 0.0f }, { cutoff, 800.0f }, { resonance, 30.0f },
        { ampAttack, 3.0f }, { ampDecay, 300.0f }, { ampSustain, 100.0f }, { ampRelease, 150.0f },
        { envAmount, 3.0f }, { envTimeMode, 0.0f }, { envSyncLength, 6.0f }, { envFreeTime, 500.0f },
        { envDirection, 0.0f }, { envRetrigger, 0.0f }, { accelStart, 0.25f }, { accelEnd, 2.0f },
        { accelCurve, 0.0f }, { dsMode, 1.0f }, { dsRate, 1400.0f }, { dsAmount, 70.0f },
        { foldAmount, 0.0f }, { foldPosition, 2.0f }, { cleanupMode, 1.0f }, { cleanupMultiple, 16.0f },
        { boostAmount, 0.0f }, { ottDepth, 0.0f }, { widthAmount, 0.0f }, { ottTime, 50.0f },
        { ottUpward, 100.0f }, { filterMirror, 0.0f }, { filterDrive, 0.0f }, { dsLock, 0.0f }, { macroVoice, 0.0f }, { macroThroat, 100.0f }, { macroPower, 0.0f },
        { macroControl, 0.0f }, { macroWidth, 0.0f },
    };
    for (const auto& item : expected) {
        const float actual = kernel->getParameter(item.address);
        CHECK(std::fabs(actual - item.value) < 1e-4f, "address %llu defaults to %f, expected %f",
              (unsigned long long)item.address, actual, item.value);
    }
}

void testRangesClamp() {
    // The owner's ranges: the filter only needs to reach 2500 Hz, and S&H lives at 1300-6000 Hz.
    auto kernel = makeKernel();
    kernel->setParameter(cutoff, 20000.0f);
    CHECK(kernel->getParameter(cutoff) == 2500.0f, "cutoff should stop at 2500 Hz, got %f", kernel->getParameter(cutoff));
    kernel->setParameter(dsRate, 200.0f);
    CHECK(kernel->getParameter(dsRate) == 1300.0f, "S&H rate should start at 1300 Hz, got %f", kernel->getParameter(dsRate));
    kernel->setParameter(dsRate, 12000.0f);
    CHECK(kernel->getParameter(dsRate) == 6000.0f, "S&H rate should stop at 6000 Hz, got %f", kernel->getParameter(dsRate));
    // The crossover keeps the Max device's range.
    kernel->setParameter(subCrossover, 10.0f);
    CHECK(kernel->getParameter(subCrossover) == 50.0f, "crossover should start at 50 Hz, got %f", kernel->getParameter(subCrossover));
    kernel->setParameter(subCrossover, 5000.0f);
    CHECK(kernel->getParameter(subCrossover) == 700.0f, "crossover should stop at 700 Hz, got %f", kernel->getParameter(subCrossover));
    // The fold's whole throw is 0-5 %: past that it only gets worse.
    kernel->setParameter(foldAmount, 60.0f);
    CHECK(std::fabs(kernel->getParameter(foldAmount) - 5.0f) < 1e-4f, "fold should stop at 5 %%, got %f", kernel->getParameter(foldAmount));
}

void testParametersRoundTrip() {
    auto kernel = makeKernel();
    struct Item { AUParameterAddress address; float value; };
    const Item items[] = {
        { outputLevel, -12.0f }, { glideTime, 250.0f }, { glideMode, 1.0f }, { bendRange, 12.0f },
        { oscShape, 40.0f }, { subLevel, 75.0f }, { subShape, 60.0f }, { subOctave, 1.0f },
        { subCrossover, 300.0f }, { filterMode, 1.0f }, { cutoff, 1234.0f }, { resonance, 85.0f },
        { ampAttack, 20.0f }, { ampDecay, 900.0f }, { ampSustain, 40.0f }, { ampRelease, 700.0f },
        { envAmount, 5.5f }, { envTimeMode, 1.0f }, { envSyncLength, 17.0f }, { envFreeTime, 1234.0f },
        { envDirection, 5.0f }, { envRetrigger, 1.0f }, { accelStart, 0.5f }, { accelEnd, 3.0f },
        { accelCurve, -0.4f }, { dsMode, 2.0f }, { dsRate, 2500.0f }, { dsAmount, 70.0f },
        { foldAmount, 3.5f }, { foldPosition, 1.0f }, { cleanupMode, 0.0f }, { cleanupMultiple, 7.5f },
        { boostAmount, 40.0f }, { ottDepth, 60.0f }, { widthAmount, 80.0f }, { ottTime, 20.0f },
        { ottUpward, 150.0f }, { filterMirror, 55.0f }, { filterDrive, 30.0f }, { dsLock, 1.0f },
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
        kernel->setParameter(cutoff, 2500.0f);
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
    CHECK(brightness(200.0f, 0.0f) < brightness(2500.0f, 0.0f) * 0.5, "low-pass cutoff barely changes the tone");
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
    for (float hertz : { 20.0f, 55.0f, 110.0f, 440.0f, 1200.0f, 2500.0f }) {
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

// MARK: - Drawn curve

std::vector<float> renderTable(std::vector<bdd::CurvePoint> points) {
    std::array<bdd::CurvePoint, bdd::kMaxCurvePoints> clean{};
    const int count = bdd::sanitizeCurve(points.data(), int(points.size()), clean.data());
    std::vector<float> table(static_cast<size_t>(bdd::kCurveTableSize));
    bdd::renderCurveTable(clean.data(), count, table.data(), int(table.size()));
    return table;
}

void testCurveRendering() {
    const auto straight = renderTable({ { 0, 0, 0 }, { 1, 1, 0 } });
    CHECK(std::fabs(straight[512] - 0.5f) < 1e-6f, "straight line midpoint is %f", straight[512]);
    CHECK(straight[0] == 0.0f && straight[1024] == 1.0f, "straight line ends are %f and %f", straight[0], straight[1024]);
    CHECK(std::fabs(bdd::lookup(straight.data(), bdd::kCurveTableSize, 0.3) - 0.3f) < 1e-5f, "lookup does not interpolate");

    const auto slowStart = renderTable({ { 0, 0, 0.5f }, { 1, 1, 0 } });
    const auto fastStart = renderTable({ { 0, 0, -0.5f }, { 1, 1, 0 } });
    CHECK(slowStart[512] < 0.4f, "positive bend should start slowly: midpoint %f", slowStart[512]);
    CHECK(fastStart[512] > 0.6f, "negative bend should start fast: midpoint %f", fastStart[512]);
    CHECK(slowStart[1024] == 1.0f && fastStart[1024] == 1.0f, "bent segments must still reach their end point");

    const auto gate = renderTable({ { 0, 1, 0 }, { 0.5f, 1, 0 }, { 0.5f, 0, 0 }, { 1, 0, 0 } });
    CHECK(gate[511] == 1.0f && gate[512] == 0.0f, "a vertical jump should land exactly at its x: %f, %f", gate[511], gate[512]);
}

void testCurveSanitising() {
    std::array<bdd::CurvePoint, bdd::kMaxCurvePoints> out{};

    bdd::CurvePoint messy[] = { { 0.8f, 2.0f, 0.0f }, { 0.2f, -1.0f, 5.0f }, { 0.5f, 0.5f, 0.0f } };
    int count = bdd::sanitizeCurve(messy, 3, out.data());
    CHECK(count == 3, "kept %d of 3 points", count);
    CHECK(out[0].x == 0.0f && out[1].x == 0.5f && out[2].x == 1.0f, "not sorted and pinned: %f %f %f", out[0].x, out[1].x, out[2].x);
    CHECK(out[0].y == 0.0f && out[2].y == 1.0f, "heights not clamped: %f, %f", out[0].y, out[2].y);
    CHECK(out[0].bend == 1.0f, "bend not clamped: %f", out[0].bend);

    const float missing = std::nanf("");
    bdd::CurvePoint withNaN[] = { { 0, 0, 0 }, { missing, 0.5f, 0 }, { 1, 1, 0 } };
    CHECK(bdd::sanitizeCurve(withNaN, 3, out.data()) == 2, "a non-finite point was kept");

    bdd::CurvePoint single[] = { { 0.3f, 0.7f, 0.0f } };
    count = bdd::sanitizeCurve(single, 1, out.data());
    CHECK(count == 2 && out[0].y == 0.7f && out[1].y == 0.7f, "one point should become a flat line at its height");
    count = bdd::sanitizeCurve(nullptr, 5, out.data());
    CHECK(count == 2 && out[0].y == 0.5f, "no points should become a flat line at half height");

    std::vector<bdd::CurvePoint> many(200);
    for (size_t i = 0; i < many.size(); ++i) {
        many[i] = { float(i) / 199.0f, 0.5f, 0.0f };
    }
    CHECK(bdd::sanitizeCurve(many.data(), 200, out.data()) == bdd::kMaxCurvePoints, "point count not capped");
}

void testDirections() {
    using D = bdd::EnvelopeDirection;
    const bdd::AccelerateSettings defaults{};
    auto read = [&](D direction, double phase, int64_t cycle = 3) {
        return bdd::readPosition(direction, cycle, phase, 42, defaults);
    };

    CHECK(read(D::forward, 0.25) == 0.25, "forward");
    CHECK(read(D::backward, 0.25) == 0.75, "backward");
    CHECK(read(D::pingpong, 0.25) == 0.5 && read(D::pingpong, 0.5) == 1.0 && read(D::pingpong, 0.75) == 0.5,
          "pingpong should reach the end halfway and come back");
    CHECK(std::fabs(read(D::sine, 0.0)) < 1e-12 && std::fabs(read(D::sine, 0.5) - 1.0) < 1e-12,
          "sine should start at the beginning and reach the end halfway");

    CHECK(std::fabs(read(D::random, 1.0, 3) - read(D::random, 0.0, 4)) < 1e-12, "random jumps between passes");
    double lowest = 1.0;
    double highest = 0.0;
    for (int64_t cycle = 0; cycle < 100; ++cycle) {
        lowest = std::min(lowest, bdd::randomPoint(42, cycle));
        highest = std::max(highest, bdd::randomPoint(42, cycle));
    }
    CHECK(lowest < 0.1 && highest > 0.9, "random points only cover %f to %f", lowest, highest);

    const bdd::AccelerateSettings steady{ 1.0, 1.0, 0.0 };
    CHECK(std::fabs(bdd::readPosition(D::accelerate, 0, 0.3, 42, steady) - 0.3) < 1e-12, "accelerate at 1x should read like forward");
    CHECK(std::fabs(bdd::accelerateTravel(1.0, defaults) - 1.125) < 1e-12, "0.25x to 2x should travel 1.125 drawings per pass, got %f",
          bdd::accelerateTravel(1.0, defaults));
    CHECK(std::fabs(read(D::accelerate, 1.0 - 1e-12) - 0.125) < 1e-6, "accelerate should wrap round the drawing");
    CHECK(bdd::accelerateTravel(0.5, defaults) < bdd::accelerateTravel(1.0, defaults) - bdd::accelerateTravel(0.5, defaults),
          "accelerate should cover less ground in its first half than its second");
    const bdd::AccelerateSettings bent{ 0.25, 2.0, 1.0 };
    CHECK(bdd::accelerateTravel(0.5, bent) < bdd::accelerateTravel(0.5, defaults), "a positive curve should hold the speed down for longer");
}

void testSyncLengths() {
    CHECK(Kernel::syncLengthCount() == 24, "%d sync lengths", Kernel::syncLengthCount());
    CHECK(bdd::syncLengthInQuarterNotes(9, 4, 4) == 1.0, "1/4 should be one beat");
    CHECK(bdd::syncLengthInQuarterNotes(15, 4, 4) == 4.0, "a bar of 4/4 should be four beats");
    CHECK(bdd::syncLengthInQuarterNotes(15, 3, 4) == 3.0, "a bar of 3/4 should be three beats");
    CHECK(bdd::syncLengthInQuarterNotes(15, 6, 8) == 3.0, "a bar of 6/8 should be three beats");
    CHECK(bdd::syncLengthInQuarterNotes(6, 3, 4) == 0.5, "note values should not scale with the time signature");
    CHECK(std::string(Kernel::syncLengthName(6)) == "1/8", "sync option 6 is %s", Kernel::syncLengthName(6));
    CHECK(std::string(Kernel::directionName(5)) == "Accelerate", "direction 5 is %s", Kernel::directionName(5));
}

// MARK: - Envelope timing

/// A kernel with the drawn envelope on, synced to sync option `syncIndex`.
std::unique_ptr<Kernel> makeEnvelopeKernel(int syncIndex) {
    auto kernel = makeKernel(kSampleRate, true);
    kernel->setParameter(envSyncLength, float(syncIndex));
    return kernel;
}

void testEnvelopeFollowsHostPosition() {
    auto kernel = makeEnvelopeKernel(9);   // 1/4: one pass per beat
    int64_t time = 0;
    kernel->setHostTiming(120.0, 0.0, 0, true, 4, 4);
    kernel->noteOn(40, 100);
    render(*kernel, 12000, 2, 250, &time);   // 0.25 s: half a beat at 120 BPM
    CHECK(std::fabs(kernel->envelopeDisplayPosition() - 0.5f) < 0.001f, "half a beat in, position is %f",
          kernel->envelopeDisplayPosition());

    // The host jumps (a loop, or the playhead moved): the envelope follows at once.
    kernel->setHostTiming(120.0, 2.75, time, true, 4, 4);
    render(*kernel, 250, 2, 250, &time);
    const double expected = 0.75 + 249.0 / 24000.0;
    CHECK(std::fabs(kernel->envelopeDisplayPosition() - expected) < 0.001, "after a jump to beat 2.75, position is %f, expected %f",
          kernel->envelopeDisplayPosition(), expected);
}

void testEnvelopeRunsAtTempoWhenStopped() {
    auto kernel = makeEnvelopeKernel(9);
    kernel->setParameter(envRetrigger, 1.0f);   // start from the note, so the position is predictable
    kernel->setHostTiming(90.0, 0.0, 0, false, 4, 4);
    kernel->noteOn(40, 100);
    render(*kernel, 12000, 2, 250);
    const double expected = 11999.0 * (90.0 / 60.0 / kSampleRate);
    CHECK(std::fabs(kernel->envelopeDisplayPosition() - expected) < 0.001, "at 90 BPM with the transport stopped, position is %f, expected %f",
          kernel->envelopeDisplayPosition(), expected);
}

void testFreeTime() {
    auto kernel = makeKernel(kSampleRate, true);
    kernel->setParameter(envTimeMode, 1.0f);
    kernel->setParameter(envFreeTime, 500.0f);
    kernel->setParameter(envRetrigger, 1.0f);
    kernel->noteOn(40, 100);
    render(*kernel, 12000, 2, 250);
    CHECK(std::fabs(kernel->envelopeDisplayPosition() - 0.5f) < 0.001f, "250 ms into a 500 ms envelope, position is %f",
          kernel->envelopeDisplayPosition());
}

void testRetrigger() {
    auto setUp = [](float retrigger) {
        auto kernel = makeKernel(kSampleRate, true);
        kernel->setParameter(envTimeMode, 1.0f);
        kernel->setParameter(envFreeTime, 1000.0f);
        kernel->setParameter(envRetrigger, retrigger);
        kernel->noteOn(40, 100);
        render(*kernel, 18000, 2, 250);   // 0.375 s in
        return kernel;
    };

    {
        auto kernel = setUp(1.0f);
        kernel->noteOn(43, 100);   // overlapping
        render(*kernel, 250, 2, 250);
        CHECK(kernel->envelopeDisplayPosition() > 0.37f, "an overlapping note restarted the envelope: %f",
              kernel->envelopeDisplayPosition());
        kernel->noteOff(43);
        kernel->noteOff(40);
        render(*kernel, 250, 2, 250);
        kernel->noteOn(45, 100);   // fresh
        render(*kernel, 250, 2, 250);
        CHECK(kernel->envelopeDisplayPosition() < 0.01f, "with Re-Trigger on, a fresh note should restart the drawing: %f",
              kernel->envelopeDisplayPosition());
    }
    {
        auto kernel = setUp(0.0f);
        kernel->noteOff(40);
        render(*kernel, 250, 2, 250);
        kernel->noteOn(45, 100);
        render(*kernel, 250, 2, 250);
        CHECK(kernel->envelopeDisplayPosition() > 0.37f, "with Re-Trigger off, the envelope should carry on: %f",
              kernel->envelopeDisplayPosition());
    }
}

void testEnvelopeRunsThroughRelease() {
    auto kernel = makeKernel(kSampleRate, true);
    kernel->setParameter(envTimeMode, 1.0f);
    kernel->setParameter(envFreeTime, 1000.0f);
    kernel->setParameter(ampRelease, 1000.0f);
    kernel->noteOn(40, 100);
    render(*kernel, 4800);
    kernel->noteOff(40);
    const float before = kernel->envelopeDisplayPosition();
    render(*kernel, 4800);
    CHECK(kernel->isSounding(), "the voice should still be releasing");
    CHECK(kernel->envelopeDisplayPosition() > before + 0.09f, "the envelope stopped in the release: %f then %f",
          before, kernel->envelopeDisplayPosition());
}

void testRandomRepeatsWithTheSong() {
    auto run = []() {
        auto kernel = makeEnvelopeKernel(4);   // 1/16
        kernel->setParameter(envDirection, 4.0f);
        int64_t time = 0;
        kernel->setHostTiming(128.0, 16.0, 0, true, 4, 4);
        kernel->noteOn(40, 100);
        std::vector<float> positions;
        for (int block = 0; block < 40; ++block) {
            render(*kernel, 256, 2, 256, &time);
            positions.push_back(kernel->envelopeDisplayPosition());
        }
        return positions;
    };
    CHECK(run() == run(), "Random should repeat exactly when the song plays from the same place");
}

// MARK: - Envelope on the filter

void setFlatDrawing(Kernel& kernel, float level) {
    const float xs[] = { 0.0f, 1.0f };
    const float ys[] = { level, level };
    kernel.setEnvelopeCurve(xs, ys, nullptr, 2);
}

double brightness(const std::vector<float>& output, size_t from) {
    std::vector<float> difference(output.size());
    for (size_t index = 1; index < output.size(); ++index) {
        difference[index] = output[index] - output[index - 1];
    }
    return rms(difference, from) / std::max(1e-9, rms(output, from));
}

void testEnvelopeMovesTheCutoff() {
    auto play = [](float level, float amount) {
        auto kernel = makeKernel(kSampleRate, true);
        kernel->setParameter(subLevel, 0.0f);
        kernel->setParameter(cutoff, 2500.0f);
        kernel->setParameter(envAmount, amount);
        setFlatDrawing(*kernel, level);
        render(*kernel, 256);   // taken up while silent, so there is no crossfade to hear
        kernel->noteOn(45, 100);
        return render(*kernel, frames(0.4));
    };
    const auto top = play(1.0f, 4.0f);
    const auto bottom = play(0.0f, 4.0f);
    CHECK(brightness(bottom, frames(0.1)) < brightness(top, frames(0.1)) * 0.5,
          "the bottom of the drawing should close the filter: brightness %f against %f",
          brightness(bottom, frames(0.1)), brightness(top, frames(0.1)));

    const auto noAmountTop = play(1.0f, 0.0f);
    const auto noAmountBottom = play(0.0f, 0.0f);
    CHECK(noAmountTop == noAmountBottom, "with Amount at 0 the drawing should change nothing");
    CHECK(top == noAmountTop, "the top of the drawing should sit exactly on CUTOFF");
}

void testRedrawingCrossfades() {
    auto kernel = makeKernel(kSampleRate, true);
    setFlatDrawing(*kernel, 0.0f);
    render(*kernel, 256);
    kernel->noteOn(40, 100);
    render(*kernel, 256);
    CHECK(kernel->envelopeDisplayValue() == 0.0f, "flat drawing at 0 reads %f", kernel->envelopeDisplayValue());

    setFlatDrawing(*kernel, 1.0f);
    render(*kernel, 256);   // about 5 ms into a 20 ms fade
    const float partway = kernel->envelopeDisplayValue();
    CHECK(partway > 0.1f && partway < 0.5f, "a redrawn envelope should fade in over about 20 ms, but read %f after 5 ms", partway);
    render(*kernel, frames(0.03));
    CHECK(kernel->envelopeDisplayValue() == 1.0f, "the fade did not finish: %f", kernel->envelopeDisplayValue());
}

void testFactoryShapes() {
    auto kernel = makeKernel();
    CHECK(kernel->envelopePointCount() == 5, "a new kernel should start on the mock-up drawing, has %d points",
          kernel->envelopePointCount());
    CHECK(std::string(kernel->factoryShapeName(0)) == "Mock-up", "first shape is %s", kernel->factoryShapeName(0));

    for (int index = 0; index < kernel->factoryShapeCount(); ++index) {
        kernel->loadFactoryShape(index);
        const int count = kernel->envelopePointCount();
        CHECK(count >= 2 && kernel->envelopePointX(0) == 0.0f && kernel->envelopePointX(count - 1) == 1.0f,
              "factory shape %s is malformed", kernel->factoryShapeName(index));
        std::vector<float> table(64);
        kernel->copyEnvelopeTable(table.data(), int(table.size()));
        CHECK(std::all_of(table.begin(), table.end(), [](float value) { return value >= 0.0f && value <= 1.0f; }),
              "factory shape %s leaves 0...1", kernel->factoryShapeName(index));
    }

    kernel->setEnvelopeCurve(nullptr, nullptr, nullptr, 5);
    CHECK(kernel->envelopePointCount() == 2 && kernel->envelopePointY(0) == 0.5f, "missing points should give a flat line");
}

void testEnvelopeExtremesStayBounded() {
    auto kernel = makeKernel(kSampleRate, true, true);
    kernel->setParameter(outputLevel, 6.0f);
    kernel->setParameter(resonance, 100.0f);
    kernel->setParameter(cutoff, 2500.0f);
    kernel->setParameter(envAmount, 8.0f);
    kernel->setParameter(envTimeMode, 1.0f);
    kernel->setParameter(envFreeTime, 10.0f);
    kernel->setParameter(accelStart, 4.0f);
    kernel->setParameter(accelEnd, 4.0f);
    kernel->loadFactoryShape(4);   // Gate: hard jumps
    kernel->noteOn(33, 127);

    float highest = 0.0f;
    bool finite = true;
    for (int direction = 0; direction < Kernel::directionCount(); ++direction) {
        kernel->setParameter(envDirection, float(direction));
        const auto output = render(*kernel, frames(0.2));
        highest = std::max(highest, peak(output));
        finite = finite && allFinite(output);
    }
    CHECK(highest <= 1.0f, "fast, deep envelope left full scale: peak %f", highest);
    CHECK(finite, "fast, deep envelope produced a non-finite sample");
}

// MARK: - Downsampler

/// Lengths of the runs of identical samples between `from` and `to`, leaving out the first and
/// last runs, which may be cut short by the window.
std::vector<int> holdRuns(const std::vector<float>& samples, size_t from, size_t to) {
    std::vector<int> runs;
    int length = 1;
    for (size_t index = from + 1; index < to && index < samples.size(); ++index) {
        if (samples[index] == samples[index - 1]) {
            ++length;
        } else {
            runs.push_back(length);
            length = 1;
        }
    }
    if (!runs.empty()) {
        runs.erase(runs.begin());
    }
    return runs;
}

double averageOf(const std::vector<int>& values) {
    double sum = 0.0;
    for (int value : values) {
        sum += value;
    }
    return values.empty() ? 0.0 : sum / double(values.size());
}

void testSampleAndHold() {
    for (double rate : { 48000.0, 96000.0 }) {
        bdd::SampleAndHold hold;
        std::vector<float> output(48000);
        bool grabsInput = true;
        for (size_t index = 0; index < output.size(); ++index) {
            output[index] = hold.next(float(index), 1400.0, rate);
            if (index > 0 && output[index] != output[index - 1]) {
                grabsInput = grabsInput && output[index] == float(index);
            }
        }
        const auto runs = holdRuns(output, 0, output.size());
        const double expected = rate / 1400.0;
        const int shortest = *std::min_element(runs.begin(), runs.end());
        const int longest = *std::max_element(runs.begin(), runs.end());
        CHECK(std::fabs(averageOf(runs) - expected) < 0.01, "S&H at 1400 Hz and %.0f Hz holds for %f samples on average, expected %f",
              rate, averageOf(runs), expected);
        CHECK(shortest == int(expected) && longest == int(expected) + 1, "S&H holds should alternate %d and %d samples, got %d to %d",
              int(expected), int(expected) + 1, shortest, longest);
        CHECK(grabsInput, "S&H should hold the input sample it grabbed");
    }
}

void testSampleCountDownsampler() {
    auto runsFor = [](double factor) {
        bdd::SampleCountDownsampler downsampler;
        std::vector<float> output(20000);
        for (size_t index = 0; index < output.size(); ++index) {
            output[index] = downsampler.next(float(index), factor);
        }
        return std::make_pair(output, holdRuns(output, 0, output.size()));
    };

    const auto whole = runsFor(27.0).second;
    CHECK(*std::min_element(whole.begin(), whole.end()) == 27 && *std::max_element(whole.begin(), whole.end()) == 27,
          "a factor of 27 should hold exactly 27 samples");

    const auto fractional = runsFor(29.3878).second;
    CHECK(std::fabs(averageOf(fractional) - 29.3878) < 0.01, "a factor of 29.39 holds for %f on average", averageOf(fractional));
    CHECK(*std::min_element(fractional.begin(), fractional.end()) == 29 && *std::max_element(fractional.begin(), fractional.end()) == 30,
          "a fractional factor should alternate between the two nearest hold lengths");

    const auto [transparent, none] = runsFor(1.0);
    bool unchanged = true;
    for (size_t index = 0; index < transparent.size(); ++index) {
        unchanged = unchanged && transparent[index] == float(index);
    }
    CHECK(unchanged, "a factor of 1 should pass the input straight through");

    bdd::SampleCountDownsampler shortened;
    for (int index = 0; index < 30; ++index) {
        shortened.next(float(index), 60.0);
    }
    const float before = shortened.next(100.0f, 4.0);
    int wait = 1;
    while (shortened.next(float(200 + wait), 4.0) == before && wait < 100) {
        ++wait;
    }
    CHECK(wait <= 4, "shortening the hold mid-way should take effect at once, but took %d samples", wait);
}

/// A held note through the full voice with the downsampler set to `mode`, after the attack.
std::vector<float> playThroughDownsampler(float mode, double sampleRate = kSampleRate) {
    auto kernel = makeKernel(sampleRate, false, true);
    kernel->setParameter(dsMode, mode);
    kernel->setParameter(dsAmount, 45.0f);   // the amount these hold lengths were worked out for
    kernel->setParameter(subLevel, 0.0f);   // the sub joins after the downsampler, over its steps
    kernel->noteOn(40, 100);
    return render(*kernel, frames(0.5, sampleRate));
}

void testDownsamplerInTheVoice() {
    const auto off = playThroughDownsampler(0.0f);
    const auto offRuns = holdRuns(off, frames(0.1), off.size());
    CHECK(*std::max_element(offRuns.begin(), offRuns.end()) <= 2, "with the downsampler off the voice should not step");

    const auto sampleHold = playThroughDownsampler(1.0f);
    const auto sampleHoldRuns = holdRuns(sampleHold, frames(0.1), sampleHold.size());
    CHECK(std::fabs(averageOf(sampleHoldRuns) - kSampleRate / 1400.0) < 0.05, "S&H in the voice holds for %f samples, expected %f",
          averageOf(sampleHoldRuns), kSampleRate / 1400.0);

    // Downsample amount 45 is a 27-sample hold at 48 kHz, as in the Max device in the owner's
    // projects, and the same effective rate (so fewer or more samples) at 44.1 and 96 kHz.
    const auto at48 = playThroughDownsampler(2.0f, 48000.0);
    const auto runs48 = holdRuns(at48, frames(0.1), at48.size());
    CHECK(*std::min_element(runs48.begin(), runs48.end()) == 27 && *std::max_element(runs48.begin(), runs48.end()) == 27,
          "Downsample at amount 45 and 48 kHz should hold exactly 27 samples");
    for (double rate : { 44100.0, 96000.0 }) {
        const auto other = playThroughDownsampler(2.0f, rate);
        const auto runs = holdRuns(other, frames(0.1, rate), other.size());
        const double expected = 27.0 * rate / 48000.0;
        CHECK(std::fabs(averageOf(runs) - expected) < 0.05, "Downsample at %.0f Hz holds for %f, expected %f (the same rate as 27 at 48 kHz)",
              rate, averageOf(runs), expected);
    }
}

void testDownsamplerComesAfterTheFilter() {
    // A low cutoff leaves almost nothing above a few hundred hertz. If the downsampler runs after
    // the filter, its images of what remains come back in well above the cutoff; if it ran before,
    // the filter would take them out again.
    auto play = [](float mode) {
        auto kernel = makeKernel(kSampleRate, false, true);
        kernel->setParameter(dsMode, mode);
        kernel->setParameter(cutoff, 150.0f);
        kernel->setParameter(resonance, 60.0f);
        kernel->noteOn(40, 100);
        return render(*kernel, frames(0.5));
    };
    const auto off = play(0.0f);
    const auto sampleHold = play(1.0f);
    CHECK(brightness(sampleHold, frames(0.1)) > brightness(off, frames(0.1)) * 3.0,
          "the downsampler should add energy above the filter: brightness %f against %f",
          brightness(sampleHold, frames(0.1)), brightness(off, frames(0.1)));
}

void testDownsamplerModeSwitchSettles() {
    auto kernel = makeKernel(kSampleRate, false, true);
    kernel->setParameter(dsMode, 0.0f);
    kernel->setParameter(dsAmount, 45.0f);   // the amount these hold lengths were worked out for
    kernel->setParameter(subLevel, 0.0f);   // the sub joins after the downsampler, over its steps
    kernel->noteOn(40, 100);
    render(*kernel, frames(0.2));
    kernel->setParameter(dsMode, 1.0f);
    const auto switching = render(*kernel, frames(0.2));
    const auto early = holdRuns(switching, 0, frames(0.005));
    const auto settled = holdRuns(switching, frames(0.1), switching.size());
    CHECK(early.empty() || *std::max_element(early.begin(), early.end()) <= 2, "switching modes should fade in, not jump");
    CHECK(std::fabs(averageOf(settled) - kSampleRate / 1400.0) < 0.05, "after the fade, S&H should be fully in: %f", averageOf(settled));
}

void testDownsamplerExtremesStayBounded() {
    float highest = 0.0f;
    bool finite = true;
    for (float mode : { 1.0f, 2.0f }) {
        for (float setting : { 0.0f, 1.0f }) {
            auto kernel = makeKernel(kSampleRate, true, true);
            kernel->setParameter(dsMode, mode);
            kernel->setParameter(dsRate, setting == 0.0f ? 1300.0f : 6000.0f);
            kernel->setParameter(dsAmount, setting == 0.0f ? 100.0f : 0.0f);
            kernel->setParameter(resonance, 100.0f);
            kernel->setParameter(outputLevel, 6.0f);
            kernel->noteOn(28, 127);
            const auto output = render(*kernel, frames(0.3));
            highest = std::max(highest, peak(output));
            finite = finite && allFinite(output);
        }
    }
    CHECK(highest <= 1.0f, "downsampler extremes left full scale: peak %f", highest);
    CHECK(finite, "downsampler extremes produced a non-finite sample");
}

// MARK: - Wavefolder and clean-up filter

void testWavefolderCurve() {
    using F = bdd::Wavefolder;
    CHECK(std::fabs(F::fold(0.3) - 0.3) < 1e-12 && std::fabs(F::fold(-0.7) + 0.7) < 1e-12, "the fold should leave -1...1 untouched");
    CHECK(std::fabs(F::fold(1.5) - 0.5) < 1e-12 && std::fabs(F::fold(-1.5) + 0.5) < 1e-12, "1.5 should reflect to 0.5");
    CHECK(std::fabs(F::fold(3.0) + 1.0) < 1e-12 && std::fabs(F::fold(5.0) - 1.0) < 1e-12, "the fold should repeat every 4");
    double worst = 0.0;
    for (double x = -9.0; x <= 9.0; x += 0.0137) {
        const double h = 1e-5;
        const double slope = (F::integral(x + h) - F::integral(x - h)) / (2.0 * h);
        worst = std::max(worst, std::fabs(slope - F::fold(x)));
    }
    CHECK(worst < 1e-5, "the integral's slope should be the fold curve everywhere: worst error %g", worst);
}

/// Share of a signal's power that is not at a harmonic of `fundamental`: aliasing. The window
/// must hold a whole number of cycles, so each harmonic falls exactly on its own frequency.
double inharmonicShare(const std::vector<float>& samples, double fundamental) {
    double total = 0.0;
    for (float sample : samples) {
        total += double(sample) * double(sample);
    }
    total /= double(samples.size());
    double harmonic = 0.0;
    for (double frequency = fundamental; frequency < kSampleRate * 0.5; frequency += fundamental) {
        const double magnitude = energyAt(samples, 0, samples.size(), frequency);
        harmonic += 2.0 * magnitude * magnitude;   // a sinusoid's power from its normalised magnitude
    }
    return std::max(0.0, total - harmonic) / total;
}

void testWavefolderAntialiasing() {
    // A 2.9 kHz sine driven 8x into the fold, over exactly 2900 cycles. Everything that isn't a
    // harmonic of 2.9 kHz is aliasing: the fold's harmonics above Nyquist folding back.
    auto run = [](bool antialiased) {
        bdd::Wavefolder folder;
        std::vector<float> output(48000);
        for (size_t i = 0; i < output.size(); ++i) {
            const double x = 8.0 * std::sin(2.0 * kPi * 2900.0 * double(i) / kSampleRate);
            output[i] = float(antialiased ? folder.process(x) : bdd::Wavefolder::fold(x));
        }
        return output;
    };
    const auto naive = run(false);
    const auto smooth = run(true);
    const double naiveAliasing = inharmonicShare(naive, 2900.0);
    const double smoothAliasing = inharmonicShare(smooth, 2900.0);
    CHECK(smoothAliasing < naiveAliasing * 0.5, "anti-aliasing should at least halve the aliasing: %.2f %% against %.2f %% naive",
          100.0 * smoothAliasing, 100.0 * naiveAliasing);
    const double naiveThird = energyAt(naive, 0, naive.size(), 8700.0);
    const double smoothThird = energyAt(smooth, 0, smooth.size(), 8700.0);
    CHECK(smoothThird > naiveThird * 0.8, "anti-aliasing should keep the real 3rd harmonic: %g against %g", smoothThird, naiveThird);
}

std::vector<float> playFold(float amount, float position, float cutoffHertz, int frameCount = 24000) {
    auto kernel = makeKernel();
    kernel->setParameter(foldAmount, amount);
    kernel->setParameter(foldPosition, position);
    kernel->setParameter(cutoff, cutoffHertz);
    kernel->setParameter(resonance, 0.0f);
    kernel->noteOn(40, 100);
    return render(*kernel, frameCount);
}

void testFoldInTheVoice() {
    CHECK(playFold(0.0f, 0.0f, 800.0f) == playFold(0.0f, 1.0f, 800.0f), "at 0 %% the fold should be an exact bypass in either position");
    // The fold's throw is 0-5 % now, where it's subtle: at 5 % it leaves brightness within 0.1 %
    // and its uneven offset adds the 2nd harmonic (testPostDownsampleFold). What still shows:
    // after a low cutoff, folding pre-downsample keeps more top than pre-filter (x1.12 measured
    // at 5 %, 2026-09-29; it was x1.5 at the old 60 %).
    const double preDownsample = brightness(playFold(5.0f, 1.0f, 300.0f), 4800);
    const double preFilter = brightness(playFold(5.0f, 0.0f, 300.0f), 4800);
    CHECK(preDownsample > preFilter * 1.05, "pre-downsample folding should be brighter than pre-filter after a low cutoff: x%.3f",
          preDownsample / preFilter);
}

void testFoldPositionSwitchIsSmooth() {
    auto kernel = makeKernel();
    kernel->setParameter(foldAmount, 5.0f);
    kernel->setParameter(cutoff, 300.0f);
    kernel->setParameter(resonance, 0.0f);
    kernel->noteOn(40, 100);
    const auto before = render(*kernel, frames(0.2));
    kernel->setParameter(foldPosition, 1.0f);
    const auto during = render(*kernel, frames(0.03));
    const auto after = render(*kernel, frames(0.2));
    auto largestStep = [](const std::vector<float>& samples, size_t from) {
        float largest = 0.0f;
        for (size_t i = std::max<size_t>(from, 1); i < samples.size(); ++i) {
            largest = std::max(largest, std::fabs(samples[i] - samples[i - 1]));
        }
        return largest;
    };
    // Folding after the filter is brighter, so its steady steps are bigger: compare the switch
    // with the larger of the two settled sounds.
    const float steady = std::max(largestStep(before, frames(0.1)), largestStep(after, frames(0.1)));
    const float switching = std::max(largestStep(during, 0), std::fabs(during[0] - before.back()));
    CHECK(switching < steady * 1.5f, "moving the fold should fade, not jump: largest step %f against %f steady", switching, steady);
}

std::vector<float> playCleanup(float mode, float multiple) {
    auto kernel = makeKernel(kSampleRate, false, true, true);
    kernel->setParameter(cleanupMode, mode);
    kernel->setParameter(cleanupMultiple, multiple);
    kernel->setParameter(resonance, 70.0f);
    kernel->noteOn(40, 100);
    return render(*kernel, frames(0.5));
}

void testCleanupFilter() {
    const auto off = playCleanup(0.0f, 5.0f);
    const auto on = playCleanup(1.0f, 5.0f);
    CHECK(brightness(on, frames(0.1)) < brightness(off, frames(0.1)) * 0.7, "clean-up should tame the downsampler: %f against %f",
          brightness(on, frames(0.1)), brightness(off, frames(0.1)));
    CHECK(brightness(playCleanup(1.0f, 2.0f), frames(0.1)) < brightness(playCleanup(1.0f, 16.0f), frames(0.1)),
          "a smaller multiple should clean up more");
    CHECK(playCleanup(0.0f, 2.0f) == playCleanup(0.0f, 16.0f), "with clean-up off, the multiple should change nothing");
}

void testGritExtremesStayBounded() {
    float highest = 0.0f;
    bool finite = true;
    for (float position : { 0.0f, 1.0f }) {
        auto kernel = makeKernel(kSampleRate, true, true, true);
        kernel->setParameter(foldAmount, 100.0f);
        kernel->setParameter(foldPosition, position);
        kernel->setParameter(cleanupMultiple, 1.0f);
        kernel->setParameter(resonance, 100.0f);
        kernel->setParameter(outputLevel, 6.0f);
        kernel->setParameter(subLevel, 100.0f);
        kernel->noteOn(28, 127);
        const auto output = render(*kernel, frames(0.3));
        highest = std::max(highest, peak(output));
        finite = finite && allFinite(output);
    }
    CHECK(highest <= 1.0f, "full fold and resonance left full scale: peak %f", highest);
    CHECK(finite, "full fold and resonance produced a non-finite sample");
}

// MARK: - The owner's UI/UX test 1

void testShapeMorphKeepsItsLevel() {
    // Saw to square changes the tone, with no hole in the middle. With a rising saw the square's
    // odd harmonics cancelled the saw's halfway: the fundamental all but vanished and the level
    // fell by 10 dB.
    auto measure = [](float shape) {
        bdd::MorphOscillator oscillator;
        std::vector<float> samples(size_t(frames(0.5)));
        for (auto& sample : samples) {
            sample = oscillator.next(55.0 / kSampleRate, shape);
        }
        return std::array<double, 2>{ rms(samples), energyAt(samples, 0, samples.size(), 55.0) };
    };
    const auto saw = measure(0.0f);
    double quietest = 1.0;
    double weakestFundamental = 1.0;
    for (int step = 1; step <= 10; ++step) {
        const auto morphed = measure(float(step) / 10.0f);
        quietest = std::min(quietest, morphed[0] / saw[0]);
        weakestFundamental = std::min(weakestFundamental, morphed[1] / saw[1]);
    }
    CHECK(20.0 * std::log10(quietest) > -1.0, "the morph dips %.1f dB below the saw", 20.0 * std::log10(quietest));
    CHECK(20.0 * std::log10(weakestFundamental) > -1.0, "the morph loses %.1f dB of fundamental",
          -20.0 * std::log10(weakestFundamental));
}

std::vector<float> playCrossover(float subPercent, float crossoverHertz) {
    auto kernel = makeKernel();
    kernel->setParameter(subLevel, subPercent);
    kernel->setParameter(subCrossover, crossoverHertz);
    kernel->setParameter(cutoff, 2500.0f);
    kernel->setParameter(resonance, 0.0f);
    kernel->noteOn(33, 100);   // A1, 55 Hz; the sub is at 27.5 Hz
    return render(*kernel, frames(1.0));
}

void testSubCrossover() {
    const size_t from = size_t(frames(0.3));
    auto level = [from](const std::vector<float>& output, double frequency) {
        return energyAt(output, from, output.size(), frequency);
    };
    // With the crossover above the note, the oscillator's fundamental gives way to the sub...
    const auto open = playCrossover(100.0f, 50.0f);
    const auto split = playCrossover(100.0f, 130.0f);
    CHECK(level(split, 55.0) < level(open, 55.0) * 0.25, "the oscillator should give up its low end below the crossover");
    // ...and the sub keeps it.
    const double subChange = 20.0 * std::log10(level(split, 27.5) / level(playCrossover(100.0f, 700.0f), 27.5));
    CHECK(std::fabs(subChange) < 1.0, "the sub below the crossover should be untouched, changed %.2f dB", subChange);
    // With the sub off the split stays: Sub Level 0 means no low end below X-OVER, not the
    // oscillator's own low end coming back (YOI-001).
    CHECK(level(playCrossover(0.0f, 130.0f), 55.0) < level(playCrossover(0.0f, 50.0f), 55.0) * 0.25,
          "with no sub, the oscillator should still give up its low end below the crossover");
    // Through the whole default patch (envelope, downsampler, clean-up), turning the sub the rest
    // of the way down must never make YOI louder or bring the fundamental back. Before the fix,
    // 10 % to 0 % raised the level by 5 dB and the fundamental by 30 dB.
    auto playDefaults = [](float subPercent) {
        auto kernel = makeKernel(kSampleRate, true, true, true);
        kernel->setParameter(subLevel, subPercent);
        kernel->noteOn(33, 100);
        return render(*kernel, frames(2.0));
    };
    const auto tenPercent = playDefaults(10.0f);
    const auto noSub = playDefaults(0.0f);
    CHECK(rms(noSub, from) <= rms(tenPercent, from) * 1.0001, "sub at 0 is louder than at 10 %%: %.1f dB against %.1f dB",
          20.0 * std::log10(rms(noSub, from)), 20.0 * std::log10(rms(tenPercent, from)));
    // 3 dB of room: the sub's 27.5 Hz leaks a little into a single-frequency reading, and the
    // bug this guards against was a 30 dB jump.
    CHECK(level(noSub, 55.0) <= level(tenPercent, 55.0) * 2.0 + 1e-12, "sub at 0 brought the oscillator's fundamental back");
    // The split is what stops the stacking: less low end piles up than with the oscillator open.
    CHECK(rms(split, from) < rms(open, from), "splitting at the crossover should leave less stacked low end");
}

double foldLevel(float amount, float position, float mode) {
    auto kernel = makeKernel();
    kernel->setParameter(filterMode, mode);
    kernel->setParameter(foldAmount, amount);
    kernel->setParameter(foldPosition, position);
    kernel->noteOn(33, 100);
    const auto output = render(*kernel, frames(1.0));
    return rms(output, size_t(frames(0.3)));
}

void testFoldKeepsTheLevel() {
    // The fold adds harmonics, not volume. Before level matching, driving the quieter band-pass
    // voice into the fold after the filter made it up to 9 dB louder.
    for (float mode : { 0.0f, 1.0f }) {
        const double unfolded = foldLevel(0.0f, 1.0f, mode);
        for (float amount : { 1.25f, 2.5f, 5.0f }) {
            const double change = 20.0 * std::log10(foldLevel(amount, 1.0f, mode) / unfolded);
            CHECK(std::fabs(change) < 1.5, "%s, pre-downsample fold at %.2f%% changes the level by %.1f dB",
                  mode > 0.5f ? "band-pass" : "low-pass", amount, change);
        }
    }
}

// MARK: - The owner's UI/UX test 2: the new signal flow

/// Renders both channels of a held note. The `render` helper insists the channels match, which
/// they no longer do once Width is up.
std::array<std::vector<float>, 2> renderStereo(Kernel& kernel, int frameCount) {
    std::array<std::vector<float>, 2> output;
    std::vector<float> left(256), right(256);
    float* pointers[2] = { left.data(), right.data() };
    for (int start = 0; start < frameCount; start += 256) {
        const int count = std::min(256, frameCount - start);
        kernel.process(std::span<float*>(pointers, 2), 0, uint32_t(count));
        output[0].insert(output[0].end(), left.begin(), left.begin() + count);
        output[1].insert(output[1].end(), right.begin(), right.begin() + count);
    }
    return output;
}

void testSubSkipsTheFilter() {
    // The sub now takes its own path around the filter and grit: a band-pass or a closed low-pass
    // no longer touches it.
    auto subEnergy = [](float mode, float hertz) {
        auto kernel = makeKernel(kSampleRate, false, true, true);
        kernel->setParameter(filterMode, mode);
        kernel->setParameter(cutoff, hertz);
        kernel->setParameter(subLevel, 100.0f);
        kernel->noteOn(33, 100);   // the sub is at 27.5 Hz
        const auto output = render(*kernel, frames(1.0));
        return energyAt(output, size_t(frames(0.3)), output.size(), 27.5);
    };
    const double open = subEnergy(0.0f, 2500.0f);
    for (const auto& [mode, hertz] : { std::pair{ 1.0f, 2500.0f }, std::pair{ 0.0f, 20.0f }, std::pair{ 1.0f, 300.0f } }) {
        const double change = 20.0 * std::log10(subEnergy(mode, hertz) / open);
        CHECK(std::fabs(change) < 0.5, "the sub should skip the filter, but %s at %.0f Hz changed it by %.1f dB",
              mode > 0.5f ? "band-pass" : "low-pass", hertz, change);
    }
}

std::vector<float> playFoldAt(float amount, float position) {
    auto kernel = makeKernel();
    kernel->setParameter(subLevel, 0.0f);
    kernel->setParameter(oscShape, 100.0f);   // a square: odd harmonics only
    kernel->setParameter(cutoff, 150.0f);     // and mostly just its fundamental
    kernel->setParameter(resonance, 0.0f);
    kernel->setParameter(foldAmount, amount);
    kernel->setParameter(foldPosition, position);
    kernel->noteOn(40, 100);
    return render(*kernel, frames(1.0));
}

void testPostDownsampleFold() {
    CHECK(playFoldAt(0.0f, 2.0f) == playFoldAt(0.0f, 0.0f), "at 0 %% the post-downsample fold should be an exact bypass");

    const double f0 = noteHertz(40);
    const size_t from = size_t(frames(0.3));
    auto evenShare = [&](const std::vector<float>& output) {
        return energyAt(output, from, output.size(), 2.0 * f0) / energyAt(output, from, output.size(), f0);
    };
    // A symmetric fold of a near-sine makes odd harmonics only; the uneven one adds even ones.
    // At the full 5 % throw that's about 10 % more 2nd harmonic (x1.09 measured 2026-09-29; x5
    // was the bar at the old 60 %).
    const auto uneven = playFoldAt(5.0f, 2.0f);
    const auto even = playFoldAt(5.0f, 1.0f);
    CHECK(evenShare(uneven) > evenShare(even) * 1.05, "the post-downsample fold should add even harmonics: %.4f against %.4f",
          evenShare(uneven), evenShare(even));
    double sum = 0.0;
    for (size_t index = from; index < uneven.size(); ++index) {
        sum += uneven[index];
    }
    CHECK(std::fabs(sum / double(uneven.size() - from)) < 0.002, "the uneven fold should leave no DC: mean %f",
          sum / double(uneven.size() - from));
}

void testHarmonicBooster() {
    auto play = [](float amount) {
        auto kernel = makeKernel();
        kernel->setParameter(subLevel, 0.0f);
        kernel->setParameter(cutoff, 2500.0f);
        kernel->setParameter(resonance, 0.0f);
        kernel->setParameter(boostAmount, amount);
        kernel->noteOn(40, 100);
        return render(*kernel, frames(1.0));
    };
    const auto off = play(0.0f);
    const auto on = play(100.0f);
    const double f0 = noteHertz(40);
    const size_t from = size_t(frames(0.3));
    auto third = [&](const std::vector<float>& output) {
        return energyAt(output, from, output.size(), 3.0 * f0) / energyAt(output, from, output.size(), 2.0 * f0);
    };
    CHECK(third(on) > third(off) * 2.0, "the booster should lift the 3rd harmonic against the 2nd: %.3f against %.3f",
          third(on), third(off));
    const double change = 20.0 * std::log10(rms(on, from) / rms(off, from));
    CHECK(std::fabs(change) < 1.5, "the booster should be level-matched, but changed the level by %.1f dB", change);
}

void testOttEvensOutTheLevel() {
    // The drawn envelope swings the cutoff, and so the level. The compressor should narrow the
    // swing (loud parts down, quiet parts up), bring the quiet top end forward, and keep the
    // overall level about where it was.
    struct Result { double swing, brightness, level; float highest; };
    auto measure = [](float depth) {
        auto kernel = makeKernel(kSampleRate, true, true, true);
        kernel->setParameter(subLevel, 0.0f);
        kernel->setParameter(subCrossover, 50.0f);   // a full-range voice, as this was written against
        kernel->setParameter(envAmount, 6.0f);
        kernel->setParameter(envTimeMode, 1.0f);    // Free, so the drawing moves without a host
        kernel->setParameter(envFreeTime, 400.0f);
        kernel->setParameter(macroPower, depth * 0.5f);   // POWER 50 % is OTT DEPTH 100 % at UP 100 %, as written against
        kernel->noteOn(40, 100);
        const auto output = render(*kernel, frames(2.0));
        const size_t from = size_t(frames(0.3));
        double quietest = 1e9, loudest = -1e9;
        for (size_t start = from; start + size_t(frames(0.05)) < output.size(); start += size_t(frames(0.05))) {
            const double level = 20.0 * std::log10(rms(output, start, start + size_t(frames(0.05))) + 1e-9);
            quietest = std::min(quietest, level);
            loudest = std::max(loudest, level);
        }
        return Result{ loudest - quietest, 20.0 * std::log10(brightness(output, from)),
                       20.0 * std::log10(rms(output, from)), peak(output) };
    };
    const auto dry = measure(0.0f);
    const auto squashed = measure(100.0f);
    CHECK(squashed.swing < dry.swing - 1.0, "OTT should narrow the level swing: %.1f dB against %.1f dB", squashed.swing, dry.swing);
    CHECK(squashed.brightness > dry.brightness + 2.0, "OTT should bring the top end forward: %.1f dB against %.1f dB",
          squashed.brightness, dry.brightness);
    // Its level is POWER's job now, measured in LUFS on the default patch: see testMacros.
    CHECK(squashed.highest <= 1.0f, "OTT's output should stay inside full scale: peak %f", squashed.highest);
}

void testOttTimeAndUpward() {
    // OTT TIME and OTT UP. At their defaults (50 %, 100 %) the compressor is exactly what it was;
    // TIME sets how closely it rides each sweep, UP how far it lifts the quiet top end.
    struct Result { double swing, brightness; float highest; std::vector<float> output; };
    auto measure = [](float time, float upward, bool setThem) {
        auto kernel = makeKernel(kSampleRate, true, true, true);
        kernel->setParameter(cleanupMultiple, 5.0f);   // the clean-up this was measured with
        kernel->setParameter(subLevel, 0.0f);
        kernel->setParameter(subCrossover, 50.0f);
        kernel->setParameter(envAmount, 6.0f);
        kernel->setParameter(envTimeMode, 1.0f);    // Free, so the drawing moves without a host
        kernel->setParameter(envFreeTime, 400.0f);
        kernel->setParameter(ottDepth, 100.0f);
        if (setThem) {
            kernel->setParameter(ottTime, time);
            kernel->setParameter(ottUpward, upward);
        }
        kernel->noteOn(40, 100);
        auto output = render(*kernel, frames(2.0));
        const size_t from = size_t(frames(0.3));
        double quietest = 1e9, loudest = -1e9;
        for (size_t start = from; start + size_t(frames(0.05)) < output.size(); start += size_t(frames(0.05))) {
            const double level = 20.0 * std::log10(rms(output, start, start + size_t(frames(0.05))) + 1e-9);
            quietest = std::min(quietest, level);
            loudest = std::max(loudest, level);
        }
        const double bright = 20.0 * std::log10(brightness(output, from));
        const float highest = peak(output);
        return Result{ loudest - quietest, bright, highest, std::move(output) };
    };
    const auto untouched = measure(0.0f, 0.0f, false);
    const auto defaults = measure(50.0f, 100.0f, true);
    CHECK(defaults.output == untouched.output, "OTT TIME 50 %% and OTT UP 100 %% should be exactly the OTT as it was");

    const auto fast = measure(0.0f, 100.0f, true);
    const auto slow = measure(100.0f, 100.0f, true);
    CHECK(fast.swing < slow.swing - 1.0, "a shorter OTT TIME should ride the sweeps harder: swing %.1f dB against %.1f dB",
          fast.swing, slow.swing);

    const auto noLift = measure(50.0f, 0.0f, true);
    const auto moreLift = measure(50.0f, 200.0f, true);
    CHECK(moreLift.brightness > defaults.brightness + 1.0, "OTT UP 200 %% should bring the top end further forward: %.1f dB against %.1f dB",
          moreLift.brightness, defaults.brightness);
    CHECK(noLift.brightness < defaults.brightness - 1.0, "OTT UP 0 %% should leave the top end where it was: %.1f dB against %.1f dB",
          noLift.brightness, defaults.brightness);

    for (const auto* result : { &fast, &slow, &noLift, &moreLift }) {
        CHECK(result->highest <= 1.0f, "OTT output should stay inside full scale: peak %f", result->highest);
    }

    auto kernel = makeKernel();
    kernel->setParameter(ottUpward, 500.0f);
    CHECK(kernel->getParameter(ottUpward) == 200.0f, "OTT UP should stop at 200 %%, got %f", kernel->getParameter(ottUpward));
    kernel->setParameter(ottTime, -10.0f);
    CHECK(kernel->getParameter(ottTime) == 0.0f, "OTT TIME should start at 0 %%, got %f", kernel->getParameter(ottTime));
}

void testLabExperiments() {
    // The LAB page: LOCK, MIRROR and DRIVE. At 0 each is an exact bypass (every other test runs
    // with them at 0); these check each one does what it's for, and that nothing escapes at full.

    // LOCK: S&H at a whole multiple of the note puts the aliases on the note's harmonics.
    auto playLock = [](float lock) {
        auto kernel = makeKernel(kSampleRate, false, true, false);
        kernel->setParameter(subLevel, 0.0f);
        kernel->setParameter(subCrossover, 50.0f);
        kernel->setParameter(cutoff, 2500.0f);
        kernel->setParameter(resonance, 0.0f);
        kernel->setParameter(dsLock, lock);
        kernel->noteOn(45, 100);   // 110 Hz against 1400 Hz: 12.7 times the note, so unlocked is off-pitch
        return render(*kernel, frames(1.5));
    };
    auto harmonicShare = [](const std::vector<float>& output) {
        const size_t from = size_t(frames(0.3));
        double onHarmonics = 0.0;
        for (int harmonic = 1; harmonic * noteHertz(45) < 20000.0; ++harmonic) {
            const double energy = energyAt(output, from, output.size(), harmonic * noteHertz(45));
            onHarmonics += 2.0 * energy * energy;   // a sine of amplitude 2e has power 2e^2
        }
        const double total = rms(output, from);
        return onHarmonics / (total * total);
    };
    const double free = harmonicShare(playLock(0.0f));
    const double locked = harmonicShare(playLock(100.0f));
    CHECK(locked > 0.95 && locked > free + 0.05, "LOCK should put the S&H's aliases on the note's harmonics: %.1f %% on them, against %.1f %% unlocked",
          100.0 * locked, 100.0 * free);

    // MIRROR: a second peak moving opposite the drawing. With the drawing held at the top, the
    // main peak is at CUTOFF and the mirror Amount octaves below; held at the bottom, the reverse.
    auto playMirror = [](float drawing, float mirror) {
        auto kernel = makeKernel(kSampleRate, true);
        setFlatDrawing(*kernel, drawing);
        kernel->setParameter(subLevel, 0.0f);
        kernel->setParameter(subCrossover, 50.0f);
        kernel->setParameter(filterMode, 1.0f);   // band-pass, so each peak stands on its own
        kernel->setParameter(cutoff, 1600.0f);
        kernel->setParameter(envAmount, 3.0f);    // so the bottom is 200 Hz
        kernel->setParameter(resonance, 60.0f);
        kernel->setParameter(filterMirror, mirror);
        kernel->noteOn(43, 100);   // 98 Hz: harmonics at 196 and 1568 Hz, next to both peaks
        return render(*kernel, frames(1.0));
    };
    const size_t mirrorFrom = size_t(frames(0.3));
    auto near = [&](const std::vector<float>& output, double hertz) { return energyAt(output, mirrorFrom, output.size(), hertz); };
    CHECK(near(playMirror(1.0f, 100.0f), 196.0) > near(playMirror(1.0f, 0.0f), 196.0) * 3.0,
          "with the drawing at the top, MIRROR should add a peak at the bottom of the sweep");
    CHECK(near(playMirror(0.0f, 100.0f), 1568.0) > near(playMirror(0.0f, 0.0f), 1568.0) * 3.0,
          "with the drawing at the bottom, MIRROR should add a peak at CUTOFF");

    // DRIVE: saturating the resonance loop tames a strong peak, and leaves the default sound nearly alone.
    auto playDrive = [](float drive, float resonanceAmount) {
        auto kernel = makeKernel(kSampleRate, true, true, true);
        kernel->setParameter(resonance, resonanceAmount);
        kernel->setParameter(filterDrive, drive);
        kernel->noteOn(33, 100);
        return render(*kernel, frames(1.5));
    };
    const auto sharp = playDrive(0.0f, 100.0f);
    const auto tamed = playDrive(100.0f, 100.0f);
    CHECK(peak(tamed) < peak(sharp) * 0.8f, "DRIVE should tame the peak at full resonance: peak %f against %f", peak(tamed), peak(sharp));
    const size_t driveFrom = size_t(frames(0.3));
    const double defaultLevel = 20.0 * std::log10(rms(playDrive(0.0f, 30.0f), driveFrom));
    const double drivenLevel = 20.0 * std::log10(rms(playDrive(100.0f, 30.0f), driveFrom));
    CHECK(std::fabs(drivenLevel - defaultLevel) < 1.5, "DRIVE at full should leave the default sound's level nearly alone: %.1f dB against %.1f dB",
          drivenLevel, defaultLevel);

    // Everything at full, low and high, stays finite and inside full scale.
    for (int note : { 33, 72 }) {
        auto kernel = makeKernel(kSampleRate, true, true, true);
        for (auto [address, value] : std::initializer_list<std::pair<AUParameterAddress, float>>{
                 { resonance, 100.0f }, { filterMode, 1.0f }, { envAmount, 8.0f }, { filterMirror, 100.0f },
                 { filterDrive, 100.0f }, { dsLock, 100.0f }, { foldAmount, 100.0f }, { boostAmount, 100.0f },
                 { ottDepth, 100.0f }, { ottUpward, 200.0f } }) {
            kernel->setParameter(address, value);
        }
        kernel->noteOn(note, 100);
        const auto output = render(*kernel, frames(1.0));
        CHECK(allFinite(output) && peak(output) <= 1.0f, "every LAB control at full should stay bounded (note %d): peak %f", note, peak(output));
    }

    auto kernel = makeKernel();
    kernel->setParameter(dsLock, 150.0f);
    CHECK(kernel->getParameter(dsLock) == 1.0f, "LOCK is a switch: anything past halfway is Lock, got %f", kernel->getParameter(dsLock));
    kernel->setParameter(filterDrive, -5.0f);
    CHECK(kernel->getParameter(filterDrive) == 0.0f, "DRIVE should start at 0 %%, got %f", kernel->getParameter(filterDrive));
}

/// Loudness in LUFS (ITU-R BS.1770 K-weighting at 48 kHz, no gating: the tests play steady notes).
double loudness(const std::vector<float>& left, const std::vector<float>& right, size_t from) {
    struct Biquad {
        double b0, b1, b2, a1, a2, z1 = 0.0, z2 = 0.0;
        double run(double x) { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    };
    double sum = 0.0;
    for (const auto* channel : { &left, &right }) {
        Biquad shelf{ 1.53512485958697, -2.69169618940638, 1.19839281085285, -1.69065929318241, 0.73248077421585 };
        Biquad highPass{ 1.0, -2.0, 1.0, -1.99004745483398, 0.99007225036621 };
        double meanSquare = 0.0;
        for (size_t i = 0; i < channel->size(); ++i) {
            const double y = highPass.run(shelf.run((*channel)[i]));
            if (i >= from) meanSquare += y * y;
        }
        sum += meanSquare / double(channel->size() - from);
    }
    return -0.691 + 10.0 * std::log10(sum);
}

void testMacros() {
    // The macros (YOI_DOCS decisions/redesign-macros) write the stages they cover.
    auto kernel = makeKernel();
    auto near = [&](AUParameterAddress address, float expected) {
        const float actual = kernel->getParameter(address);
        CHECK(std::fabs(actual - expected) < 0.01f, "address %llu reads %f, expected %f", (unsigned long long)address, actual, expected);
    };
    kernel->setParameter(macroVoice, 50.0f);
    near(oscShape, 50.0f); near(subShape, 50.0f);
    kernel->setParameter(macroThroat, 0.0f);
    near(dsRate, 6000.0f); near(dsAmount, 12.0f);
    kernel->setParameter(macroThroat, 50.0f);
    near(dsRate, float(std::sqrt(6000.0 * 1400.0))); near(dsAmount, 41.0f);   // log-spaced rate, linear amount
    kernel->setParameter(macroThroat, 100.0f);
    near(dsRate, 1400.0f); near(dsAmount, 70.0f);
    kernel->setParameter(macroPower, 25.0f);
    near(ottDepth, 50.0f); near(ottUpward, 100.0f);
    kernel->setParameter(macroPower, 75.0f);
    near(ottDepth, 100.0f); near(ottUpward, 150.0f);
    kernel->setParameter(macroControl, 0.0f);
    near(cleanupMultiple, 16.0f); near(boostAmount, 0.0f); near(filterDrive, 0.0f);
    kernel->setParameter(macroControl, 50.0f);
    near(cleanupMultiple, float(16.0 / std::sqrt(2.0))); near(boostAmount, 20.0f); near(filterDrive, 15.0f);   // drive squared, capped at 60 %
    kernel->setParameter(macroControl, 100.0f);
    near(cleanupMultiple, 8.0f); near(boostAmount, 40.0f); near(filterDrive, 60.0f);
    kernel->setParameter(macroWidth, 0.0f);
    near(widthAmount, 0.0f); near(subLevel, 75.0f);
    kernel->setParameter(macroWidth, 100.0f);
    near(widthAmount, 100.0f); near(subLevel, 50.0f);

    // MIRROR only sounds in band-pass: in low-pass it changes nothing at all.
    auto playMirrorInLowPass = [](float mirror) {
        auto k = makeKernel(kSampleRate, true, true, true);
        k->setParameter(filterMirror, mirror);
        k->noteOn(33, 100);
        return render(*k, frames(0.5));
    };
    CHECK(playMirrorInLowPass(100.0f) == playMirrorInLowPass(0.0f), "MIRROR should do nothing in LP");

    // POWER changes density, never loudness: in LUFS, every step stays within 0.25 LU of POWER 0
    // (measured: within 0.04 LU across six notes, three of them not in the fit).
    auto playPower = [](int note, float power) {
        auto k = makeKernel(kSampleRate, true, true, true);
        k->setParameter(macroPower, power);
        k->noteOn(note, 100);
        std::vector<float> left, right;
        std::vector<float> l(256), r(256);
        float* buffers[2] = { l.data(), r.data() };
        int64_t time = 0;
        for (int done = 0; done < frames(2.0); done += 256) {
            k->process(std::span<float*>(buffers, 2), time, 256);
            time += 256;
            left.insert(left.end(), l.begin(), l.end());
            right.insert(right.end(), r.begin(), r.end());
        }
        return loudness(left, right, size_t(frames(0.5)));
    };
    for (int note : { 33, 28, 45 }) {
        const double base = playPower(note, 0.0f);
        double worst = 0.0;
        for (int step = 1; step <= 10; ++step) {
            worst = std::max(worst, std::fabs(playPower(note, float(step * 10)) - base));
        }
        CHECK(worst < 0.25, "POWER should keep the loudness (note %d): drifted %.2f LU", note, worst);
    }

    // The same for VOICE, THROAT, CONTROL and WIDTH, from each one's default position.
    auto playMacro = [](int note, AUParameterAddress macro, float value) {
        auto k = makeKernel(kSampleRate, true, true, true);
        k->setParameter(macro, value);
        k->noteOn(note, 100);
        std::vector<float> left, right;
        std::vector<float> l(256), r(256);
        float* buffers[2] = { l.data(), r.data() };
        int64_t time = 0;
        for (int done = 0; done < frames(2.0); done += 256) {
            k->process(std::span<float*>(buffers, 2), time, 256);
            time += 256;
            left.insert(left.end(), l.begin(), l.end());
            right.insert(right.end(), r.begin(), r.end());
        }
        return loudness(left, right, size_t(frames(0.5)));
    };
    struct Neutral { const char* name; AUParameterAddress address; float reference; };
    for (const auto& macro : { Neutral{ "VOICE", macroVoice, 0.0f }, Neutral{ "THROAT", macroThroat, 100.0f },
                               Neutral{ "CONTROL", macroControl, 0.0f }, Neutral{ "WIDTH", macroWidth, 0.0f } }) {
        for (int note : { 28, 33, 45 }) {
            const double base = playMacro(note, macro.address, macro.reference);
            double worst = 0.0;
            for (int step = 0; step <= 10; ++step) {
                worst = std::max(worst, std::fabs(playMacro(note, macro.address, float(step * 10)) - base));
            }
            CHECK(worst < 0.25, "%s should keep the loudness (note %d): drifted %.2f LU", macro.name, note, worst);
        }
    }
}

void testWidthIsMonoCompatible() {
    auto play = [](float width) {
        auto kernel = makeKernel();
        kernel->setParameter(widthAmount, width);
        kernel->noteOn(45, 100);
        return renderStereo(*kernel, frames(0.5));
    };
    const auto narrow = play(0.0f);
    const auto wide = play(100.0f);
    CHECK(narrow[0] == narrow[1], "at 0 width, left and right should be identical");
    double difference = 0.0, sumError = 0.0;
    for (size_t index = 0; index < wide[0].size(); ++index) {
        difference = std::max(difference, double(std::fabs(wide[0][index] - wide[1][index])));
        const double mono = 0.5 * (double(wide[0][index]) + double(wide[1][index]));
        sumError = std::max(sumError, std::fabs(mono - double(narrow[0][index])));
    }
    CHECK(difference > 0.01, "full width should make left and right differ, largest difference %f", difference);
    CHECK(sumError < 1.0e-5, "left + right should sum back to the mono sound exactly, error %g", sumError);
}

void testFinishExtremesStayBounded() {
    float highest = 0.0f;
    bool finite = true;
    for (float position : { 0.0f, 1.0f, 2.0f }) {
        auto kernel = makeKernel(kSampleRate, true, true, true);
        kernel->setParameter(foldAmount, 100.0f);
        kernel->setParameter(foldPosition, position);
        kernel->setParameter(boostAmount, 100.0f);
        kernel->setParameter(ottDepth, 100.0f);
        kernel->setParameter(widthAmount, 100.0f);
        kernel->setParameter(resonance, 100.0f);
        kernel->setParameter(subLevel, 100.0f);
        kernel->setParameter(outputLevel, 6.0f);
        kernel->noteOn(28, 127);
        const auto output = renderStereo(*kernel, frames(0.5));
        for (const auto& channel : output) {
            highest = std::max(highest, peak(channel));
            finite = finite && allFinite(channel);
        }
    }
    CHECK(highest <= 1.0f, "everything at full left full scale: peak %f", highest);
    CHECK(finite, "everything at full produced a non-finite sample");
}

} // namespace

int main() {
    testDefaultsMatchParameterTree();
    testParametersRoundTrip();
    testRangesClamp();
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
    testCurveRendering();
    testCurveSanitising();
    testDirections();
    testSyncLengths();
    testEnvelopeFollowsHostPosition();
    testEnvelopeRunsAtTempoWhenStopped();
    testFreeTime();
    testRetrigger();
    testEnvelopeRunsThroughRelease();
    testRandomRepeatsWithTheSong();
    testEnvelopeMovesTheCutoff();
    testRedrawingCrossfades();
    testFactoryShapes();
    testEnvelopeExtremesStayBounded();
    testSampleAndHold();
    testSampleCountDownsampler();
    testDownsamplerInTheVoice();
    testDownsamplerComesAfterTheFilter();
    testDownsamplerModeSwitchSettles();
    testDownsamplerExtremesStayBounded();
    testWavefolderCurve();
    testWavefolderAntialiasing();
    testFoldInTheVoice();
    testFoldPositionSwitchIsSmooth();
    testShapeMorphKeepsItsLevel();
    testSubCrossover();
    testFoldKeepsTheLevel();
    testSubSkipsTheFilter();
    testPostDownsampleFold();
    testHarmonicBooster();
    testOttEvensOutTheLevel();
    testOttTimeAndUpward();
    testLabExperiments();
    testMacros();
    testWidthIsMonoCompatible();
    testFinishExtremesStayBounded();
    testCleanupFilter();
    testGritExtremesStayBounded();

    std::printf("%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
