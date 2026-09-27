//
//  YoiExtensionParameterAddresses.h
//  YoiExtension
//
//  Created by Chris Connelly on 2026-09-26.
//
//  Addresses are grouped in blocks of ten by section, so later stages can add parameters
//  next to their neighbours without renumbering anything:
//
//      0-9    output         10-19  voice          20-29  oscillators
//      30-39  filter         40-49  amp envelope   50-69  drawn envelope
//      70-89  grit           90-99  vowel (stage 4)
//
//  Once YOI ships, existing addresses must never be renumbered: hosts save automation by
//  address, not by name, and the VST3 build uses the same numbers as its parameter IDs.
//

#pragma once

#if defined(__APPLE__) && !defined(YOI_PORTABLE)

#include <AudioToolbox/AUParameters.h>

typedef NS_ENUM(AUParameterAddress, YoiExtensionParameterAddress) {
    outputLevel = 0,

    glideTime = 10,
    glideMode = 11,
    bendRange = 12,

    oscShape = 20,
    subLevel = 21,
    subShape = 22,
    subOctave = 23,
    subCrossover = 24,

    filterMode = 30,
    cutoff = 31,
    resonance = 32,

    ampAttack = 40,
    ampDecay = 41,
    ampSustain = 42,
    ampRelease = 43,

    envAmount = 50,
    envTimeMode = 51,
    envSyncLength = 52,
    envFreeTime = 53,
    envDirection = 54,
    envRetrigger = 55,
    accelStart = 56,
    accelEnd = 57,
    accelCurve = 58,

    dsMode = 70,
    dsRate = 71,
    dsAmount = 72,
    foldAmount = 73,
    foldPosition = 74,
    cleanupMode = 75,
    cleanupMultiple = 76
};

#else

// Outside the Audio Unit (the VST3 build and the DSP tests, on any platform) AudioToolbox is not
// available, so the two scalar types it would supply are declared here with the same widths. The
// enum keeps the same name, values and unscoped C++ form that NS_ENUM produces, so the kernel
// reads the same either way. Keep this list identical to the one above.
#include <stdint.h>

typedef uint64_t AUParameterAddress;
typedef float AUValue;

enum YoiExtensionParameterAddress : AUParameterAddress {
    outputLevel = 0,

    glideTime = 10,
    glideMode = 11,
    bendRange = 12,

    oscShape = 20,
    subLevel = 21,
    subShape = 22,
    subOctave = 23,
    subCrossover = 24,

    filterMode = 30,
    cutoff = 31,
    resonance = 32,

    ampAttack = 40,
    ampDecay = 41,
    ampSustain = 42,
    ampRelease = 43,

    envAmount = 50,
    envTimeMode = 51,
    envSyncLength = 52,
    envFreeTime = 53,
    envDirection = 54,
    envRetrigger = 55,
    accelStart = 56,
    accelEnd = 57,
    accelCurve = 58,

    dsMode = 70,
    dsRate = 71,
    dsAmount = 72,
    foldAmount = 73,
    foldPosition = 74,
    cleanupMode = 75,
    cleanupMultiple = 76
};

#endif
