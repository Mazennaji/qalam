<div align="center">

<img src="assets/logo.png" alt="qalam logo" width="260">

# قلم · qalam

**The pen for your terminal.**

A fast, hackable modal text editor.
No dependencies. No bloat. Every key is yours to remap.

[![License: MIT](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-linux%20%7C%20macos-lightgrey)](#install)
[![Status](https://img.shields.io/badge/status-early%20development-orange)](#roadmap)
[![Made in Lebanon](https://img.shields.io/badge/made%20in-Lebanon%20🇱🇧-red)](#)

[Install](#install) · [Quick start](#quick-start) · [Keymap](#default-keymap) · [Configure](#configuration) · [Architecture](#architecture) · [Roadmap](#roadmap)

</div>

---

## Why qalam?

Qalam (قلم) is Arabic for *pen*. It keeps what makes modal editing great
(composable commands, hands on the home row, speed) and drops the legacy baggage.

- **Fast.** One write per frame, instant startup.
- **Tiny.** A small codebase you can read in an afternoon.
- **Hackable.** Every behavior is a named action. Keys are data, not code.
- **Self-contained.** No third-party libraries, just the standard library and POSIX `termios`.

## Install

```bash
git clone https://github.com/YOUR_USERNAME/qalam.git
cd qalam
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
sudo cmake --install build
```

Requirements: a C++20 compiler (GCC 11+ or Clang 14+), CMake 3.20+, and a POSIX
system (Linux, macOS, or WSL on Windows).

### Building with CLion

Open the project folder, let CMake load, then enable
**Run → Edit Configurations → Emulate terminal in output console**.
Raw mode needs a real TTY, which CLion's default Run window is not.

## Quick start

```bash
qalam notes.txt
```

| Mode        | Enter with   | Purpose                   |
|-------------|--------------|---------------------------|
| **Normal**  | `Esc`        | Navigate and run commands |
| **Insert**  | `i` `a` `o`  | Type text                 |
| **Command** | `:`          | Ex-style commands         |

Quit with `:q`, or `:khalas` (خلص) if you feel like it.

## Default keymap

| Keys              | Action                       |
|-------------------|------------------------------|
| `h j k l`         | Move                         |
| `w` `b`           | Word forward / back          |
| `0` `$`           | Line start / end             |
| `gg` `G`          | File start / end             |
| `dd` `yy` `p`     | Delete / yank / paste line   |
| `u` `Ctrl-R`      | Undo / redo                  |
| `/` `n` `N`       | Search                       |
| `:w` `:q` `:wq`   | Save / quit                  |

> The keymap above is the target. See the [Roadmap](#roadmap) for what exists today.

## Configuration

Keymaps are plain data. Rebind anything:

```ini
# ~/.config/qalam/keymap.conf
normal.w        = move_word_forward
normal.dd       = delete_line
normal.<Space>f = fuzzy_find_file
```

## Architecture

```
src/
├── terminal/    raw mode (RAII), key decoding, window size
├── buffer/      TextBuffer interface · line-vector · gap buffer
├── editor/      modes, cursor, viewport, registers
├── input/       keymap, pending-command parser, actions
├── motions/     composable ranges: w, b, $, gg, G, f<c>
├── undo/        edit log with grouped transactions
├── render/      state → single frame string
└── main.cpp
```

Design principles:

1. **Motions return ranges.** One operator works with every motion.
2. **Every edit is reversible.** All changes flow through one `apply(Edit)`.
3. **Keys are data.** Behavior lives in named actions, bindings in tables.
4. **Layers don't leak.** The buffer is testable without a terminal.

## Roadmap

- [x] Step 0: project skeleton, CMake build, RAII raw-mode terminal
- [ ] Key decoder, window size, frame renderer with status bar
- [ ] Buffer and file load/save
- [ ] Normal / Insert / Command modes
- [ ] Motions, operators, counts
- [ ] Undo / redo
- [ ] Search and `:s` substitution
- [ ] Visual mode
- [ ] Config file and remappable keys
- [ ] Syntax highlighting
- [ ] Gap buffer backend with benchmarks
- [ ] Signature feature: *(your hook goes here)*

## Contributing

Issues and PRs are welcome. Please run `clang-format` before submitting.

## License

MIT © Mazen Naji