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

// One cycle of each waveform, phase in [0,1) -> sample in [-1,1].
float waveSample(Wave wave, float phase) {
    phase -= std::floor(phase);
    switch (wave) {
        case Wave::Sin:      return std::sin(phase * TAU);
        case Wave::Triangle: return 4.0f * std::fabs(phase - 0.5f) - 1.0f;
        case Wave::Square:   return phase < 0.5f ? 1.0f : -1.0f;
        case Wave::Sawtooth: return 2.0f * phase - 1.0f;
        default: {  // Noise: stable pseudo-random per bucket
            float h = std::sin(std::floor(phase * 73.0f) * 12.9898f) * 43758.5453f;
            return 2.0f * (h - std::floor(h)) - 1.0f;
        }
    }
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

    lk_.onKey  = [this](int note, int vel, bool on)  { onKey(note, vel, on); };
    lk_.onKnob = [this](int index, int value)        { onKnob(index, value); };
    lk_.onPad  = [this](int index, int vel, bool on) { onPad(index, vel, on); };

    // Connection happens in update() once midiReady() (deferred on the web).
}

void tcApp::update() {
    double t  = getElapsedTime();
    float  dt = (float)(t - lastT_);
    lastT_ = t;

    if (!started_ && midiReady()) {
        started_   = true;
        connected_ = lk_.connect("Launchkey");
        if (connected_) {
            logNotice("launchkey") << "Connected"
                << (lk_.hasLeds() ? " (pad LEDs on)" : " (no LED port)");
            refreshPadLeds();
        } else {
            logWarning("launchkey") << "No Launchkey Mini found - connect one and relaunch";
        }
    }

    lk_.update();

    // Decay the transient highlights.
    for (auto& g : knobGlow_) g = std::max(0.0f, g - dt * 2.0f);
    for (auto& f : padFlash_) f = std::max(0.0f, f - dt * 3.0f);

    // Advance and cull particles.
    for (auto& p : particles_) {
        p.y    += p.vy * dt;
        p.life -= dt;
    }
    particles_.erase(
        std::remove_if(particles_.begin(), particles_.end(),
                       [](const Particle& p) { return p.life <= 0.0f; }),
        particles_.end());
}

void tcApp::draw() {
    clear(0.07f);

    const float W = (float)getWindowWidth();
    const float H = (float)getWindowHeight();

    // Header.
    setColor(1.0f);
    drawBitmapString("tcxMidi - Launchkey Mini [MK1] chiptune synth", 20, 26);
    setColor(connected_ ? Color(0.4f, 1.0f, 0.5f) : Color(1.0f, 0.6f, 0.3f));
    drawBitmapString(connected_ ? (lk_.hasLeds() ? "device connected - LEDs on"
                                                 : "device connected")
                                : "no device - connect a Launchkey Mini",
                     W - 320, 26);

    double t = getElapsedTime();

    // Layout bands.
    drawScope(20, 44, W - 40, 120, t);
    drawKnobs(20, 184, W - 40, 110, t);

    // Remember the keyboard rect so note events can locate a key's x.
    kbX_ = 20;   kbW_ = W - 40;
    kbH_ = 124;  kbY_ = H - kbH_ - 34;

    float padsY = 312;
    float padsH = kbY_ - padsY - 16;
    drawPads(20, padsY, W - 40, padsH, t);
    drawKeyboard(kbX_, kbY_, kbW_, kbH_);

    // Footer help.
    setColor(0.55f);
    drawBitmapString("keys = play   knobs = wave/ADSR/length/detune/volume   "
                     "top pads = preset   bottom pads = octave",
                     20, H - 16);
}

void tcApp::cleanup() {
    lk_.disconnect();  // turn the LEDs off and leave extended mode
}

// =============================================================================
// Device callbacks
// =============================================================================
void tcApp::onKey(int note, int velocity, bool on) {
    int pitch = note + 12 * kOctaveOffsets[octaveIdx_];
    if (pitch < 0 || pitch > 127) return;

    if (on) {
        held_[pitch]    = true;
        noteVel_[pitch] = velocity / 127.0f;
        voices_.noteOn(pitch, velocity, patch_, getElapsedTime());

        // Spawn a rising particle from the key.
        float x = noteToX(pitch);
        if (x >= 0.0f) {
            float v = velocity / 127.0f;
            Color c = Color::fromHSB(waveHue(patch_.waveIndex()), 0.7f, 1.0f);
            particles_.push_back({x, kbY_, -(80.0f + 220.0f * v), 0.5f + 0.6f * v, c});
        }
    } else {
        held_[pitch] = false;
    }
}

