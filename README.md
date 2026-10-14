# StreamRack

A dedicated mixer for the livestream. It receives individual channels from the Behringer WING over
Dante, gives each one its own EQ, dynamics and optional VST3/AU plugins, and sends a separate
stereo stream mix to Wirecast. The house mix is never touched.

```
WING ── WING-DANTE card ── network ── Mac in the multimedia room
                                       │
                         Dante Virtual Soundcard (64 inputs)
                                       │
                                  StreamRack
          24 channel strips: input select → trim → HPF → gate → 4-band EQ
                             → compressor → 2 plugin inserts → pan/fader
          reverb bus → master: bus comp → 2 inserts → fader → limiter → loudness meter
                                       │
                 outputs 1-2 → BlackHole → Wirecast → YouTube
                 outputs 3-4 → headphones (optional)
```

## What it does

- **24 channel strips** fed from any of 64 Dante inputs, mono or stereo (stereo uses the selected
  input and the next one).
- **Built-in processing on every channel:** trim, 24 dB/oct high-pass, gate, 4-band EQ
  (shelves plus two parametric mids), compressor and a post-fader send to a shared reverb.
- **Two VST3/AU insert slots per channel, two on the master, and one on the reverb bus.** Plugin latency is compensated
  between channels so inserts don't cause phasing.
- **Master bus:** glue compressor, fader and a look-ahead brickwall limiter (default ceiling -1 dBFS).
- **Loudness meter** (ITU-R BS.1770 / EBU R128): momentary, short-term and integrated loudness
  against the -14 LUFS YouTube target. Green means on target, amber means quiet, red means hot.
- **Scenes:** save and recall full snapshots (names, settings and plugins), for example
  "Sabbath Worship" or "Sabbath School". They are stored in `~/Documents/StreamRack Scenes`.
- **Lock mode for volunteers:** a PIN-protected view that shows only channel names, faders, mutes,
  meters, loudness and scene recall. Lock mode survives a restart.
- Everything autosaves every 30 seconds and on quit, and comes back on the next launch.

## Recommended free plugins

StreamRack's built-in EQ, gate, compressor, reverb and limiter cover the basics. These free, well-regarded
plugins add the "color". All of them have native Apple Silicon builds in VST3 and AU. Download them from
the developers' own sites only.

