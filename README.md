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
