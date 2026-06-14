#pragma once

#include <TrussC.h>

#include "LaunchkeyMini.h"
#include "Patch.h"
#include "Synth.h"
#include "Scope.h"

#include <array>
#include <mutex>
#include <vector>

using namespace tc;
using namespace std;

// =============================================================================
// Launchkey Mini [MK1] demo - a playable chiptune synth
// =============================================================================
//   Keys  : play ChipSound notes (velocity sensitive, polyphonic).
//   Knobs : the 8 knobs shape the live patch (wave / ADSR / length / detune /
//           volume) - every new note is built from the current knob values.
//   Pads  : the top row picks one of 8 preset patches; the bottom row
//           transposes by octave. Selected pads light up (2-colour LEDs).
//
// Everything is driven by the device - this is a pure MIDI in/out sample. The
// screen mirrors the keyboard, knobs and pads so you can see what the hardware
// is sending. The MIDI/LED mapping lives in LaunchkeyMini.h.
// =============================================================================

class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;
    void cleanup() override;

private:
    // Device callbacks.
    void onKey(int note, int velocity, bool on);
    void onKnob(int index, int value);
    void onPad(int index, int velocity, bool on);

    // Pad actions.
    void selectPreset(int index);
    void selectOctave(int index);
    void refreshPadLeds();

    // Snapshot of the shared state draw() needs, copied once per frame under a
    // short lock so the rest of drawing runs lock-free (the MIDI thread writes
    // these; see draw()).
    struct Frame {
        Patch             patch;
        array<bool,  128> held{};
        array<float, 128> vel{};
        array<float, 8>   glow{};
        array<float, 16>  flash{};
        int  preset    = 0;
        int  octave    = 4;
        bool connected = false;
    };

    // Drawing helpers (read the per-frame snapshot, never the live members).
    void drawScope(const Frame& f, float x, float y, float w, float h);
    void drawKnobs(const Frame& f, float x, float y, float w, float h);
    void drawPads(const Frame& f, float x, float y, float w, float h);
    void drawKeyboard(const Frame& f, float x, float y, float w, float h);

    LaunchkeyMini lk_;
    Synth         synth_;
    Patch         patch_;
    Scope         scope_;

    int preset_    = 0;
    int octaveIdx_ = 4;   // index into kOctaveOffsets; 4 = +0 octaves

    bool started_   = false;
    bool connected_ = false;
    double lastT_   = 0.0;

    // MIDI callbacks fire on libremidi's input thread; this guards everything
    // they touch (synth + visual state) against the main draw/update thread.
    std::mutex mtx_;

    // Visual state, indexed by *played* pitch (after transpose).
    array<bool,  128> held_{};
    array<float, 128> noteVel_{};
    array<float, 8>   knobGlow_{};   // recent-touch highlight per knob
    array<float, 16>  padFlash_{};   // recent-hit flash per pad

    // Keyboard rect, recomputed each frame in draw().
    float kbX_ = 20.0f, kbW_ = 960.0f, kbY_ = 480.0f, kbH_ = 130.0f;
};
