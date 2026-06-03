#pragma once

// =============================================================================
// LaunchkeyMini - thin wrapper around a Novation Launchkey Mini [MK1]
// =============================================================================
// Turns the device's raw MIDI into musician-friendly callbacks, and (best
// effort) drives the 2-colour pad LEDs through InControl / "extended" mode.
//
// MK1 layout:
//   - 25 mini keys : Note On/Off on channel 1 (velocity sensitive)
//   - 8 knobs      : CC 21..28 on channel 1 (basic MIDI mode)
//   - 16 pads      : in extended mode -> Note On/Off on channel 16,
//                    notes 96..103 (top row) and 112..119 (bottom row)
//   - round btns   : CC 104 (up) / 105 (down)
//
// LED control (extended mode only): send a Note On on channel 16 to the pad's
// note; the velocity is a 2-colour value = 16*green + red (each 0..3), so the
// palette is red / amber / green only - there is no blue on this hardware.
//
// The Mini exposes two USB-MIDI ports: keys/knobs arrive on the main port,
// while the extended pad messages + LED control live on the second
// "InControl" port. We open whatever we can find and merge the input. None of
// this uses SysEx, so it also works over Web MIDI (unlike the Launchpad).
//
// NOTE: extended mode and the exact LED behaviour are documented for the MK2;
// on a real MK1 they are unconfirmed. The keyboard and knobs work regardless -
// the pad LEDs are a bonus that simply stays dark if the device ignores it.
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

// Round transport / track buttons (CC numbers).
enum class Button { Up = 104, Down = 105 };

} // namespace lk

class LaunchkeyMini {
public:
    // on=false means released; velocity is the raw note/CC value.
    std::function<void(int note, int velocity, bool on)>  onKey;
    std::function<void(int index, int value)>             onKnob;   // index 0..7, value 0..127
    std::function<void(int index, int velocity, bool on)> onPad;    // index 0..15
    std::function<void(lk::Button button, bool pressed)>  onButton;

    // Open the device. Keys/knobs come from the main port; pads + LEDs use the
    // second "InControl" port when present (we fall back to a single port).
    bool connect(const std::string& match = "Launchkey") {
        auto ins  = MidiIn::listDevices();
        auto outs = MidiOut::listDevices();

        int mainInIdx  = pickPort(ins,  match, /*incontrol=*/false);
        int ctrlInIdx  = pickPort(ins,  match, /*incontrol=*/true);
        int ctrlOutIdx = pickPort(outs, match, /*incontrol=*/true);

        if (mainInIdx < 0) return false;
        mainIn_.openPort(mainInIdx);

        // Only open the InControl input if it is a *different* port.
        if (ctrlInIdx >= 0 && ctrlInIdx != mainInIdx) ctrlIn_.openPort(ctrlInIdx);
        if (ctrlOutIdx >= 0) ctrlOut_.openPort(ctrlOutIdx);

        enterExtendedMode();
        clearPads();
        return true;
    }

    // Return the pads to their default mode and close the ports.
    void disconnect() {
        if (ctrlOut_.isOpen()) {
            clearPads();
            exitExtendedMode();
        }
        mainIn_.closePort();
        ctrlIn_.closePort();
        ctrlOut_.closePort();
    }

    bool isConnected() const { return mainIn_.isOpen(); }
    bool hasLeds()     const { return ctrlOut_.isOpen(); }

    // Drain incoming MIDI from both ports and dispatch. Call once per frame.
    void update() {
        drain(mainIn_);
        drain(ctrlIn_);
    }

    // -------------------------------------------------------------------------
    // Pad LEDs (extended mode). index 0..15: top row 0..7, bottom row 8..15.
    // -------------------------------------------------------------------------
    void setPadLed(int index, int colorVel) {
        if (!ctrlOut_.isOpen() || index < 0 || index > 15) return;
        ctrlOut_.sendNoteOn(16, padNote(index), colorVel);
    }

    void clearPads() {
        for (int i = 0; i < 16; ++i) setPadLed(i, lk::color::Off);
    }

private:
    static int padNote(int index) {
        return index < 8 ? (96 + index) : (112 + (index - 8));
    }

    static bool padNoteToIndex(int note, int& index) {
        if (note >= 96  && note <= 103) { index = note - 96;        return true; }
        if (note >= 112 && note <= 119) { index = 8 + (note - 112); return true; }
        return false;
    }

    // Pick a port by name. When InControl is present the device lists two
    // "Launchkey" ports; the second / "InControl" one carries pads + LEDs.
    static int pickPort(const std::vector<MidiDeviceInfo>& devices,
                        const std::string& match, bool incontrol) {
        int firstMatch = -1, lastMatch = -1, namedInControl = -1;
        for (const auto& d : devices) {
            if (d.name.find(match) == std::string::npos) continue;
            if (firstMatch < 0) firstMatch = d.portNumber;
            lastMatch = d.portNumber;
            if (d.name.find("InControl") != std::string::npos ||
                d.name.find("MIDIIN2")   != std::string::npos ||
                d.name.find("MIDI2")     != std::string::npos) {
                namedInControl = d.portNumber;
            }
        }
        if (incontrol) {
            if (namedInControl >= 0) return namedInControl;
            // No helpful names: assume the *second* matching port is InControl.
            return (lastMatch != firstMatch) ? lastMatch : firstMatch;
        }
        return firstMatch;
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
            } else if (cc == 104 || cc == 105) {
                if (onButton) onButton(static_cast<lk::Button>(cc), m.getValue() > 0);
            }
            return;
        }

        bool noteOn  = (m.getStatus() == MidiStatus::NoteOn);
        bool noteOff = (m.getStatus() == MidiStatus::NoteOff);
        if (!noteOn && !noteOff) return;

        int padIndex;
        // Extended-mode pads come in on channel 16; the keyboard is channel 1.
        if (m.getChannel() == 16 && padNoteToIndex(m.getPitch(), padIndex)) {
            if (onPad) onPad(padIndex, m.getVelocity(), m.isNoteOn());
            return;
        }
        if (onKey) onKey(m.getPitch(), m.getVelocity(), m.isNoteOn());
    }

    // InControl / extended-mode enable: Note On (note 12) on channel 1, sent to
    // the InControl port. Documented for the MK2; harmless on an MK1 that
    // doesn't support it (the keyboard keeps working either way).
    void enterExtendedMode() { if (ctrlOut_.isOpen()) ctrlOut_.sendNoteOn(1, 12, 127); }
    void exitExtendedMode()  { if (ctrlOut_.isOpen()) ctrlOut_.sendNoteOn(1, 12, 0); }

    MidiIn  mainIn_;   // keys + knobs (channel 1)
    MidiIn  ctrlIn_;   // extended pads (channel 16), if a second port exists
    MidiOut ctrlOut_;  // LED control + extended-mode enable
};
