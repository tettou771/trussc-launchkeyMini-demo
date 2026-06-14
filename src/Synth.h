#pragma once

// =============================================================================
// Synth - real-time polyphonic synthesis on the audio thread
// =============================================================================
// Instead of pre-rendering a fixed-length ChipSound buffer per key press, we
// synthesise live: every active Voice generates its waveform sample-by-sample
// inside the engine's `audioOut` callback and ADDs itself into the output
// buffer. A key press just hands a note to the audio thread; the Voice stays
// alive after release until its envelope tail reaches zero.
//
// Threading:
//   - MIDI thread  : noteOn/noteOff push an Event into a lock-free queue.
//                    (The app already serialises these under its mutex, so
//                    there is a single producer.)
//   - audio thread : the audioOut listener owns the Voice pool. It drains the
//                    queue, assigns/releases voices, and mixes them. No locks,
//                    no allocation, no engine calls - the rule for audio-thread
//                    callbacks.
//   - main thread  : never touches voices; the on-screen keyboard uses the
//                    app's own held_/noteVel_ flags, and the scope taps the
//                    real output.
//
// Amplitude: per-voice peak (at velocity 127, volume 1.0) is waveGain(wave),
// kept small so a handful of notes sum well under 1.0. Normal playing velocity
// scales it down further.
// =============================================================================

#include "Patch.h"

#include <TrussC.h>

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

using namespace tc;

// -----------------------------------------------------------------------------
// Lock-free single-producer / single-consumer ring buffer.
// push() runs on the (serialised) MIDI thread, pop() on the audio thread.
// -----------------------------------------------------------------------------
template <class T, int N>
class SpscQueue {
public:
    bool push(const T& v) {
        int t = tail_.load(std::memory_order_relaxed);
        int n = (t + 1) % N;
        if (n == head_.load(std::memory_order_acquire)) return false;  // full
        buf_[t] = v;
        tail_.store(n, std::memory_order_release);
        return true;
    }

    bool pop(T& out) {
        int h = head_.load(std::memory_order_relaxed);
        if (h == tail_.load(std::memory_order_acquire)) return false;  // empty
        out = buf_[h];
        head_.store((h + 1) % N, std::memory_order_release);
        return true;
    }

private:
    std::array<T, N> buf_{};
    std::atomic<int> head_{0};  // consumer (audio thread)
    std::atomic<int> tail_{0};  // producer (MIDI thread)
};

// Per-voice peak amplitude at full velocity/volume. Brighter (more harmonics)
// waves are quieter so they don't dominate or clip as easily.
inline float waveGain(Wave w) {
    switch (w) {
        case Wave::Sin:      return 0.30f;
        case Wave::Triangle: return 0.25f;
        case Wave::Square:   return 0.15f;
        case Wave::Sawtooth: return 0.18f;
        case Wave::Noise:    return 0.15f;
        default:             return 0.20f;
    }
}

// -----------------------------------------------------------------------------
// Voice - one sounding note: oscillator phase + ADSR envelope.
// -----------------------------------------------------------------------------
struct Voice {
    enum class Stage { Attack, Decay, Sustain, Release };

    bool     active = false;
    int      note   = -1;
    Wave     wave   = Wave::Square;
    float    freq   = 0.0f;   // Hz (detune folded in)
    float    phase  = 0.0f;   // 0..1
    float    gain   = 0.0f;   // waveGain * velocity * volume (peak amplitude)

    // Sub-oscillator: a square one octave below, mixed in by `sub` (0..1).
    float    subPhase = 0.0f;
    float    sub      = 0.0f;

    // ADSR: a/d/r in seconds, s is a 0..1 level.
    float    attack = 0.0f, decay = 0.0f, sustain = 0.0f, release = 0.0f;
    Stage    stage  = Stage::Attack;
    float    env    = 0.0f;   // current envelope level 0..1
    float    relFrom = 0.0f;  // envelope level captured at note-off

    uint32_t rng = 0x1234567u;  // per-voice noise state

    void start(int n, float velocity, const Patch& p) {
        note    = n;
        wave    = p.wave;
        freq    = midiToHz(n + p.detune);
        phase   = 0.0f;
        attack  = p.attack;
        decay   = p.decay;
        sustain = p.sustain;
        release = p.release;
        gain    = waveGain(p.wave) * velocity * p.volume;
        env     = 0.0f;
        stage   = Stage::Attack;
        rng     = 0x1234567u + (uint32_t)n * 2654435761u;
        subPhase = 0.0f;
        sub      = p.subMix;
        active  = true;
    }

    // Key released: fall to silence over `release` seconds from wherever we are.
    void noteOff() {
        relFrom = env;
        stage   = Stage::Release;
    }

