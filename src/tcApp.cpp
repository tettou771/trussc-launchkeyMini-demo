#include "tcApp.h"

#include <cmath>

// -----------------------------------------------------------------------------
// Local constants / helpers (file scope keeps tcApp.h tidy).
// -----------------------------------------------------------------------------
namespace {

// Visible piano range: C2 (36) .. C6 (84).
constexpr int kLo = 36;
constexpr int kHi = 84;

// Bottom-row pads transpose the keyboard by whole octaves.
const int kOctaveOffsets[8] = {-4, -3, -2, -1, 0, 1, 2, 3};

bool isWhite(int pitch) {
    static const bool w[12] = {1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 0, 1};
    return w[((pitch % 12) + 12) % 12];
}

// Number of white keys in [kLo, pitch).
int whitesBefore(int pitch) {
    int c = 0;
    for (int n = kLo; n < pitch; ++n)
        if (isWhite(n)) ++c;
    return c;
}

int whiteCount() {
    int c = 0;
    for (int n = kLo; n <= kHi; ++n)
        if (isWhite(n)) ++c;
    return c;
}

// Hue for the current waveform, used to tint the scope + particles.
float waveHue(int waveIndex) {
    return std::fmod(0.08f + waveIndex * 0.12f, 1.0f);
}

} // namespace

// =============================================================================
// Lifecycle
// =============================================================================
void tcApp::setup() {
    selectPreset(0);  // give the keys a sound before any pad is touched

    // Start the audio engine, attach the synth (generator) and tap the output
    // for the oscilloscope (monitor, runs last).
    AudioEngine::getInstance().init();
    synth_.attach();
    scope_.attach();

    lk_.onKey  = [this](int note, int vel, bool on)  { onKey(note, vel, on); };
    lk_.onKnob = [this](int index, int value)        { onKnob(index, value); };
    lk_.onPad  = [this](int index, int vel, bool on) { onPad(index, vel, on); };

    // The device's own octave buttons emit no MIDI, so the round scene buttons
    // shift the (software) octave instead - a handy MIDI-driven replacement.
    lk_.onButton = [this](lk::Button b, bool pressed) {
        if (!pressed) return;
        std::lock_guard<std::mutex> lock(mtx_);
        selectOctave(octaveIdx_ + (b == lk::Button::Up ? 1 : -1));
    };

    // Connection happens in update() once midiReady() (deferred on the web).
}

void tcApp::update() {
    double t  = getElapsedTime();
    float  dt = (float)(t - lastT_);
    lastT_ = t;

    // Guard the state the MIDI thread also touches (it triggers audio + writes
    // visual state from onKey/onKnob/onPad on libremidi's input thread).
    std::lock_guard<std::mutex> lock(mtx_);

    if (!started_ && midiReady()) {
        started_   = true;
        connected_ = lk_.connect("Launchkey");
        if (connected_) {
            // Pad LEDs are local-only on the Mk1 in basic mode (host LED control
            // would need InControl mode); we still send them best-effort.
            logNotice("launchkey") << "Connected (Mk1 pads are local-LED only)";
            refreshPadLeds();
        } else {
            logWarning("launchkey") << "No Launchkey Mini found - connect one and relaunch";
        }
    }

    // Decay the transient highlights.
    for (auto& g : knobGlow_) g = std::max(0.0f, g - dt * 2.0f);
    for (auto& f : padFlash_) f = std::max(0.0f, f - dt * 3.0f);
}

void tcApp::draw() {
    clear(0.07f);

    // Snapshot the state the MIDI thread also writes, under a short lock, then
    // draw lock-free from the copy. Holding the lock for the whole frame would
    // block a note that lands mid-draw for up to ~16 ms; this keeps the
    // critical section to a ~1.5 KB copy.
    Frame f;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        f.patch     = patch_;
        f.held      = held_;
        f.vel       = noteVel_;
        f.glow      = knobGlow_;
        f.flash     = padFlash_;
        f.preset    = preset_;
        f.octave    = octaveIdx_;
        f.connected = connected_;
    }

    const float W = (float)getWindowWidth();
    const float H = (float)getWindowHeight();

    // Header.
    setColor(1.0f);
    drawBitmapString("tcxMidi - Launchkey Mini [MK1] chiptune synth", 20, 26);
    setColor(f.connected ? Color(0.4f, 1.0f, 0.5f) : Color(1.0f, 0.6f, 0.3f));
    drawBitmapString(f.connected ? "device connected"
                                 : "no device - connect a Launchkey Mini",
                     W - 280, 26);

    // Layout bands.
    drawScope(f, 20, 44, W - 40, 120);
    drawKnobs(f, 20, 184, W - 40, 110);

    // Remember the keyboard rect so note events can locate a key's x.
    kbX_ = 20;   kbW_ = W - 40;
    kbH_ = 124;  kbY_ = H - kbH_ - 34;

    float padsY = 312;
    float padsH = kbY_ - padsY - 16;
    drawPads(f, 20, padsY, W - 40, padsH);
    drawKeyboard(f, kbX_, kbY_, kbW_, kbH_);

    // Footer help.
    setColor(0.55f);
    drawBitmapString("keys = play   knobs = wave/ADSR/length/detune/volume   "
                     "top pads = preset   bottom pads / round buttons = octave",
                     20, H - 16);
}

