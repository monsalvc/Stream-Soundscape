# StreamRack

A dedicated processor for the livestream mix. It takes audio from the Behringer WING over Dante,
adds "color" (tone, glue, saturation, reverb, consistent loudness) with built-in processing and free
VST3/AU plugins, and sends the finished stereo mix on to Wirecast. The house mix is never touched.

```
WING ─ Dante ─► "Color" Mac: Dante Virtual Soundcard
                  StreamRack
                    Bus mode:      stream bus (2 ch) → trim
                    Advanced mode: up to 24 channels → EQ/dynamics/inserts → mix (+ reverb bus)
                    then:          leveler → bus comp → 4 inserts → fader → limiter
                  Dante Virtual Soundcard outputs 1-2
             ─ Dante ─► Wirecast Mac (unchanged) ─► YouTube
```

## Two modes

Switch with the **Bus | Advanced** buttons at the top left. The mode is saved with each scene.

- **Bus mode (default):** the console's stereo stream bus comes in on one Dante pair and goes through
  the master chain. Balance stays on the console; StreamRack adds the color and keeps the level steady.
  The screen shows the incoming level, what the leveler is doing, and the signal flow.
- **Advanced mode:** individual channels come in over Dante and StreamRack mixes them itself, with full
  processing per channel (see below). Use it when you want a different balance or stream-only
  processing for each source.

## What it does

- **Automatic leveler** (both modes): slowly turns the stream up or down toward a loudness target
  (default -16 LUFS, within +/-9 dB) so the stream stays even from worship to sermon to announcements,
  and when FOH rides a post-fader bus. It holds its gain through pauses instead of pumping up room noise.
- **Master chain:** leveler, glue compressor, **four VST3/AU insert slots in order**, master fader and a
  look-ahead brickwall limiter (default ceiling -1 dBFS).
- **Loudness meter** (ITU-R BS.1770 / EBU R128): momentary, short-term and integrated loudness
  against the -14 LUFS YouTube target. Green means on target, amber means quiet, red means hot.
- **Advanced mode channels:** 24 strips fed from any of 64 Dante inputs, mono or stereo, each with
  trim, 24 dB/oct high-pass, pitch correction, gate, 4-band EQ, compressor, two plugin inserts, pan, fader and a send to a
  shared reverb bus (built-in reverb, or a plugin in the reverb bus slot). Plugin latency is compensated
  between channels so inserts don't cause phasing.
- **Vocal pitch correction (Advanced mode):** press **TUNE** in a channel's PITCH section to gently pull
  a singer toward the nearest note, for the stream only (the room still hears the live voice).
  **Amount** sets how far (50-80% sounds natural; 100% is fully in tune) and **Speed** how quickly
  (slower keeps slides and vibrato). The header shows the note being sung and how far off it is.
  It works on one voice per channel, so use it on lead and solo vocal mics, not on choir or band channels.
  It adds about 28 ms of delay, and the other channels are delayed to match automatically.
- **EQ curve view:** press **CURVE** in a channel's EQ section to swap the knobs for a frequency-response
  graph of its high-pass and EQ. Drag a point to set frequency and gain, scroll over a mid band to change
  its Q, double-click a point to reset its gain. The choice is remembered.
- **Scenes:** save and recall full snapshots (mode, names, settings and plugins), for example
  "Sabbath Worship" or "Sabbath School". They are stored in `~/Documents/StreamRack Scenes`.
- **Lock mode for volunteers:** a PIN-protected view that shows only meters, loudness, faders, mutes
  and scene recall. Lock mode survives a restart.
- Everything autosaves every 30 seconds and on quit, and comes back on the next launch.

## Recommended free plugins

StreamRack's built-in EQ, gate, compressor, reverb, leveler and limiter cover the basics. These free,
well-regarded plugins add the "color". They are available for macOS as VST3/AU; check each developer's
site for the current Apple Silicon build before installing, and download only from the developers' own sites.

