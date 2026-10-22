# SDL controller mapping database

`gamecontrollerdb.txt` is vendored from
[mdqinc/SDL_GameControllerDB](https://github.com/mdqinc/SDL_GameControllerDB)
(master, downloaded 2026-10-22). It contains the community-maintained SDL
game controller mappings for macOS, Linux, Windows, Android and iOS.

- License: zlib — see `LICENSE.SDL_GameControllerDB` in this directory.
- At runtime `allnes::gamepad::Gamepads` loads the file with
  `SDL_GameControllerAddMappingsFromFile()`.
- Override the bundled file with the `ALLNES_GAMECONTROLLERDB` environment
  variable, or drop an updated copy named `gamecontrollerdb.txt` next to the
  executable (or into the app's pref directory).
