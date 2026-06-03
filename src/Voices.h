#pragma once

// =============================================================================
// Voices - a small polyphonic pool of one-shot ChipSound voices
// =============================================================================
// ChipSound renders a fixed-length buffer, so every key press builds a fresh
// note from the *current* Patch and plays it on a free voice (stealing the
// oldest if they are all busy). Holding a key doesn't sustain past the buffer -
// that's the chiptune grain, and it means the knobs shape every new note you
// trigger. 16 voices is plenty for two-handed mini-key playing.
// =============================================================================

#include "Patch.h"

#include <TrussC.h>

#include <algorithm>
#include <array>

using namespace tc;

class Voices {
public:
    static constexpr int kCount = 16;

    void noteOn(int note, int velocity, const Patch& patch, double t) {
        int v = pickVoice(t);

        float hz  = midiToHz(note + patch.detune);
        float vel = velocity / 127.0f;

        // Velocity scales loudness; keep per-voice volume modest so stacked
        // notes don't sum into clipping.
        ChipSoundNote n{patch.wave, hz, patch.length, patch.volume * (0.3f + 0.7f * vel)};
        n.attack  = patch.attack;
        n.decay   = patch.decay;
        n.sustain = patch.sustain;
        n.release = patch.release;

        voices_[v].sound = n.build();
        voices_[v].sound.play();
        voices_[v].note   = note;
        voices_[v].startT = t;
        voices_[v].endT   = t + patch.length;
    }

    // Stop any voice still holding this note (key released).
    void noteOff(int note) {
        for (auto& v : voices_) {
            if (v.note == note) {
                v.sound.stop();
                v.note = -1;
            }
        }
    }

private:
    struct Voice {
        Sound  sound;
        int    note   = -1;
        double startT = 0.0;
        double endT   = 0.0;
    };

    // Prefer a finished voice; otherwise steal the oldest still playing.
    int pickVoice(double t) {
        int oldest = 0;
        for (int i = 0; i < kCount; ++i) {
            if (voices_[i].endT <= t) return i;
            if (voices_[i].startT < voices_[oldest].startT) oldest = i;
        }
        return oldest;
    }

    std::array<Voice, kCount> voices_;
};
