#pragma once

// =============================================================================
// Patch - the chiptune synth's live parameters, knob mapping and presets
// =============================================================================
// A Patch fully describes one voice. The 8 knobs (CC 21..28) edit it in real
// time; Voices.h turns a (Patch + note) into an audible ChipSound. The top pad
// row picks one of the 8 presets below.
// =============================================================================

#include <TrussC.h>

#include <cmath>

using namespace tc;

struct Patch {
    Wave  wave    = Wave::Square;
    float attack  = 0.005f;  // seconds
    float decay   = 0.06f;   // seconds
    float sustain = 0.6f;    // 0..1 level
    float release = 0.12f;   // seconds
    float length  = 0.45f;   // seconds (total note duration)
    float detune  = 0.0f;    // semitones (fine, -1..+1)
    float volume  = 0.5f;    // 0..1

    // 5 usable waveforms spread across the WAVE knob.
    static Wave waveFromKnob(int value) {
        static const Wave waves[5] = {
            Wave::Sin, Wave::Triangle, Wave::Square, Wave::Sawtooth, Wave::Noise
        };
        int i = value * 5 / 128;
        return waves[i < 0 ? 0 : (i > 4 ? 4 : i)];
    }

    int waveIndex() const {
        switch (wave) {
            case Wave::Sin:      return 0;
            case Wave::Triangle: return 1;
            case Wave::Square:   return 2;
            case Wave::Sawtooth: return 3;
            default:             return 4;  // Noise
        }
    }

    const char* waveName() const {
        static const char* names[5] = {"SINE", "TRI", "SQUARE", "SAW", "NOISE"};
        return names[waveIndex()];
    }

    // Apply a knob (0..7) carrying a 0..127 value.
    void setKnob(int index, int value) {
        float n = value / 127.0f;  // 0..1
        switch (index) {
            case 0: wave    = waveFromKnob(value);  break;
            case 1: attack  = 0.001f + n * 0.5f;    break;  // 1 ms .. 0.5 s
            case 2: decay   = 0.005f + n * 0.5f;    break;
            case 3: sustain = n;                    break;
            case 4: release = 0.005f + n * 0.8f;    break;
            case 5: length  = 0.08f  + n * 1.6f;    break;  // 80 ms .. 1.7 s
            case 6: detune  = (n - 0.5f) * 2.0f;    break;  // -1 .. +1 semitone
            case 7: volume  = n;                    break;
        }
    }

    // Normalised [0..1] reading for an on-screen dial.
    float knobValue(int index) const {
        switch (index) {
            case 0: return waveIndex() / 4.0f;
            case 1: return (attack  - 0.001f) / 0.5f;
            case 2: return (decay   - 0.005f) / 0.5f;
            case 3: return sustain;
            case 4: return (release - 0.005f) / 0.8f;
            case 5: return (length  - 0.08f)  / 1.6f;
            case 6: return detune * 0.5f + 0.5f;
            case 7: return volume;
        }
        return 0.0f;
    }
};

inline const char* knobName(int index) {
    static const char* names[8] = {
        "WAVE", "ATTACK", "DECAY", "SUSTAIN", "RELEASE", "LENGTH", "DETUNE", "VOLUME"
    };
    return (index >= 0 && index < 8) ? names[index] : "?";
}

// MIDI note -> frequency (A4 = note 69 = 440 Hz).
inline float midiToHz(float note) {
    return 440.0f * std::pow(2.0f, (note - 69.0f) / 12.0f);
}

// 8 preset patches, picked by the top pad row.
//                          wave            atk    dec    sus   rel    len   det    vol
inline Patch presetPatch(int index) {
    switch (index & 7) {
        case 0: return {Wave::Square,   0.004f, 0.06f, 0.6f, 0.12f, 0.45f, 0.0f,  0.5f};  // Lead
        case 1: return {Wave::Triangle, 0.005f, 0.10f, 0.7f, 0.20f, 0.70f, 0.0f,  0.5f};  // Soft
        case 2: return {Wave::Sawtooth, 0.004f, 0.14f, 0.5f, 0.16f, 0.60f, 0.06f, 0.45f}; // Buzz
        case 3: return {Wave::Sin,      0.02f,  0.20f, 0.8f, 0.30f, 0.95f, 0.0f,  0.5f};  // Pad
        case 4: return {Wave::Square,   0.001f, 0.05f, 0.0f, 0.05f, 0.20f, 0.0f,  0.5f};  // Pluck
        case 5: return {Wave::Sawtooth, 0.001f, 0.04f, 0.0f, 0.06f, 0.18f, 0.0f,  0.45f}; // Stab
        case 6: return {Wave::Triangle, 0.002f, 0.05f, 0.3f, 0.10f, 0.32f, 0.0f,  0.5f};  // Bell
        case 7: return {Wave::Noise,    0.001f, 0.06f, 0.0f, 0.07f, 0.22f, 0.0f,  0.4f};  // Perc
    }
    return {};
}

inline const char* presetName(int index) {
    static const char* names[8] = {
        "Lead", "Soft", "Buzz", "Pad", "Pluck", "Stab", "Bell", "Perc"
    };
    return names[index & 7];
}
