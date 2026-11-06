# Quantum Terminal

Userspace terminal emulator for CryptixOS. It is built from the kernel's
`VirtualConsole` and `FramebufferConsole`, so it looks and behaves like the
kernel console.

## Layout

| Path                         | Contents                                                     |
|------------------------------|--------------------------------------------------------------|
| `Source/Terminal/`           | `VirtualConsole` (escape-sequence parser), `FramebufferConsole` (glyph renderer), builtin font |
| `Source/Graphics/`           | `Framebuffer` abstraction plus backends, PNG/ZLib decoder    |
| `Source/Platform/`           | `PseudoTerminal` (openpty + fork/exec of the shell)          |
| `Resources/background.png`   | Default background, installed to `$datadir/quantum-term`     |

## Backends

- **fbdev** maps `/dev/fbN` (`FBIOGET_*SCREENINFO` + `mmap`) and reads keyboard
  input from the controlling terminal in raw mode. This is the backend used on
  CryptixOS and it is always built.
- **sdl** emulates the framebuffer in an SDL2 window, for testing on the host.
  It is built when SDL2 is found (`-Dsdl=enabled|disabled|auto`).

If you don't pass `--backend`, the terminal uses `sdl` when `DISPLAY` or
`WAYLAND_DISPLAY` is set, and `fbdev` otherwise.

## Building and running on the host

```sh
meson setup build -Dbuild_tests=true
ninja -C build && meson test -C build
QUANTUM_BACKGROUND=Resources/background.png ./build/quantum-term
```

Options: `-b sdl|fbdev`, `-d /dev/fb0`, `-g image.png`, `--no-background`,
`-s <font scale>`, `--size 1280x800`, `-v`, `-e command args...`.

Alt+F1..F8 switches between 8 virtual consoles in the SDL backend. Each
console's shell starts the first time you switch to it.
`QUANTUM_SCREENSHOT=out.bmp` dumps the last frame on exit.

## Copy and paste

| Action                              | Effect                                          |
|-------------------------------------|-------------------------------------------------|
| Left drag                           | Select text, copied to the primary selection    |
| Double click / triple click         | Select a word / a whole line                    |
| Ctrl+Shift+C                        | Copy the selection to the clipboard             |
| Ctrl+Shift+V                        | Paste the clipboard                             |
| Shift+Insert, middle click          | Paste the primary selection                     |

The selection is cleared as soon as any selected cell changes. Pasted text is
wrapped in `ESC[200~ ... ESC[201~` when the application enables bracketed
paste mode (`ESC[?2004h`). Applications can set the clipboard with OSC 52
(`ESC]52;c;<base64>BEL`), reading the clipboard back through OSC 52 is not
supported on purpose. The fbdev backend has no system clipboard, so it keeps
the text in memory, shared between the virtual consoles.

## CI and packaging

- `.github/workflows/typos.yml`: spell checking with [typos](https://github.com/crate-ci/typos).
- `.github/workflows/build.yml`: gcc and clang, debug and release, plus an
  fbdev only build, on Arch Linux.
- `.github/workflows/tests.yml`: `meson test` with gcc and clang.
- `.github/workflows/aur.yml`: on a `v*` tag, builds `packaging/aur/PKGBUILD.in`
  with `makepkg` and pushes it to the [`quantum-term`](https://aur.archlinux.org/packages/quantum-term)
  AUR package. It needs the `AUR_SSH_PRIVATE_KEY`, `AUR_USERNAME` and
  `AUR_EMAIL` secrets, the tag has to match the version in `meson.build`.

Prism isn't packaged anywhere, so CI and the AUR package build it from the
revision pinned in `packaging/dependencies.env`
(`.github/scripts/install-prism.sh` installs it locally the same way).