| Plugin | Use it for | Where it goes |
|---|---|---|
| [TDR Nova](https://www.tokyodawn.net) (Tokyo Dawn Labs) | Dynamic EQ: tames a boomy pastor voice only when it booms, and works as a de-esser (a dynamic band around 6-8 kHz) | Speech and vocal channels |
| [TDR Kotelnikov](https://www.tokyodawn.net) (Tokyo Dawn Labs) | Clean, transparent bus compression that glues the mix | Master insert 1 |
| [Analog Obsession](https://www.patreon.com/analogobsession) ATONE, BritBundle, dB Comp | Console-style EQ and compressor character (warmth, punch) | Vocals, keys, bass |
| [Klanghelm DC1A](https://klanghelm.com) | One-knob musical compressor | Any channel that needs evening out |
| [Bertom Denoiser Classic](https://www.kvraudio.com/product/denoiser-classic-by-bertom-audio) | Zero-latency hiss and room-noise reduction | Lavalier, podium and room mics |
| [Valhalla Supermassive](https://valhalladsp.com) | Lush reverbs and delays | The reverb bus slot (set **Mix to 100%**) |
| [Kilohearts Essentials](https://kilohearts.com) | Saturation, transient shaper, stereo width, clipper | Wherever needed |

### Starting points

- **Pastor / speech:** HPF 100 Hz → Denoiser Classic (only if the mic is noisy) → Nova with a dynamic
  cut around 200-300 Hz and a de-ess band at 6-8 kHz → built-in compressor 3:1. No reverb.
- **Lead vocal:** HPF 100 Hz → Nova de-ess → ATONE or DC1A for character → a little reverb send.
- **Band:** keep it light: HPF, a broad EQ move, and reverb on keys/pads. Let the master bus glue it.
- **Master:** Kotelnikov at about 2:1 and 1-2 dB of gain reduction → built-in limiter at -1 dB.
  Ride the master fader so short-term loudness sits around -14 LUFS.

Use the **reverb bus slot** (in the master panel's REVERB BUS section) rather than a channel insert for
reverb, so all channels share one reverb. When a plugin is loaded there it replaces the built-in reverb;
set the plugin to 100% wet, and the channels' Reverb knobs control how much each one sends.

## One-time setup

### 1. WING → Dante

1. Install the **WING-DANTE** expansion card in the WING.
2. In the WING routing, send each channel you want on the stream to a Dante output on the card.
   Use a **tap point before the fader** (post-EQ/dynamics if available) so FOH fader moves don't
   change the stream balance. Check which tap points your firmware offers for card outputs.
3. Leave the WING card as the Dante clock leader at **48 kHz**.

### 2. The Mac

1. Install **Dante Virtual Soundcard** (Audinate licence) and **Dante Controller**. Set DVS to
   48 kHz and route the WING card's channels to DVS receive channels in Dante Controller.
2. Install **BlackHole 2ch** (free virtual audio cable) for the feed into Wirecast.
3. Open **Audio MIDI Setup → + → Create Aggregate Device** and tick:
   - Dante Virtual Soundcard, set as the **Clock Source**
   - BlackHole 2ch, with **Drift Correction** on
   - (optional) your headphone output, with Drift Correction on

   StreamRack sends the stream mix to every output pair, so BlackHole and the headphones both
   get it.
4. In StreamRack click **Audio...** and choose the aggregate device for input and output,
   at 48 kHz. A buffer of 256 samples is plenty; latency doesn't matter for a stream.
5. In **Wirecast** set the audio source to **BlackHole 2ch**. Use Wirecast's audio delay to line
   the audio up with the video (the video is usually the one that is late).

### 3. Plugins (optional)

Click **Plugins... → Options... → Scan for new or updated VST3 plug-ins** (and AudioUnit). Scanning
runs inside the app; if a plugin crashes the scan, relaunch and scan again and that plugin will be
skipped. Then click any **+ insert** button to load one.

### 4. Lock it for Sabbath

Name the channels (double-click a name), set the mix, save a scene, then click **Lock** and choose
a PIN. Volunteers get the simple view. **Unlock** asks for the PIN.

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
The app isn't code-signed, so the first time you open it, right-click it and choose **Open**.

To use an existing JUCE checkout instead of downloading it, pass `-DSTREAMRACK_JUCE_DIR=/path/to/JUCE`.

## Code layout

| Path | What's there |
|---|---|
| `Source/Engine/RackProcessor.*` | The engine: routing, reverb, master bus, state save/restore |
| `Source/Engine/ChannelStrip.*` | One channel's processing chain |
| `Source/Engine/Dsp.h` | EQ filters, compressor, gate, limiter, delay compensation |
| `Source/Engine/LoudnessMeter.h` | BS.1770 loudness measurement |
| `Source/Engine/PluginSlot.*`, `PluginHost.*` | VST3/AU hosting |
| `Source/Engine/Params.*` | Every parameter, its range and its default |
| `Source/UI/*` | Main window, strips, channel editor, master panel, plugin windows |
| `Tests/EngineTests.cpp` | Headless tests: loudness against EBU reference signals, limiter ceiling, EQ, compressor, routing, state |

## Licensing

StreamRack is built on [JUCE](https://juce.com). JUCE is dual-licensed (AGPLv3 or a commercial
licence); check that your use fits one of them before distributing builds outside your church.
VST is a trademark of Steinberg Media Technologies GmbH.

## Ideas for later

- True-peak (oversampled) limiting and metering
- Out-of-process plugin scanning so a bad plugin can't crash a scan
- EQ curve display and per-channel presets ("Pastor lav", "Acoustic guitar")
- Solo/PFL to the headphone output
- Remote control from a tablet (OSC or a small web page)
