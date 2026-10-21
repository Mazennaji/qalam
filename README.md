<div align="center">

<img src="assets/logo.png" alt="qalam logo" width="260">

# قلم · qalam

**The pen for your terminal.**

A fast, hackable modal text editor.
No dependencies. No bloat. Every key is yours to remap.

[![CI](https://github.com/YOUR_USERNAME/qalam/actions/workflows/ci.yml/badge.svg)](https://github.com/YOUR_USERNAME/qalam/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-linux%20%7C%20macos%20%7C%20wsl-lightgrey)](#install)
[![Status](https://img.shields.io/badge/status-usable%20alpha-yellowgreen)](#roadmap)
[![Made in Lebanon](https://img.shields.io/badge/made%20in-Lebanon%20🇱🇧-red)](#)

[Install](#install) · [Quick start](#quick-start) · [Keys](#keys) · [Commands](#commands) · [Configure](#configuration) · [Architecture](#architecture) · [Testing](#testing) · [Roadmap](#roadmap)

</div>

---

## Why qalam?

Qalam (قلم) is Arabic for *pen*. It keeps what makes modal editing great
(composable commands, hands on the home row, speed) and drops the legacy baggage.

- **Fast.** One write per frame, instant startup.
- **Small.** A codebase you can read in a weekend.
- **Hackable.** Key mappings, aliases, colors, and options live in one plain config file.
- **Self-contained.** No third-party libraries, just the C++ standard library and POSIX `termios`.
- **Unicode-aware.** UTF-8 input, wide characters, and combining marks line up correctly.
- **Careful with your files.** Atomic saves, preserved permissions and line endings, byte-exact round trips.

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
On Windows, use a WSL toolchain.

## Quick start

```bash
qalam notes.txt
qalam main.cpp util.cpp     # several files, one buffer each
```

| Mode        | Enter with              | Purpose                          |
|-------------|-------------------------|----------------------------------|
| **Normal**  | `Esc`                   | Navigate and run commands        |
| **Insert**  | `i` `a` `o` `I` `A` `O` | Type text                        |
| **Visual**  | `v` `V` `Ctrl-V`        | Select characters, lines, blocks |
| **Command** | `:`                     | Ex commands                      |
| **Search**  | `/` `?`                 | Regex search                     |

Quit with `:q`, or `:khalas` (خلص) if you feel like it.

## Keys

| Area | Keys |
|------|------|
| Move | `h j k l`, arrows, `0 ^ $`, `w b e`, `{ }`, `gg G` (`5G`), `f t F T` `; ,`, `%` |
| Edit | `i a I A o O`, `x X s S D C`, `r`, `~`, `J`, `.` repeat, `u` undo, `Ctrl-R` redo |
| Operators | `d c y > <` with any motion, counts, or text object (`dw`, `c$`, `>ip`, `d/foo<CR>`) |
| Text objects | `iw aw`, `i" a"`, `i( a(`, `i{ a{`, `i[ a[`, `i< a<`, `ip ap` |
| Registers | `"a` … `"z`, `"A` appends, `"0` last yank, `"1`–`"9` delete history, `"-` small delete, `"_` black hole, `"+` `"*` system clipboard, `p P` |
| Macros | `qa` … `q`, `@a`, `@@`, `3@a` |
| Marks and jumps | `ma`, `` `a ``, `'a`, ``` `` ```, `Ctrl-O`, `Tab` |
| Search | `/` `?` `n` `N` `*` `#` |
| Visual | `v V Ctrl-V`, `o`, `d y c p`, `I A` (block), `>` `<`, `:` |
| Windows | `Ctrl-W` + `s v h j k l w c o` |

Counts work almost everywhere: `3dd`, `d2w`, `5j`, `2@a`.

### System clipboard

`"+y` copies and `"+p` pastes. qalam uses `wl-copy`, `xclip`, `xsel`, `pbcopy`, or
`clip.exe` when one is installed, and falls back to the OSC 52 escape sequence on
terminals that support it. To make plain `y` use the clipboard, map it:

```
map normal <Space>y "+y
map visual <Space>y "+y
map normal <Space>p "+p
```

## Commands

| Command | Action |
|---------|--------|
| `:w` `:q` `:wq` `:wa` `:qa` `:wqa` | Save and quit (`!` forces) |
| `:e FILE` `:bn` `:bp` `:b N` `:ls` `:bd` | Buffers |
| `:sp` `:vs` `:close` `:only` | Windows |
| `:s/pat/rep/g` `:%s/…/…/` `:N,Ms/…/…/` | Substitute with regex, `&`, `\1`–`\9` |
| `:g/pat/cmd` `:v/pat/cmd` | Run a command on matching or non-matching lines |
| `:normal KEYS` | Run Normal-mode keys on each line of a range |
| `:d` `:y` `:m` `:t` `:>` `:<` | Line commands with ranges (`%`, `N,M`, `.`, `$`) |
| `:r FILE` | Insert a file below the current line |
| `:set …` | `number` `relativenumber` `wrap` `expandtab` `autoindent` `syntax` `ts=N` `sw=N` `ft=LANG` |
| `:map` `:unmap` | Runtime key mappings |
| `:reg` `:noh` `:syntax on/off` `:reload` | Registers, highlighting, config |

The command line supports history (`Up` / `Down`), cursor movement, `Ctrl-W`,
`Ctrl-U`, `Ctrl-R` for registers, and `Tab` completion for commands and paths.

Regex syntax: `.` `*` `+` `?` `^` `$` `( )` `|` `[a-z]` `\w \d \s` `\< \>`.
Smartcase applies.

## Syntax highlighting

| Language | Detected by |
|----------|-------------|
| C, C++ | `c h cc cpp cxx hh hpp hxx ipp inl tpp` |
| Python | `py pyw`, `python` shebang |
| Shell | `sh bash zsh`, shell shebangs |
| JavaScript, TypeScript | `js mjs cjs jsx ts tsx`, `node` / `deno` / `bun` shebangs |
| Rust | `rs` |
| Go | `go` |
| JSON | `json jsonc` |
| Lua | `lua`, `lua` shebang |
| Markdown | `md markdown` |

Detection uses the file extension first, then a `#!` line. Override it per buffer
with `:set ft=python`, turn it off with `:set ft=none`, and return to automatic
with `:set ft=`. Adding a language means adding one table entry in
`src/syntax/syntax.cpp`.

## Files

- **Atomic saves.** qalam writes a temporary file and renames it over the original, so a crash or full disk can't leave half a file.
- **Permissions and symlinks.** An executable script stays executable, and saving through a symlink updates the target.
- **Line endings.** A CRLF file loads without `\r`, shows `[dos]`, and saves back as CRLF. Mixed endings are left untouched.
- **Binary-safe.** NUL bytes, invalid UTF-8, and very long lines survive a round trip.
- **Final newline.** A missing one is added on save.

## Configuration

qalam reads `$QALAM_CONFIG`, else `$XDG_CONFIG_HOME/qalam/keymap.conf`,
else `~/.config/qalam/keymap.conf`. Run `:reload` to re-read it.

```
map normal <Space>w :w<CR>
map insert jk <Esc>
map visual <Space>y y
map normal s <Nop>
alias bye q
color keyword 203
set number
set tabstop=4
set shiftwidth=4
set expandtab
set autoindent
set timeoutlen 400
```

| Line | Meaning |
|------|---------|
| `map MODE KEYS KEYS` | Bind a key sequence. MODE is `normal`, `insert`, or `visual`. |
| `alias NAME COMMAND` | Make `:NAME` run an existing ex command |
| `color NAME CODE` | Syntax color (256-color code). NAME: `keyword type string comment number preproc` |
| `set OPTION` / `set noOPTION` | `number` `relativenumber` `wrap` `expandtab` `autoindent` `syntax` `history` |
| `set tabstop=N`, `set shiftwidth=N` | Tab width and shift width |
| `set timeoutlen MS` | How long a partial mapping waits (default 500) |

Key notation: `<Esc> <CR> <Tab> <BS> <Space> <Lt> <Gt> <Up> <Down> <Left>
<Right> <Home> <End> <PageUp> <PageDown> <Del> <C-x> <Nop>`.
Write spaces as `<Space>`. Mappings are not recursive. See `examples/keymap.conf`.

Command and search history is stored in `$XDG_STATE_HOME/qalam/history`
(default `~/.local/state/qalam/history`). Turn it off with `set nohistory`.

## Architecture

```
src/
├── terminal/    raw mode (RAII), UTF-8 key decoding, window size
├── text/        UTF-8 boundaries, display columns, tab stops
├── buffer/      TextBuffer interface, line-vector buffer, edits, file I/O
├── motions/     composable motions and text objects (they return ranges)
├── search/      regex engine, search, substitution
├── undo/        grouped edit history
├── syntax/      table-driven tokenizer for nine languages
├── input/       key notation and keymap
├── config/      config file parser
├── clipboard/   system clipboard via native tools or OSC 52
├── window/      split layout tree
├── render/      state → one frame string
├── editor/      modes, registers, macros, marks, ex commands
└── main.cpp
tests/           unit and scenario tests, no framework
```

Everything except `main.cpp` builds into a `qalam_core` static library that the
editor and the tests both link.

Design principles:

1. **Motions return ranges.** One operator works with every motion and text object.
2. **Every edit is reversible.** All changes flow through one `applyEdit`.
3. **Keys are data.** Mappings, aliases, and options are configuration, not code.
4. **Layers don't leak.** The buffer is testable without a terminal.

## Testing

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
./build/qalam_tests regex      # run only tests whose name contains "regex"
```

The tests cover the text, buffer, undo, regex, motion, syntax, config, layout,
rendering, and file I/O layers, plus `Editor` itself, driven by key strings such
as `"3dd"` or `":%s/a/b/g<CR>"` with no terminal. Pass `-DQALAM_BUILD_TESTS=OFF`
to skip them, or `-DQALAM_SANITIZE=ON` to build with AddressSanitizer and UBSan.

CI builds Debug and Release on Linux and macOS, and runs the suite once more
under the sanitizers.

## Roadmap

- [x] RAII raw-mode terminal, key decoder, frame renderer
- [x] Buffers, file load and save, scrolling
- [x] Normal, Insert, Visual (char, line, block), Command modes
- [x] Motions, operators, counts, text objects, dot repeat
- [x] Undo and redo
- [x] Regex search and substitution, `:g`, `:normal`
- [x] Registers (named, numbered, black hole), macros, marks, jump list
- [x] System clipboard
- [x] Config file, remappable keys, runtime `:map`
- [x] UTF-8 and wide characters
- [x] Syntax highlighting for nine languages
- [x] Multiple buffers and window splits
- [x] Line numbers, soft wrap, indentation options
- [x] Command-line editing, history, completion
- [x] Atomic saves, CRLF handling, binary-safe I/O
- [x] Unit and scenario tests, CI with sanitizers
- [ ] Key decoder tests (escape sequences, split UTF-8 reads)
- [ ] Gap buffer or rope backend with benchmarks
- [ ] Mouse support
- [ ] Signature feature: *(your hook goes here)*

## Contributing

Issues and PRs are welcome. Please run `clang-format` and the test suite before
submitting.

## License

MIT © Mazen Naji