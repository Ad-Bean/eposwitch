# eposwitch

Open-source control panel for the **EPOS H3PRO** headset (and other audio endpoints on
the same machine), written to replace the discontinued EPOS Gaming Suite — single
executable, no installer, no driver, no runtime dependencies.

> Unofficial community project. Not affiliated with or endorsed by EPOS / Demant.
> "EPOS" is a trademark of its respective owner.

## Features

- **Device manager** — list *all* playback/recording endpoints (including virtual ones
  like NVIDIA Broadcast, grouped separately), switch the default device with one click,
  per-device volume slider and mute.
- **Volume mixer** — per-application volume and mute (`IAudioSessionManager2`).
- **EQ & effects** — 9-band speaker/mic EQ, reverb, noise gate, sidetone, 3D surround,
  voice enhance; factory + custom presets, applied through the same APO channel the
  official app uses.
- **Per-app EQ presets** — rules like `cs2.exe = ESPORT` automatically switch presets
  when a game comes to the foreground (editable in-app, stored in `profiles.txt`).
- **Live ANC state** — reads the headset's ANC on/off through the dongle's vendor HID
  interface (read-only; press the ANC key once to sync). See *How it works*.
- **Global hotkeys** — configurable/rebindable (cycle output, mute, cycle EQ preset),
  with a master on/off switch.
- **Extras** — low-battery notifications, auto-switch on headset connect, restore on
  reconnect, autostart, tray mode, Chinese/English UI, crash logging.

## Build

MinGW-w64 (GCC 13+ tested with 15.2):

```bat
windres app.rc -O coff -o app_res.o
g++ -std=c++17 -O2 -municode -mwindows -s -static -o eposwitch.exe main.cpp app_res.o ^
    -lgdiplus -lole32 -luuid -loleaut32 -lshlwapi -lwinmm -lversion -lcomdlg32 -lsetupapi -lhid
```

The result is a single ~1.4 MB `eposwitch.exe` with no external dependencies.

## Usage

1. Quit the official Gaming Suite if it is running (it holds the pipe this tool needs).
2. Start `eposwitch.exe`. Settings live next to the exe:
   `settings.ini`, `profiles.txt` (per-app rules), `presets.txt` (custom EQ).
3. Closing the window minimizes to the tray; right-click the tray icon to exit.

## How it works

- **Device/effect control** talks to the EPOS agent service over the same named pipes
  the official UI uses (`GSAgentPipe` / `GSUIPipe`), speaking its JSON protocol
  (22+ request types; EQ/effects go through the Windows APO channel via `rt=13`).
- **Audio endpoints, per-device volume and the mixer** use plain Core Audio
  (`IMMDeviceEnumerator`, `IAudioEndpointVolume`, `IAudioSessionManager2`,
  undocumented `IPolicyConfig` for default-device switching).
- **ANC state** is read passively from the dongle's vendor HID interface
  (`vid_1395&pid_0303&mi_03&col01`): the 107-byte input report (report ID 1) is pushed
  on every ANC key press; byte 17 is the on/off flag. The monitor opens the device in
  shared mode and only *reads* — it never writes to the headset.

## Known limitations

- ANC can be **read** but not set (the headset key is the only control; the value does
  not travel through the pipe protocol).
- Auto sleep / voice-prompt language / smart button are not supported on H3PRO
  (the device does not answer those requests).
- Firmware update is intentionally not implemented.

## License

[MIT](LICENSE)
