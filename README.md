# Melton Dual Feed Recorder

Native OBS Studio plugin (C++17, Qt, CMake) for OBS **32.2.2**, Windows 11 x64.

One recording command, two synchronized files:

| File | What it is |
|------|------------|
| `DIRTY_*.mkv` | OBS's own recording: full Program with graphics, unchanged. The plugin does not touch it. |
| `CLEAN_*.mkv` | Plugin-owned recording: same camera cuts, same transitions, same OBS audio mix, **without the sources you exclude**. |

OBS stays your only switcher. The plugin reads the real Program output every frame and never changes your scenes, Preview, Program or transitions.

## How it works

* **Following Program.** Each graphics frame the plugin reads OBS's Program output (the transition on output channel 0) and the scene it is on or moving to. Preview is never used. (OBS's `SCENE_CHANGED` event only fires *after* a transition ends, so it is not used for timing.)
* **Clean composition.** A private `obs_view_t` + video mix renders a mirror of the Program scene:
  * A scene that contains an excluded source (directly, in a nested scene, or in a group) is rebuilt as a private mirror scene: same items, order, transforms, crop, scale filter, blending and visibility, minus the excluded sources.
  * A scene with nothing excluded is used as-is (exact, zero copy).
  * Mirror scenes reference the **same sources**, so no camera or capture device is opened twice.
  * Mirrors rebuild automatically (coalesced) when scenes change, and are swapped in without a black frame.
  * Works for both layouts: Program = camera scene, or Program = master scene with a nested camera scene plus graphics.
* **Transitions.** On each Program change the plugin starts a private clone of the same transition (same type, settings, per-scene override and duration). If a transition cannot be cloned, or its duration is 0, the clean feed **cuts** to the correct scene. It never shows a wrong scene or black.
* **Clean encoding.** Its own encoder (hardware preferred, x264 fallback) feeds an `ffmpeg_muxer` output. Format MKV (default) or fragmented MP4.
* **Audio.** An AAC encoder attached to OBS's global audio pipeline on the same mixer track as your dirty recording (Track 1 by default). No audio device is opened a second time.
* **Sync.** The clean mix is rendered by OBS's own graphics thread, so video shares OBS's clock and audio comes from the same pipeline.

## Modes

* **Linked** (default): OBS recording starts -> clean recording starts; OBS recording stops -> clean stops. Works from OBS's own Start Recording button, hotkeys, or the dock.
* **Independent**: Start/Stop Clean on their own; native recording is never stopped by the plugin. The dock warns when clean records while OBS native recording is off.

## Dock: MELTON DUAL FEED

Status (OBS, Program scene, DIRTY/CLEAN indicators, durations, free storage, file), buttons (Start Both, Stop Both, Start Clean, Stop Clean, Open Recording Folder), **Clean feed settings** tab (resolution, FPS, encoder, bitrates, audio track, format, destination, filename format, mode), **Graphics exclusion** tab (tick sources to hide, scene-structure view, test preview). Everything saves automatically.

## Files and folders

Clean files: `<destination>/<YYYY>/<MM>/<DD>/CLEAN_<time>.mkv` (default destination `Documents\Melton Recordings`). Existing files are never overwritten (`_2`, `_3`...).

The dirty file location is controlled by OBS (Settings -> Output -> Recording). To keep both together set the OBS recording path to your Melton folder and the filename format to `DIRTY_%CCYY-%MM-%DD_%hh%mm%ss`. The plugin does not edit your OBS profile.

## Build (Windows 11, Visual Studio 2022)

Prerequisites: Visual Studio 2022 (Desktop C++), CMake 3.28+, Git.

```
cmake --preset windows-x64
cmake --build --preset windows-x64 --config RelWithDebInfo
```

The first configure downloads OBS 32.2.2 sources, obs-deps and Qt6 as pinned in `buildspec.json`. Install by copying:

* `build_x64\RelWithDebInfo\melton-dual-feed-recorder.dll` -> `C:\Program Files\obs-studio\obs-plugins\64bit\`
* `data\locale\en-US.ini` -> `C:\Program Files\obs-studio\data\obs-plugins\melton-dual-feed-recorder\locale\`

Restart OBS. Dock: View -> Docks -> MELTON DUAL FEED.

## Status of this delivery (read this)

* Every source file was compiled (`-fsyntax-only -Wall -Wextra`, zero warnings/errors) against the **real OBS 32.2.2 headers**, and the Qt headers were run through `moc`. All OBS API names/signatures used were checked in that source tree.
* **Not done:** no Windows DLL was built, the plugin was never loaded into OBS, and none of the 8 tests in `docs/TEST-PLAN.md` have been run. Treat the first run as a test session, not production.
* `buildspec.json` pins OBS sources 32.2.2 (hash computed from the release archive). obs-deps/Qt pins are the template's (2025-07-11); if configure reports a hash mismatch or missing deps, update those entries from the OBS 32.2.2 release's dependency versions.

## Showing the clean feed somewhere else

Three ways, all driven by the same clean renderer:

1. **Projector window (dock).** In the MELTON DUAL FEED dock pick a monitor and click **Fullscreen projector** (or **Windowed projector**). Esc or double-click closes it. This arms the clean feed automatically.
2. **"Melton Clean Feed" source.** Add Sources -> *Melton Clean Feed* to any scene. Use a dedicated output scene that is never your Program. Then use OBS itself:
   * right-click the source or scene -> **Fullscreen Projector (Source/Scene)** -> choose the monitor, or
   * Start Virtual Camera with output type *Source* or *Scene*, to feed Zoom/Teams/etc., or
   * send that scene/source out through an NDI plugin (e.g. DistroAV) if you use one (not tested here).
3. **Recording** keeps working as before; an armed feed is reused, not restarted.

**Arm clean feed** keeps the clean mix running at the configured size/FPS without recording (costs one extra render; no encoder). Recording starting or stopping does not interrupt an armed feed. The source is always left out of the clean composition itself (no feedback loop). The source carries video only; programme audio is the normal OBS mix.

## Known limitations

1. Transitions are replicated by type/settings/duration started when the Program target changes. Same look, not frame-locked to OBS's transition. Studio-mode T-bar/manual transitions and fade-to-black are not mirrored (clean cuts to the right scene).
2. Clean FPS cannot exceed OBS's FPS (it is clamped and logged).
3. Groups containing items are mirrored best-effort (re-created as private groups).
4. The clean feed is a second render + second encoder. On a modest PC prefer a hardware encoder or 720p30; x264 at 1080p30 is CPU heavy. The dock warns on skipped/dropped frames and when OBS render time nears the frame budget.
5. Preview needs the clean mix running; it uses a low-cost 640x360 mix until you record.
6. Both files start within the delay between OBS's "recording started" event and the clean output starting (typically well under a second); durations can differ by that much.
