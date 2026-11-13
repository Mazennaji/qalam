# Changelog

## Unreleased

- Add the in-editor manual with `:help` and `:help TOPIC`
- Add line sorting with `:sort[!] [n i u]`

## 0.1.0

First public release.

- Modal editing: Normal, Insert, Visual (character, line, block), Command, Search
- Operators with motions, counts, and text objects; dot repeat; undo and redo
- Regex search and substitution, `:g`, `:v`, `:normal`, ex line commands
- Registers (named, numbered, black hole, system clipboard), macros, marks, jump list
- Multiple buffers, window splits, mouse support, bracketed paste
- Syntax highlighting for C/C++, Python, shell, JS/TS, Rust, Go, JSON, Lua, Markdown
- Fuzzy finder (`:find`, `:fb`) and live picker (`:pick`, `:pickb`)
- Config file with mappings, aliases, colors, and options
- Right-to-left display (`set bidi`), visual `h`/`l`, Arabic command aliases
- Atomic saves, CRLF and binary-safe I/O, remembered cursor positions
- Chunked line store: single edits stay fast on 200,000-line files
- About 280 tests, sanitizer CI on Linux and macOS