void tcApp::onKnob(int index, int value) {
    if (index < 0 || index > 7) return;
    patch_.setKnob(index, value);
    knobGlow_[index] = 1.0f;
}

void tcApp::onPad(int index, int velocity, bool on) {
    if (index < 0 || index > 15) return;
    if (on) padFlash_[index] = 1.0f;
    if (!on) return;

    if (index < 8) selectPreset(index);
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
void tcApp::drawScope(float x, float y, float w, float h, double t) {
    // Panel.
    setColor(0.11f);
    drawRect(x, y, w, h);

    float energy = voices_.energy(t);
    float amp    = (0.10f + 0.85f * energy) * (h * 0.42f);
    float cy     = y + h * 0.5f;
    float scroll = (float)t * 1.4f;
    const float cycles = 3.0f;

    setColor(Color::fromHSB(waveHue(patch_.waveIndex()), 0.6f, 1.0f));

    int N = (int)w;
    float px = x, py = cy;
    for (int i = 0; i <= N; i += 2) {
        float ph = (float)i / N * cycles + scroll;
        float yy = cy - waveSample(patch_.wave, ph) * amp;
        float xx = x + i;
        if (i > 0) drawLine(px, py, xx, yy);
        px = xx;
        py = yy;
    }

    // Particles float up across the scope band.
    for (const auto& p : particles_) {
        float a = std::min(p.life, 1.0f);
        setColor(Color(p.color.r, p.color.g, p.color.b, a));
        drawCircle(p.x, p.y, 2.5f + 2.0f * a);
    }

    setColor(0.5f);
    drawBitmapString(string("wave: ") + patch_.waveName(), x + 8, y + 16);
}

void tcApp::drawKnobs(float x, float y, float w, float h, double t) {
    const int   n  = 8;
    const float cw = w / n;
    const float r  = std::min(cw * 0.32f, h * 0.34f);

    for (int i = 0; i < n; ++i) {
        float cx = x + cw * (i + 0.5f);
        float cy = y + h * 0.40f;
        float v  = patch_.knobValue(i);
        float glow = knobGlow_[i];

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
    (void)t;
}

void tcApp::drawPads(float x, float y, float w, float h, double t) {
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
                base = (col == preset_) ? Color(0.25f, 0.95f, 0.4f)
                                        : Color(0.30f, 0.13f, 0.13f);
            } else {         // octave
                base = (col == octaveIdx_) ? Color(1.0f, 0.65f, 0.2f)
                     : (col == 4)          ? Color(0.13f, 0.28f, 0.16f)
                                           : Color(0.14f, 0.14f, 0.16f);
            }

            // Flash brighter just after a hit.
            float f = padFlash_[index];
            setColor(Color(base.r + (1.0f - base.r) * f,
                           base.g + (1.0f - base.g) * f,
                           base.b + (1.0f - base.b) * f));
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
    (void)t;
}

void tcApp::drawKeyboard(float x, float y, float w, float h) {
    int   nWhite = whiteCount();
    float kw     = w / nWhite;

    // White keys first.
    for (int n = kLo; n <= kHi; ++n) {
        if (!isWhite(n)) continue;
        float kx = x + whitesBefore(n) * kw;
        float v  = held_[n] ? noteVel_[n] : 0.0f;
        if (held_[n]) setColor(Color(0.4f + 0.6f * v, 0.9f, 0.5f + 0.4f * v));
        else          setColor(Color(0.92f, 0.92f, 0.92f));
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
        float v  = held_[n] ? noteVel_[n] : 0.0f;
        if (held_[n]) setColor(Color(0.3f + 0.7f * v, 0.8f, 0.4f + 0.5f * v));
        else          setColor(Color(0.08f, 0.08f, 0.1f));
        drawRect(kx, y, bw, bh);
    }
}

float tcApp::noteToX(int pitch) const {
    if (pitch < kLo || pitch > kHi) return -1.0f;
    int   nWhite = whiteCount();
    float kw     = kbW_ / nWhite;
    if (isWhite(pitch)) return kbX_ + whitesBefore(pitch) * kw + kw * 0.5f;
    return kbX_ + whitesBefore(pitch) * kw;  // black sits on the boundary
}