    // One waveform sample for a given phase (0..1). rng is only touched by
    // Noise, so the sub-oscillator (always Square) can share the voice's rng.
    static float oscillator(Wave w, float phase, uint32_t& rng) {
        switch (w) {
            case Wave::Sin:      return std::sin(TAU * phase);
            case Wave::Triangle: return 4.0f * std::fabs(phase - 0.5f) - 1.0f;
            case Wave::Square:   return phase < 0.5f ? 1.0f : -1.0f;
            case Wave::Sawtooth: return 2.0f * phase - 1.0f;
            case Wave::Noise:
                rng = rng * 1664525u + 1013904223u;
                return (float)(rng >> 9) * (2.0f / 8388608.0f) - 1.0f;
            default:             return 0.0f;
        }
    }

    // Produce one sample and advance phase + envelope. Deactivates itself when
    // the release tail reaches zero.
    float tick(float sr) {
        switch (stage) {
            case Stage::Attack:
                env += 1.0f / std::max(attack * sr, 1.0f);
                if (env >= 1.0f) { env = 1.0f; stage = Stage::Decay; }
                break;
            case Stage::Decay:
                env -= (1.0f - sustain) / std::max(decay * sr, 1.0f);
                if (env <= sustain) { env = sustain; stage = Stage::Sustain; }
                break;
            case Stage::Sustain:
                break;  // hold until noteOff
            case Stage::Release:
                env -= relFrom / std::max(release * sr, 1.0f);
                if (env <= 0.0f) { env = 0.0f; active = false; return 0.0f; }
                break;
        }

        // Main oscillator, plus an optional square sub one octave down.
        // Normalise by (1 + sub) so adding the sub thickens without raising
        // the peak past the per-wave gain (keeps the clip headroom).
        float s = oscillator(wave, phase, rng);
        if (sub > 0.0f) {
            float subSample = oscillator(Wave::Square, subPhase, rng);
            s = (s + subSample * sub) / (1.0f + sub);
        }
        s *= env * gain;

        phase += freq / sr;
        if (phase >= 1.0f) phase -= 1.0f;
        subPhase += (freq * 0.5f) / sr;  // one octave down
        if (subPhase >= 1.0f) subPhase -= 1.0f;
        return s;
    }
};

// -----------------------------------------------------------------------------
// Synth - the voice pool + audioOut wiring.
// -----------------------------------------------------------------------------
class Synth {
public:
    static constexpr int kPoly = 64;

    // Subscribe to the engine's output. Call once after the engine is up.
    void attach() {
        listener_ = AudioEngine::getInstance().audioOut.listen(
            [this](AudioOutBuffer& b) { render(b); },
            audio::priority::Generator);  // produces audio into the buffer
    }

    // MIDI thread (serialised by the app mutex). Just enqueue - the audio
    // thread does the real work.
    void noteOn(int note, int velocity, const Patch& patch) {
        queue_.push({Event::Type::NoteOn, note, velocity / 127.0f, patch});
    }

    void noteOff(int note) {
        queue_.push({Event::Type::NoteOff, note, 0.0f, Patch{}});
    }

private:
    struct Event {
        enum class Type { NoteOn, NoteOff };
        Type  type;
        int   note;
        float velocity;  // 0..1
        Patch patch;     // snapshot at note-on (ignored for NoteOff)
    };

    // Audio thread: pull pending events, then mix every active voice.
    void render(AudioOutBuffer& b) {
        Event e;
        while (queue_.pop(e)) {
            if (e.type == Event::Type::NoteOn) pickVoice().start(e.note, e.velocity, e.patch);
            else                               releaseNote(e.note);
        }

        const float sr = (float)b.sampleRate;
        for (int i = 0; i < b.frameCount; ++i) {
            float s = 0.0f;
            for (auto& v : voices_)
                if (v.active) s += v.tick(sr);
            for (int c = 0; c < b.channels; ++c)
                b.data[i * b.channels + c] += s;
        }
    }

    // Release every voice still holding this note.
    void releaseNote(int note) {
        for (auto& v : voices_)
            if (v.active && v.note == note && v.stage != Voice::Stage::Release)
                v.noteOff();
    }

    // Prefer an idle voice; otherwise steal the quietest (lowest envelope).
    Voice& pickVoice() {
        for (auto& v : voices_)
            if (!v.active) return v;
        Voice* victim = &voices_[0];
        for (auto& v : voices_)
            if (v.env < victim->env) victim = &v;
        return *victim;
    }

    std::array<Voice, kPoly> voices_;
    SpscQueue<Event, 256>    queue_;
    EventListener            listener_;  // RAII: unsubscribes on destruction
};