| Plugin | Use it for | Where it goes |
|---|---|---|
| [TDR Nova](https://www.tokyodawn.net) (Tokyo Dawn Labs) | Dynamic EQ: tames a boomy pastor voice only when it booms, and works as a de-esser (a dynamic band around 6-8 kHz) | Speech and vocal channels |
| [TDR Kotelnikov](https://www.tokyodawn.net) (Tokyo Dawn Labs) | Clean, transparent bus compression that glues the mix | Master insert 1 |
| [Analog Obsession](https://www.patreon.com/analogobsession) ATONE, BritBundle, dB Comp | Console-style EQ and compressor character (warmth, punch) | Vocals, keys, bass |
| [Klanghelm DC1A](https://klanghelm.com) | One-knob musical compressor | Any channel that needs evening out |
| [Bertom Denoiser Classic](https://www.kvraudio.com/product/denoiser-classic-by-bertom-audio) | Zero-latency hiss and room-noise reduction | Lavalier, podium and room mics |
| [Valhalla Supermassive](https://valhalladsp.com) | Lush reverbs and delays | The reverb bus slot (set **Mix to 100%**) |
| [Kilohearts Essentials](https://kilohearts.com) | Saturation, transient shaper, stereo width, clipper | Wherever needed |
| [Softube Saturation Knob](https://www.softube.com/saturationknob) | One-knob warmth/grit (needs a free Softube + iLok account) | Master insert, vocals |
| [Klanghelm IVGI](https://klanghelm.com) | Subtle analog-style saturation and crosstalk ("glue") | Master insert |
| [Voxengo Tube Amp](https://www.voxengo.com) | Tube-style drive (closest free relative of Magma BB Tubes) | Master insert, vocals |
| [Analog Obsession TUBA](https://www.patreon.com/analogobsession) | Tube harmonics, adjustable low/high balance | Master insert, vocals |
| [Slate Digital Fresh Air](https://slatedigital.com) | High-frequency exciter for "air" and clarity (needs a free iLok account) | Master insert, vocals |

### Starting points

- **Pastor / speech:** HPF 100 Hz → Denoiser Classic (only if the mic is noisy) → Nova with a dynamic
  cut around 200-300 Hz and a de-ess band at 6-8 kHz → built-in compressor 3:1. No reverb.
- **Lead vocal:** HPF 100 Hz → Nova de-ess → ATONE or DC1A for character → a little reverb send.
- **Band:** keep it light: HPF, a broad EQ move, and reverb on keys/pads. Let the master bus glue it.
- **Master / Bus mode:** leveler on → insert 1 Nova (tame mud around 250 Hz and harshness around
  3 kHz) → insert 2 a saturator (Saturation Knob, IVGI or Tube Amp, kept subtle) → insert 3 Fresh Air
  (a touch of air) → insert 4 Kotelnikov at about 2:1 with 1-2 dB of gain reduction → built-in limiter
  at -1 dB. Aim for a short-term loudness around -14 LUFS.

Use the **reverb bus slot** (in the master panel's REVERB BUS section) rather than a channel insert for
reverb, so all channels share one reverb. When a plugin is loaded there it replaces the built-in reverb;
set the plugin to 100% wet, and the channels' Reverb knobs control how much each one sends.

## One-time setup

### 1. WING → Dante

1. The **WING-DANTE** card sends the stream bus (Bus mode) or individual channels (Advanced mode).
2. For Advanced mode, send each channel to a Dante output using a **tap point before the fader**
   (post-EQ/dynamics if available) so FOH fader moves don't change the stream balance. For Bus mode,
   ask whoever runs the bus whether its sends can be **pre-fader** for the same reason.
3. Leave the WING card as the Dante clock leader at **48 kHz**.

### 2. The "color" Mac (runs StreamRack)

1. Install **Dante Virtual Soundcard** (each Mac needs its own licence) and **Dante Controller**.
   Set DVS to 48 kHz.
2. In StreamRack click **Audio...** and choose **Dante Virtual Soundcard** for both input and output,
   at 48 kHz. Using one device for both keeps everything on the Dante clock. A buffer of 256 samples
   is plenty; latency doesn't matter for a stream.
3. In **Dante Controller**:
   - WING card → this Mac's DVS receive channels (the bus pair, or the individual channels).
   - This Mac's DVS transmit channels **1-2** → the Wirecast Mac's DVS receive channels that Wirecast
     already uses. StreamRack sends the finished mix to every output pair, so 1-2 is all you need.
4. In Bus mode, pick the Dante pair the bus arrives on in **STREAM BUS IN** and check the meters move.
   If the WING sends several buses (say the stream bus on 1-2 and a youth-service bus on 3-4),
   select each pair and click **Name...**. Every named pair gets a one-click button under the
   dropdown with a dot that lights when that pair has signal. Switching crossfades over 50 ms, so it
   doesn't click on the stream. Names are saved with the session and in scenes.

### 3. The Wirecast Mac

Nothing changes: it keeps receiving over Dante. After the first test, use Wirecast's audio delay to
line the audio up with the video (the extra hop adds a few milliseconds).

**Backup:** the color Mac is now in the audio path. Also route the WING's raw bus straight to two
spare DVS inputs on the Wirecast Mac and set up a second Wirecast audio source for it, so a volunteer
can switch to it in seconds if the color Mac ever freezes.

**Running StreamRack on the Wirecast Mac instead** (one computer): install **BlackHole 2ch**, create an
**Aggregate Device** in Audio MIDI Setup with DVS as the clock source and BlackHole (plus headphones,
if wanted) with drift correction on, choose it in StreamRack's **Audio...**, and set Wirecast's audio
source to BlackHole.

### Using Waves SoundGrid instead of Dante

StreamRack doesn't care which network carries the audio: it uses whatever Mac audio device you pick.
With the **Waves SoundGrid Driver** installed, choose the SoundGrid device in **Audio...** (for input
and output) and patch channels in SoundGrid Studio the same way you would in Dante Controller. Waves
plugins installed through Waves Central load in StreamRack's inserts like any AU/VST3 plugin; they
run on the Mac's processor, not on a SoundGrid DSP server. (This path is covered by simulated-device
tests but has not been tried on real SoundGrid hardware yet.)

### 4. Plugins (optional)

Click **Plugins... → Options... → Scan for new or updated VST3 plug-ins** (and AudioUnit). Scanning
runs inside the app; if a plugin crashes the scan, relaunch and scan again and that plugin will be
skipped. Then click any **+ insert** button to load one.

### 5. Lock it for Sabbath

Set things up, save a scene, then click **Lock** and choose a PIN. Volunteers get the simple view
(meters, loudness, faders and scene recall). **Unlock** asks for the PIN.

## Building from source

Requires CMake 3.22+ and Xcode (or the Command Line Tools). JUCE 8 is downloaded automatically.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
./build/StreamRackTests_artefacts/Release/StreamRackTests   # engine tests
open build/StreamRack_artefacts/Release/StreamRack.app
```

Every push also builds an Apple Silicon macOS app in GitHub Actions (**Actions → Build → Artifacts**).
CI skips link-time optimisation to keep builds quick; local builds use it by default
(`-DSTREAMRACK_LTO=OFF` turns it off, `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"` makes an Intel + Apple Silicon build).
The download is a zip containing **StreamRack.dmg**: open the disk image and drag StreamRack into
Applications. The app has an ad-hoc signature but isn't notarized by Apple, so the first time you open
it macOS will refuse; go to **System Settings → Privacy & Security** and click **Open Anyway**. If macOS
still says the app is damaged, run `xattr -dr com.apple.quarantine /Applications/StreamRack.app` once.

To use an existing JUCE checkout instead of downloading it, pass `-DSTREAMRACK_JUCE_DIR=/path/to/JUCE`.

## Code layout

| Path | What's there |
|---|---|
| `Source/Engine/RackProcessor.*` | The engine: routing, reverb, master bus, state save/restore |
| `Source/Engine/ChannelStrip.*` | One channel's processing chain |
| `Source/Engine/Dsp.h` | EQ filters, compressor, gate, limiter, delay compensation |
| `Source/Engine/LoudnessMeter.h` | BS.1770 loudness measurement |
| `Source/Engine/Leveler.h` | Automatic leveler |
| `Source/Engine/PitchCorrector.h` | Vocal pitch detection (YIN) and pitch-synchronous shifter |
| `Source/Engine/PluginSlot.*`, `PluginHost.*` | VST3/AU hosting |
| `Source/Engine/Params.*` | Every parameter, its range and its default |
| `Source/UI/*` | Main window, Bus mode screen, strips, channel editor and EQ curve, master panel, plugin windows |
| `Tests/EngineTests.cpp` | Headless tests: loudness against EBU reference signals, limiter ceiling, EQ, compressor, leveler, pitch correction accuracy/stability/latency, Bus mode and channel routing, state, and the full device path with simulated Dante/SoundGrid-style devices (channel counts, 44.1-96 kHz, 32-1024 sample buffers) |

## Licensing

StreamRack is built on [JUCE](https://juce.com). JUCE is dual-licensed (AGPLv3 or a commercial
licence); check that your use fits one of them before distributing builds outside your church.
VST is a trademark of Steinberg Media Technologies GmbH.

## Ideas for later

- True-peak (oversampled) limiting and metering
- Out-of-process plugin scanning so a bad plugin can't crash a scan
- Per-channel presets ("Pastor lav", "Acoustic guitar")
- Solo/PFL to the headphone output
- Remote control from a tablet (OSC or a small web page)
