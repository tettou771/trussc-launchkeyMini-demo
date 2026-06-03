#pragma once

#include <TrussC.h>

#include "LaunchkeyMini.h"
#include "Patch.h"
#include "Voices.h"
#include "Scope.h"

#include <array>
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

    // Drawing helpers.
    void drawScope(float x, float y, float w, float h, double t);
    void drawKnobs(float x, float y, float w, float h, double t);
    void drawPads(float x, float y, float w, float h, double t);
    void drawKeyboard(float x, float y, float w, float h);
    float noteToX(int pitch) const;   // screen x of a key (uses last layout)

    LaunchkeyMini lk_;
    Voices        voices_;
    Patch         patch_;
    Scope         scope_;

    int preset_    = 0;
    int octaveIdx_ = 4;   // index into kOctaveOffsets; 4 = +0 octaves

    bool started_   = false;
    bool connected_ = false;
    double lastT_   = 0.0;

    // Visual state, indexed by *played* pitch (after transpose).
    array<bool,  128> held_{};
    array<float, 128> noteVel_{};
    array<float, 8>   knobGlow_{};   // recent-touch highlight per knob
    array<float, 16>  padFlash_{};   // recent-hit flash per pad

    // Keyboard geometry, refreshed each frame so events can find a key's x.
    float kbX_ = 20.0f, kbW_ = 960.0f, kbY_ = 480.0f, kbH_ = 130.0f;

    struct Particle { float x, y, vy, life; Color color; };
    vector<Particle> particles_;
};