void tcApp::cleanup() {
    lk_.disconnect();  // unsubscribe, turn the LEDs off, close the ports
}

// =============================================================================
// Device callbacks
// =============================================================================
void tcApp::onKey(int note, int velocity, bool on) {
    // Runs on the MIDI thread - guard shared synth + visual state.
    std::lock_guard<std::mutex> lock(mtx_);

    int pitch = note + 12 * kOctaveOffsets[octaveIdx_];
    if (pitch < 0 || pitch > 127) return;

    if (on) {
        held_[pitch]    = true;
        noteVel_[pitch] = velocity / 127.0f;
        synth_.noteOn(pitch, velocity, patch_);
    } else {
        held_[pitch] = false;
        synth_.noteOff(pitch);  // key release starts the note's release tail
    }
}

void tcApp::onKnob(int index, int value) {
    if (index < 0 || index > 7) return;
    std::lock_guard<std::mutex> lock(mtx_);
    patch_.setKnob(index, value);
    knobGlow_[index] = 1.0f;
}

void tcApp::onPad(int index, int velocity, bool on) {
    if (index < 0 || index > 15) return;
    std::lock_guard<std::mutex> lock(mtx_);
    if (on) padFlash_[index] = 1.0f;
    if (!on) return;

    if (index < 8) selectPreset(index);   // helpers run under this lock
    else           selectOctave(index - 8);
}

// =============================================================================
// Pad actions
// =============================================================================
void tcApp::selectPreset(int index) {
    preset_ = index & 7;
    patch_  = presetPatch(preset_);
    refreshPadLeds();
    logNotice("launchkey") << "preset: " << presetName(preset_);
}

void tcApp::selectOctave(int index) {
    octaveIdx_ = (index < 0) ? 0 : (index > 7 ? 7 : index);
    refreshPadLeds();
    logNotice("launchkey") << "octave: "
        << (kOctaveOffsets[octaveIdx_] >= 0 ? "+" : "")
        << kOctaveOffsets[octaveIdx_];
}

void tcApp::refreshPadLeds() {
    // Top row: preset slots. Selected = bright green, rest = dim red.
    for (int i = 0; i < 8; ++i)
        lk_.setPadLed(i, i == preset_ ? lk::color::Green : lk::color::DimRed);

    // Bottom row: octave. Selected = amber; the centre (+0) glows dim green.
    for (int i = 0; i < 8; ++i) {
        int c = (i == octaveIdx_) ? lk::color::Amber
              : (i == 4)          ? lk::color::DimGreen
                                  : lk::color::Off;
        lk_.setPadLed(8 + i, c);
    }
}

// =============================================================================
// Drawing
// =============================================================================
void tcApp::drawScope(const Frame& f, float x, float y, float w, float h) {
    // Panel + zero line.
    setColor(0.11f);
    drawRect(x, y, w, h);
    float cy = y + h * 0.5f;
    setColor(0.18f);
    drawLine(x, cy, x + w, cy);

    // Plot the *real* audio output captured from the engine.
    static std::array<float, Scope::N> samples;
    scope_.readWindow(samples);

    const float gain  = h * 0.45f * 3.5f;  // output is quiet; scale it up
    const float clamp = h * 0.48f;
    setColor(Color::fromHSB(waveHue(f.patch.waveIndex()), 0.6f, 1.0f));

    float px = x, py = cy;
    for (int i = 0; i < Scope::N; ++i) {
        float s = samples[i] * gain;
        s = std::max(-clamp, std::min(clamp, s));
        float xx = x + (float)i / (Scope::N - 1) * w;
        float yy = cy - s;
        if (i > 0) drawLine(px, py, xx, yy);
        px = xx;
        py = yy;
    }

    setColor(0.5f);
    drawBitmapString(string("audio out  (wave: ") + f.patch.waveName() + ")", x + 8, y + 16);
}

