![Call of Duty 2 VR](docs/images/banner.jpg)

# CALL OF DUTY 2 VR

A PC VR mod for the **Call of Duty 2 single player campaign** (`CoD2SP_s.exe` 1.3), from the makers of the
[Call of Duty 1 VR mod](https://github.com/lambda-gab/cod1-vr).

> **Disclaimer:** unofficial fan project, not affiliated with or endorsed by Activision. Call of Duty is a
> trademark of Activision Publishing, Inc. A legitimate copy of the game is required; no game code or game files
> are included or distributed.

> [!WARNING]
> **Early development (v0.1.0).** The stereo rendering and the headset path work without a headset (side by side
> on the monitor and a simulated headset); **it has not been tested in a headset yet.**

## What works

- Stereo rendering of the world from each tracked eye, with each eye's own asymmetric field of view and the
  headset's resolution (one frontend pass, the renderer's command list executed once per eye).
- OpenXR (32-bit runtime: Virtual Desktop, Oculus/Meta, SteamVR beta), with the game's Direct3D 9 device turned
  into a D3D9Ex device that shares the eye images with a D3D11 OpenXR session.
- Head tracking (rotation and position); the game aims where you look.
- HUD and menus on a floating screen; menus stay where they opened, and you point at them with the controller
  (a beam from the hand, drawn in a layer of its own over everything).
- Straight to the main menu (no legal screen and logo videos).
- The first person arms hidden, so only the gun is drawn (optional data pack, see below).
- Touch controllers: buttons to game commands, stick walking, snap or smooth turning, stance on the stick.
- Without a headset: both eyes side by side on the monitor.
- Launcher with PLAY, CONTROLS, GRAPHICS, VR and ABOUT.

Not yet: the gun in your hand, weapons on the body, hand aiming, scopes, roomscale walking, physical crouch,
vibration, vehicles, comfort vignette, the MODS page and the installer of the CoD 1 mod.

## Install (manual)

1. Build (below) or take `d3d9.dll`, `openxr_loader.dll`, `cod2_vr.ini` and `CoD2-VR-Launcher.exe`.
2. Copy them to the game folder (where `CoD2SP_s.exe` is).
3. Start `CoD2-VR-Launcher.exe` and press PLAY. It starts the game with
   `+set r_smp_backend 0 +set r_aasamples 1 +set r_fullscreen 0` (the mod needs the renderer's backend on the
   game's thread and the eye images without multisampling).

Delete those files to uninstall.

### Hiding the arms (optional data pack)

```
python tools/build_iwd.py "<game folder>"
```

Then copy `mods/out/zz_cod2vr.iwd` to `<game folder>\main`. It is made from your own copy of the game: the
first person arm models (`xmodel/viewmodel_hands*`) point at a new material whose texture is fully transparent.
Delete it to get the arms back.

## Controls

| Control | Action |
|---|---|
| Right trigger | Fire |
| Left trigger | Aim down the sights (hold) |
| Right grip | Use / pick up |
| Left grip | Melee |
| Left stick | Move (where you look) |
| Right stick | Turn (45° snap); up / down: stance |
| A / B | Jump / reload |
| X / Y | Grenade / next weapon |
| Left stick click | Objectives |
| Right stick click | Go prone |
| Menu | Game menu |

In menus: point with the right hand, trigger clicks, B goes back, A confirms. Buttons are in `[Buttons]` of
`cod2_vr.ini` (`+cmd` is held while pressed, `a|b` runs `a` on press and `b` on release, `key:NAME` sends a key).

## Building

Visual Studio with the C++ tools (x86), and the Khronos OpenXR loader in `deps/openxr/`
(`openxr_loader_windows-<version>.zip` from
[OpenXR-SDK-Source releases](https://github.com/KhronosGroup/OpenXR-SDK-Source/releases)).

- `proxy\build.bat` → `proxy\out\d3d9.dll`, `openxr_loader.dll`, `cod2_vr.ini`
- `launcher\build_launcher.bat` → `launcher\out\CoD2-VR-Launcher.exe`

## Testing without a headset

`cod2_vr.ini`, `[Debug]`:

- `FakeHeadset=1`: the whole headset path with made-up eyes (Quest 3 like fov, `FakeEyeWidth` x `FakeEyeHeight`),
  shown side by side with the HUD screen in the lower right corner.
- `TraceCommands=N`: logs the renderer's backend commands of N frames with the world.

The log is `cod2_vr.log` (under Program Files it lands in `%LOCALAPPDATA%\VirtualStore\...`).

## Technical notes

How it was reverse engineered and how the stereo works: [docs/CoD2_VR_RE.md](docs/CoD2_VR_RE.md) (Portuguese).
`tools/` has the REA client (`reacli.mjs`, `rq.py`) and the capstone helpers (`redis.py`) used for it.
