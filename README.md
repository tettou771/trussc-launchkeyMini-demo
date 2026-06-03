# trussc-launchkeyMini-demo

A small [TrussC](https://github.com/TrussC-org/TrussC) demo for the Novation
**Launchkey Mini [MK1]**. It is a real example of the MIDI add-on
**[tcxMidi](https://github.com/TrussC-org/TrussC/tree/main/addons/tcxMidi)**.

Play the keys: they drive TrussC's ChipSound as a polyphonic chiptune synth.
The 8 knobs shape the sound live, the pads pick presets and octaves, and the
screen mirrors everything the hardware sends.

![launchkey mini chiptune synth](docs/screenshot.png)

## What you need

- A **Novation Launchkey Mini Mk1** (the original)
- TrussC (with the `trusscli` tool)
- The `tcxMidi` add-on (it ships with TrussC and is listed in `addons.make`,
  so it is found automatically)

## Build & run

```bash
git clone git@github.com:tettou771/trussc-launchkeyMini-demo.git
cd trussc-launchkeyMini-demo
trusscli update
trusscli run
```

Plug in the Launchkey first. The app finds it and connects on its own. With no
device it still starts, but the keys/knobs/pads come from MIDI, so nothing
plays until you connect one.

## Controls

| Control | What it does |
|---------|--------------|
| **Keys** | Play notes (velocity sensitive, polyphonic). |
| **Knobs** | Shape the live patch: WAVE / ATTACK / DECAY / SUSTAIN / RELEASE / LENGTH / DETUNE / VOLUME. Every new note is built from the current knob values. |
| **Top pads** | Pick one of 8 preset patches (Lead, Soft, Buzz, Pad, Pluck, Stab, Bell, Perc). The selected pad lights **green**. |
| **Bottom pads** | Transpose the keyboard by octave (−4 … +3). The selected pad lights **amber**. |
| **Round buttons** | The two round buttons (right of the pads) also shift the octave (up / down). The device's own octave buttons send no MIDI, so these are the MIDI-driven replacement. |

ChipSound renders a fixed-length buffer per note, so holding a key doesn't
sustain past the note length — that's the chiptune grain. Turn the LENGTH and
ADSR knobs to reshape each new note.

## MIDI mapping (Launchkey Mini Mk1)

Probed on a real device — handy if you target the same hardware:

| Control | MIDI |
|---------|------|
| Keys | Note On/Off, **channel 1**, velocity sensitive |
| Knobs 1–8 | **CC 21–28**, channel 1 |
| Round buttons (right of pads) | **CC 108** (top) / **CC 109** (bottom), channel 1 |
| Pads | Note On/Off, **channel 10** (grid below) |
| Octave buttons | **no MIDI** — they shift the keyboard's notes on the device itself |

Pad note numbers (top row 1–8, bottom row 9–16, left → right):

```
top:    40 41 42 43 | 48 49 50 51
bottom: 36 37 38 39 | 44 45 46 47
```

Because the octave buttons don't send MIDI, the note you receive is already
octave-shifted by the device — you can't tell the absolute physical key from the
note number alone.

## Pad LEDs (best effort)

The pads have **red/green 2-colour LEDs** (no blue). The demo lights a pad by
echoing its note back on **channel 10** with a velocity colour = `16*green + red`
(each 0–3). This is unconfirmed on the Mk1 — if the device ignores it the
keyboard and knobs still work and the pads just stay dark. No SysEx is used, so
unlike the Launchpad demo this can also run on the Web (Web MIDI input).

## Other devices

This demo is tuned for the **Launchkey Mini Mk1** (see the mapping table above):

- **Keys** are standard Note On/Off on channel 1, so any MIDI keyboard plays.
- **Knobs** expect **CC 21–28** on channel 1. Other controllers send different CCs.
- **Pads** expect the Mk1's **channel-10** note layout. Other devices differ.

> Making something for another controller? First run tcxMidi's **`example-basic`**
> to see which notes / CCs your device sends, then build from there.
> `example-basic` works with any MIDI device.

## How it works (notes)

The device-specific code (port choice, note/CC layout, LED encoding) lives in
`LaunchkeyMini.h`. The synth is split into small, readable pieces:

- `Patch.h` — the live patch, the 8-knob mapping, and the 8 presets.
- `Voices.h` — a 16-voice pool of one-shot ChipSound notes.
- `tcApp` — wires the device to the synth and draws the scope / knobs / pads /
  keyboard.

The Mini exposes two USB-MIDI ports. The wrapper opens both inputs and merges
them, then routes purely by channel (ch1 → keys/knobs/buttons, ch10 → pads), so
port ordering doesn't matter.

## License

MIT
