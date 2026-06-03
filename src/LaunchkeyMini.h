#pragma once

// =============================================================================
// LaunchkeyMini - thin wrapper around a Novation Launchkey Mini [MK1]
// =============================================================================
// Turns the device's raw MIDI into musician-friendly callbacks and (best
// effort) drives the 2-colour pad LEDs.
//
// MK1 mapping - PROBED ON A REAL DEVICE (2026-06-03), not guessed:
//   - 25 mini keys : Note On/Off on channel 1 (velocity sensitive)
//   - 8 knobs      : CC 21..28 on channel 1
//   - 16 pads      : Note On/Off on channel 10 (NOT an "extended" mode).
//                    Pad notes, pads numbered 1..16 (top row 1..8, bottom row
//                    9..16, left -> right):
//                      top:    40 41 42 43 | 48 49 50 51
//                      bottom: 36 37 38 39 | 44 45 46 47
//   - round btns   : CC 108 (top) / 109 (bottom) on channel 1
//   - octave btns  : emit NO MIDI - they shift the keyboard's note range on the
//                    device itself, so the keys just send already-shifted notes.
//                    There's nothing to listen for; this is a hardware limit.
//
// Pad LEDs have red/green diodes only (no blue). We light a pad by echoing its
// note back on channel 10 with a velocity colour = 16*green + red (each 0..3).
// This is unconfirmed on the MK1 - if the device ignores it the pads simply
// stay dark; the rest of the demo is unaffected.
//
// The Mini exposes two USB-MIDI ports; we open both inputs and merge them, and
// route purely by channel, so port ordering doesn't matter. None of this uses
// SysEx, so it also works over Web MIDI.
// =============================================================================

#include <tcxMidi.h>

#include <functional>
#include <string>
#include <vector>

using namespace tcx;

namespace lk {

// 2-colour pad value: red and green are each 0..3. (0,0) = off.
inline int padColor(int red, int green) {
    auto clamp3 = [](int v) { return v < 0 ? 0 : (v > 3 ? 3 : v); };
    return 16 * clamp3(green) + clamp3(red);
}

namespace color {
    const int Off      = padColor(0, 0);
    const int Red      = padColor(3, 0);
    const int Amber    = padColor(3, 3);
    const int Green    = padColor(0, 3);
    const int DimRed   = padColor(1, 0);
    const int DimGreen = padColor(0, 1);
    const int DimAmber = padColor(1, 1);
}

// Round scene/launch buttons to the right of the pads (CC numbers, channel 1).
enum class Button { Up = 108, Down = 109 };

// Pad notes in pad-index order 0..15 (top row 0..7, bottom row 8..15).
inline const int padNotes[16] = {
    40, 41, 42, 43, 48, 49, 50, 51,   // top row    -> index 0..7
    36, 37, 38, 39, 44, 45, 46, 47    // bottom row -> index 8..15
};

} // namespace lk

class LaunchkeyMini {
public:
    // on=false means released; velocity is the raw note/CC value.
    std::function<void(int note, int velocity, bool on)>  onKey;
    std::function<void(int index, int value)>             onKnob;   // index 0..7, value 0..127
    std::function<void(int index, int velocity, bool on)> onPad;    // index 0..15
    std::function<void(lk::Button button, bool pressed)>  onButton;

    // Open the device. Inputs from both ports are merged; the first output port
    // is used for pad LEDs.
    bool connect(const std::string& match = "Launchkey") {
        auto inPorts  = matchingPorts(MidiIn::listDevices(),  match);
        auto outPorts = matchingPorts(MidiOut::listDevices(), match);
        if (inPorts.empty()) return false;

        mainIn_.openPort(inPorts[0]);
        if (inPorts.size() > 1) ctrlIn_.openPort(inPorts[1]);  // merge the 2nd port
        if (!outPorts.empty()) out_.openPort(outPorts[0]);

        clearPads();
        return true;
    }

    // Turn the LEDs off and close the ports.
    void disconnect() {
        if (out_.isOpen()) clearPads();
        mainIn_.closePort();
        ctrlIn_.closePort();
        out_.closePort();
    }

    bool isConnected() const { return mainIn_.isOpen(); }
    bool hasLeds()     const { return out_.isOpen(); }

    // Drain incoming MIDI from both ports and dispatch. Call once per frame.
    void update() {
        drain(mainIn_);
        drain(ctrlIn_);
    }

    // -------------------------------------------------------------------------
    // Pad LEDs. index 0..15: top row 0..7, bottom row 8..15. (Unconfirmed MK1.)
    // -------------------------------------------------------------------------
    void setPadLed(int index, int colorVel) {
        if (!out_.isOpen() || index < 0 || index > 15) return;
        out_.sendNoteOn(10, lk::padNotes[index], colorVel);
    }

    void clearPads() {
        for (int i = 0; i < 16; ++i) setPadLed(i, lk::color::Off);
    }

private:
    static bool padNoteToIndex(int note, int& index) {
        for (int i = 0; i < 16; ++i) {
            if (lk::padNotes[i] == note) { index = i; return true; }
        }
        return false;
    }

    static std::vector<int> matchingPorts(const std::vector<MidiDeviceInfo>& devices,
                                          const std::string& match) {
        std::vector<int> ports;
        for (const auto& d : devices)
            if (d.name.find(match) != std::string::npos) ports.push_back(d.portNumber);
        return ports;
    }

    void drain(MidiIn& in) {
        if (!in.isOpen()) return;
        MidiMessage m;
        while (in.getNextMessage(m)) dispatch(m);
    }

    void dispatch(const MidiMessage& m) {
        if (m.isControlChange()) {
            int cc = m.getControl();
            if (cc >= 21 && cc <= 28) {
                if (onKnob) onKnob(cc - 21, m.getValue());
            } else if (cc == 108 || cc == 109) {
                if (onButton) onButton(static_cast<lk::Button>(cc), m.getValue() > 0);
            }
            return;
        }

        bool noteOn  = (m.getStatus() == MidiStatus::NoteOn);
        bool noteOff = (m.getStatus() == MidiStatus::NoteOff);
        if (!noteOn && !noteOff) return;

        int padIndex;
        // Pads are channel 10; the keyboard is channel 1.
        if (m.getChannel() == 10 && padNoteToIndex(m.getPitch(), padIndex)) {
            if (onPad) onPad(padIndex, m.getVelocity(), m.isNoteOn());
            return;
        }
        if (onKey) onKey(m.getPitch(), m.getVelocity(), m.isNoteOn());
    }

    MidiIn  mainIn_;   // first port  (keys/knobs on ch1, pads on ch10)
    MidiIn  ctrlIn_;   // second port, merged in case messages arrive there
    MidiOut out_;      // pad LEDs (ch10)
};
