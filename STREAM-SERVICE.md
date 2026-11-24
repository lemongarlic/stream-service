# stream-service

netcade's streaming host: a fork of [Apollo](https://github.com/ClassicOldSong/Apollo) (itself Sunshine's), kept to
what streaming needs and run headless. netcade controls it; Moonlight clients connect to it as to Sunshine or Apollo.

## What's kept

Apollo's streaming core, unchanged where it can be: capture (DXGI duplication, WGC), the encoders (NVENC, AMF,
QSV, software) with their zero-copy paths, the GameStream protocol (nvhttp pairing and serving, RTSP, the
control and media streams), audio, input, and Apollo's own virtual display (SudoVDA on Windows: a screen made per
client at its size, refresh rate and HDR, put back when it goes).

## What's gone

The web UI and its HTTPS server (confighttp: pages, login, password, cover uploads), the tray. Configuration,
pairing and everything else the web UI did go through the control channel.

## The control channel

One per host, local only: a named pipe on Windows (`\\.\pipe\stream-service`), a unix socket elsewhere
(`$XDG_RUNTIME_DIR/stream-service.sock`, else in the config dir). JSON lines, as netcade's controls-service speaks:

- a command: `{"id": 1, "method": "...", "args": {...}}` -> `{"id": 1, "ok": ...}` or `{"id": 1, "error": "..."}`
- an event, any time: `{"type": "...", ...}` with no id

Commands (first set): `status`, `pin {pin, name}`, `otp {passphrase}`, `clients`, `updateClient {uuid, ...}`,
`unpair {uuid}`, `unpairAll`, `disconnect {uuid}`, `config`, `setConfig {values}`, `screenMode {mode}`,
`resetDisplayPersistence`, `restart`, `quit`, `logs`.

Events: `clientConnected {uuid, name, width, height, fps, hdr}`, `clientDisconnected {uuid}`, `sessionStarted`,
`sessionEnded`, `pinRequested`, `screenModeChanged {mode}`.

## Screen mode

Solo (the virtual screen alone, the client's size), Dual (the virtual screen beside the main one), Mirror (the main
screen as it is). Taken live: changing it mid-stream moves the capture to the screen the new mode streams (the
capture re-picks its screen by the display device's id, not a name another layout gives to another screen).

## Platforms

Windows first. Everything Windows-only (SudoVDA, Windows' display configuration, the named pipe) stays behind
src/platform; on Linux and macOS the same channel is a unix socket, and what a platform can't do (a virtual screen
where there's no driver for one) is reported as unavailable, not a broken build.

## Building (Windows)

MSYS2 UCRT64 (see Apollo's docs for the packages), then `cmake -B build -G Ninja -S . -DCMAKE_BUILD_TYPE=Release`
and `ninja -C build`.
