#pragma once

// =============================================================================
// Scope - captures the real audio output for the on-screen oscilloscope
// =============================================================================
// Instead of drawing a synthetic wave, we tap the engine's actual output. An
// `audioOut` listener at **Monitor priority** (so it runs *after* any synth /
// effect listeners and sees the final mix) copies channel 0 into a ring buffer
// on the audio thread; draw() reads the latest window on the main thread.
//
// The listener is read-only and does only a copy - no allocation, no locks, no
// engine calls - which is the rule for audio-thread callbacks.
// =============================================================================

#include <TrussC.h>

#include <array>
#include <atomic>

using namespace tc;

class Scope {
public:
    static constexpr int N = 1024;  // samples shown across the scope

    // Subscribe to the engine's output. Call once after the engine is up.
    void attach() {
        listener_ = AudioEngine::getInstance().audioOut.listen(
            [this](AudioOutBuffer& b) { capture(b); },
            audio::priority::Monitor);  // reads only, ideally last
    }

    // Copy the most recent N samples (oldest -> newest) for plotting.
    // Safe to call from the main thread.
    void readWindow(std::array<float, N>& out) const {
        int w = writePos_.load(std::memory_order_acquire);  // next slot = oldest
        for (int i = 0; i < N; ++i)
            out[i] = ring_[(w + i) % N];
    }

private:
    // Audio thread: append channel-0 samples to the ring.
    void capture(const AudioOutBuffer& b) {
        int w = writePos_.load(std::memory_order_relaxed);
        for (int i = 0; i < b.frameCount; ++i) {
            ring_[w] = b.data[i * b.channels];  // channel 0
            w = (w + 1) % N;
        }
        writePos_.store(w, std::memory_order_release);
    }

    std::array<float, N> ring_{};
    std::atomic<int>     writePos_{0};
    EventListener        listener_;  // RAII: unsubscribes on destruction
};
