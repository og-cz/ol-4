# OL-4 Optical Leveler

[![Downloads](https://img.shields.io/github/downloads/og-cz/ol4-optical-leveler/total?label=Downloads&color=6b5b3a)](https://github.com/og-cz/ol4-optical-leveler/releases) [![Platform](https://img.shields.io/badge/Platform-Windows%20%7C%20macOS%20%7C%20Linux-0078D6)](#download) [![Formats](https://img.shields.io/badge/Formats-VST3%20%7C%20AU%20%7C%20LV2%20%7C%20AAX-556b2f)](#download) [![License](https://img.shields.io/badge/License-AGPL--3.0-blue)](#license)

---

A solid-state opto compressor/limiter plugin modeled on the behavior of the late-70s UREI LA-4 / LA-4A.
Independent project; not affiliated with Universal Audio, UREI or JBL.

**Formats:** VST3 (Windows, macOS, Linux) · AU (macOS) · Standalone app · LV2 (Linux) · AAX (Pro Tools, see below)

<img src="docs/screenshot.webp" alt="OL-4 Optical Leveler, powered on (top) and off (bottom)" width="100%">

## Download

Every push builds installers on GitHub Actions. Open the **Actions** tab, pick the latest green run and
download **OL-4-Windows** or **OL-4-macOS** under *Artifacts*. Tagged versions (`v1.0.0`, …) also appear
under **Releases**. Install notes: `installer/INSTALL-Windows.txt`, `installer/INSTALL-macOS.txt`.

## Controls (same as the hardware)

| Control | What it does |
|---|---|
| Threshold (inner knob) | Level where compression starts. Turn up for more gain reduction. |
| Ratio (outer clear ring) | 2:1 (soft knee, threshold auto-lowered), 4:1, 8:1, 12:1, 20:1. 8:1 and up is limiting. |
| Output Level (inner knob) | Make-up gain, ±20 dB, 5 = unity. |
| Meter (outer clear ring) | GR = gain reduction, +4 / +8 = output level. |
| Stereo | Links both channels so the image stays put. |
| Power | Off = bypass (crossfaded, no click). |

Drag knobs up/down (Shift for fine), scroll wheel works, double-click resets. Click or drag the outer rings.

## What is modeled

* Sidechain: LED driven mostly by the attenuated signal plus a little of the input (LA-4A topology),
  soft diode turn-on (2:1 is a soft knee), CdS photocell with light-dependent attack and
  program-dependent "memory" release.
* Attack 1–10 ms (63 %), release 0.1–1 s depending on how long it has been compressing (manual specs).
* RC4136 op-amp slew and upper-mid grit, darkening under heavy gain reduction, output transformer weight.
* Zero latency; identical output for any buffer size; 44.1–192 kHz.

The bench in the web prototype checks 40 measurements against the LA-4 manual.
The exact knee and darkening are estimates from published notes; a recording through a real unit would refine them.

## Build it yourself

You need an internet connection the first time (CMake downloads JUCE 9.0.4 automatically).

### Windows (about 15 minutes the first time)

1. Install **Visual Studio 2022 Community** (free) with the **"Desktop development with C++"** workload.
   It includes CMake. Also install **Git for Windows**.
2. Unzip this project, e.g. to `C:\ol4-plugin`.
3. Open **"x64 Native Tools Command Prompt for VS 2022"** from the Start menu and run:
   ```
   cd C:\ol4-plugin
   cmake -S . -B build -DOL4_BUILD_TESTS=OFF
   cmake --build build --config Release
   ```
4. Copy `build\OL4_artefacts\Release\VST3\OL-4 Optical Leveler.vst3` (the whole folder) to
   `C:\Program Files\Common Files\VST3`. The Standalone app is
   `build\OL4_artefacts\Release\Standalone\OL-4 Optical Leveler.exe`.
5. In FL Studio: *Options > Manage plugins > Find installed plugins*, then load it on a mixer insert.

Optional installer: install Inno Setup 6 and run
`ISCC /DSrcDir="C:\ol4-plugin\build\OL4_artefacts\Release" installer\windows.iss`.

### macOS (Apple Silicon or Intel)

1. Install **Xcode** from the App Store, open it once, then in Terminal run `xcode-select --install`.
   Install **CMake** (`brew install cmake`, or the app from cmake.org).
2. In Terminal:
   ```
   cd ~/Downloads/ol4-plugin
   cmake -S . -B build -G Xcode -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DOL4_BUILD_TESTS=OFF
   cmake --build build --config Release
   ```
3. Copy the plugins into place:
   ```
   cp -R "build/OL4_artefacts/Release/VST3/OL-4 Optical Leveler.vst3" ~/Library/Audio/Plug-Ins/VST3/
   cp -R "build/OL4_artefacts/Release/AU/OL-4 Optical Leveler.component" ~/Library/Audio/Plug-Ins/Components/
   ```
   Plugins you build on your own Mac are not blocked by Gatekeeper.
4. Optional installer package: `bash installer/make-macos-pkg.sh 1.0.0 build/OL4_artefacts/Release dist`.

### Automatic builds (later, optional)

If you put this folder in a GitHub repository, `.github/workflows/build.yml` builds the Windows installer,
the macOS installer and a Linux build on every push, runs the tests and pluginval, and attaches the
installers to a release when you push a tag such as `v1.0.0`.

## Pro Tools (AAX)

Pro Tools only loads AAX plugins, and Avid requires two things that must be done in your own name:

1. Join the free AAX developer program at <https://developer.avid.com/aax>, download the AAX SDK,
   and request a **Pro Tools Developer** activation (it loads unsigned plugins for testing).
2. To run in normal Pro Tools, plugins must be signed with **PACE** tools. Email audiosdk@avid.com
   ("PACE Eden Signing Tools Request") with a plugin overview and a screen recording of it running in
   Pro Tools Developer. Avid provides the tools free of charge.

Then build with `-DOL4_AAX_SDK=/path/to/aax-sdk` and sign the `.aaxplugin` with PACE `wraptool`.

## License

AGPL-3.0 (this project and the JUCE framework under its AGPLv3 option). If you want to sell a closed-source
version, you need a commercial JUCE licence: see <https://juce.com/get-juce/>.