void tcApp::drawKnobs(const Frame& f, float x, float y, float w, float h) {
    const int   n  = 8;
    const float cw = w / n;
    const float r  = std::min(cw * 0.32f, h * 0.34f);

    for (int i = 0; i < n; ++i) {
        float cx = x + cw * (i + 0.5f);
        float cy = y + h * 0.40f;
        float v  = f.patch.knobValue(i);
        float glow = f.glow[i];

        // Ring (brighter just after the knob was touched).
        noFill();
        setColor(Color(0.35f + 0.5f * glow, 0.35f + 0.4f * glow, 0.4f));
        drawCircle(cx, cy, r);
        fill();

        // Pointer: 270-degree sweep, 0 at 12 o'clock, going clockwise.
        float turn = 0.625f + v * 0.75f;
        float ang  = turn * TAU;
        setColor(Color(0.5f + 0.5f * glow, 1.0f, 0.7f));
        drawLine(cx, cy, cx + std::sin(ang) * r, cy - std::cos(ang) * r);

        // Labels (bitmap font ~8 px per char -> center by half the width).
        string name = knobName(i);
        setColor(0.7f);
        drawBitmapString(name, cx - name.size() * 4.0f, y + h - 18);
        string pct = std::to_string((int)std::round(v * 100.0f));
        setColor(0.55f);
        drawBitmapString(pct, cx - pct.size() * 4.0f, y + h - 4);
    }
}

void tcApp::drawPads(const Frame& f, float x, float y, float w, float h) {
    const int   cols = 8;
    const float gap  = 6.0f;
    const float pw   = (w - gap * (cols - 1)) / cols;
    const float ph   = (h - gap) / 2.0f;

    for (int row = 0; row < 2; ++row) {
        for (int col = 0; col < cols; ++col) {
            int   index = row * 8 + col;
            float px    = x + col * (pw + gap);
            float py    = y + row * (ph + gap);

            // Base colour from the current selection.
            Color base(0.14f, 0.14f, 0.16f);
            if (row == 0) {  // preset slots
                base = (col == f.preset) ? Color(0.25f, 0.95f, 0.4f)
                                         : Color(0.30f, 0.13f, 0.13f);
            } else {         // octave
                base = (col == f.octave) ? Color(1.0f, 0.65f, 0.2f)
                     : (col == 4)        ? Color(0.13f, 0.28f, 0.16f)
                                         : Color(0.14f, 0.14f, 0.16f);
            }

            // Flash brighter just after a hit.
            float flash = f.flash[index];
            setColor(Color(base.r + (1.0f - base.r) * flash,
                           base.g + (1.0f - base.g) * flash,
                           base.b + (1.0f - base.b) * flash));
            drawRectRounded(px, py, pw, ph, 6.0f);

            // Label.
            string text = (row == 0)
                ? string(presetName(col))
                : (string(kOctaveOffsets[col] >= 0 ? "+" : "") +
                   std::to_string(kOctaveOffsets[col]));
            setColor(0.95f);
            drawBitmapString(text, px + 6, py + ph * 0.5f + 4);
        }
    }
}

void tcApp::drawKeyboard(const Frame& f, float x, float y, float w, float h) {
    int   nWhite = whiteCount();
    float kw     = w / nWhite;

    // White keys first.
    for (int n = kLo; n <= kHi; ++n) {
        if (!isWhite(n)) continue;
        float kx = x + whitesBefore(n) * kw;
        float v  = f.held[n] ? f.vel[n] : 0.0f;
        if (f.held[n]) setColor(Color(0.4f + 0.6f * v, 0.9f, 0.5f + 0.4f * v));
        else           setColor(Color(0.92f, 0.92f, 0.92f));
        drawRect(kx + 1, y, kw - 2, h);

        // Mark each C.
        if (n % 12 == 0) {
            setColor(0.45f);
            drawBitmapString("C" + std::to_string(n / 12 - 1), kx + 2, y + h - 6);
        }
    }

    // Black keys on top.
    float bw = kw * 0.6f;
    float bh = h * 0.6f;
    for (int n = kLo; n <= kHi; ++n) {
        if (isWhite(n)) continue;
        float kx = x + whitesBefore(n) * kw - bw * 0.5f;
        float v  = f.held[n] ? f.vel[n] : 0.0f;
        if (f.held[n]) setColor(Color(0.3f + 0.7f * v, 0.8f, 0.4f + 0.5f * v));
        else           setColor(Color(0.08f, 0.08f, 0.1f));
        drawRect(kx, y, bw, bh);
    }
}
