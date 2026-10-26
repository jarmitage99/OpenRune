# OpenRune

An open-source reimplementation of the Rune engine and RuneEd. It runs the
original game UnrealScript bytecode from the shipped `.u` packages on a
new C++ engine built on SDL2 and OpenGL 3.3. Based on OpenDE, the Deus Ex
reimplementation.

The original game data is required and is not included.

## Requirements

Debian/Ubuntu with:

    $ sudo apt install build-essential cmake pkg-config libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxss-dev libxkbcommon-dev libgl-dev libegl-dev libasound2-dev libpulse-dev libwayland-dev wayland-protocols libdecor-0-dev

Every library dependency is vendored in `vendor/` and built with the project.

## Game data

Copy the contents of the Rune game or demo into `game-data/`, so that these exist:

```
game-data/System
game-data/Maps
game-data/Meshes
game-data/Textures
game-data/Sounds
game-data/Music
```

You can also specify `-DRUNE_GAME_DATA=/path/to/rune`

At runtime the game and the editor look for the data in this order: the
`RUNE_GAME_DATA` environment variable, a `game-data` directory next to the
executable, the Steam install of Rune Classic (`steamapps/common/Rune Classic`
in the default Steam directories and their library folders), and finally the
path given at configure time.

## Building

    $ cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    $ cmake --build build -j"$(nproc)"

Windows builds are cross-compiled with MinGW:

    $ cmake -S . -B build-mingw -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-toolchain.cmake
    $ cmake --build build-mingw -j"$(nproc)"

Browser builds use Emscripten and WebGL 2. The game data given at configure
time is packaged with the page, without the Windows binaries:

    $ emcmake cmake -S . -B build-web -DCMAKE_BUILD_TYPE=Release -DSKIP_UT=ON -DRUNE_GAME_DATA="$PWD/demo-data"
    $ cmake --build build-web --target rune -j"$(nproc)"
    $ python3 -m http.server -d build-web/bin 8000

Then open `http://localhost:8000`. The game data is LZ4 compressed and cached
in the browser after the first download, together with settings and saves.

The build compiles `RuneExtra.u` for the Extra options page when game data is
available, and uses the bundled package otherwise. Releases include it in
`System/` beside the executables. After changing the scripts, refresh the
bundle with game data configured:

    $ cmake --build build --target update-rune-extra

Include the updated package and manifest with the script changes.

## Running

    $ build/bin/rune

Settings and saves live in `~/.local/share/rune` (`%APPDATA%\rune` on Windows):
`Rune.ini` and `User.ini` are read from there when present and otherwise from
the game's `System` directory, changed settings are written there, and saved
games are in `Save/`. The game data directory is never written to.

The video options menu selects windowed, borderless or fullscreen display
and switches between OpenGL and software rendering. Use `--renderer gl` or
`--renderer soft` to choose one for a single run. The Extra
options page has a Large Text setting for HUD messages and subtitles.

Useful options:

- `--renderer gl` uses the OpenGL renderer
- `--renderer soft` uses the software renderer
- `--width W --height H` sets the window size

## Editor

    $ build/bin/runeed RagnarVillage

The editor is a work in progress. Maps are saved to
`~/.local/share/rune/Maps`.

## Multiplayer

Multiplayer works between copies of this engine. It can't connect to original
Rune clients or servers.

To host a game and play on it:

    $ build/bin/rune "DM-Hel?game=RuneI.RuneMultiPlayer?listen"

To run a dedicated server without a window:

    $ build/bin/ucc server "DM-Hel?game=RuneI.RuneMultiPlayer?port=7777"

To join, give the server's address instead of a map, or use `open` in the
console:

    $ build/bin/rune 192.168.0.10:7777

The port defaults to 7777. The web admin is enabled with `bEnabled=True` in
`[UWeb.WebServer]` in `Rune.ini`. It serves `/ServerAdmin` on `ListenPort`,
with credentials from `[RuneServerAdmin.RuneServerAdmin]`.

## Tests

    $ build/bin/unit-tests

The tests need the game data. They write configuration only to a temporary
directory.
