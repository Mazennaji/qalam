#include "editor/help.h"

namespace qalam {

const std::string& helpText() {
    static const std::string text = R"HELP(# qalam help

    Search this page with /word, or run :help TOPIC. Close it with :bd.

    ## Modes

    - Normal: Esc. Navigate and run commands.
    - Insert: i a I A o O. Type text.
    - Visual: v V Ctrl-V. Select characters, lines, blocks.
    - Command: `:` runs ex commands. Search: `/` and `?`.
    - Picker: :pick and :pickb. Fuzzy-find a file or buffer.

    ## Moving

    - h j k l, arrows, 0 ^ $, w b e, { }, gg G (5G), f t F T ; , and %
    - Ctrl-O jumps back, Tab jumps forward. Marks: ma, `a, 'a.

    ## Editing

    - x X s S D C r ~ J, and . repeats the last change.
    - Operators d c y > < take any motion, count, or text object.
    - Text objects: iw aw, i" a", i( a(, i{ a{, i[ a[, ip ap.
    - u undoes, Ctrl-R redoes. Undo survives quitting after :w.

    ## Registers and macros

    - "a to "z, "A appends, "0 last yank, "1 to "9 deletes, "_ black hole.
    - "+ and "* use the system clipboard.
    - qa records a macro, q stops, @a plays it, @@ repeats it.

    ## Search and replace

    - / ? n N * #, with regex and smartcase.
    - :s/pat/rep/g, :%s/pat/rep/, :g/pat/cmd, :v/pat/cmd, :normal KEYS.
    - Regex: . * + ? ^ $ ( ) | [a-z] \w \d \s \< \>

    ## Files, buffers, windows

    - :w :q :wq :wa :qa (add ! to force). Arabic: :خلص :احفظ :افتح
    - :e FILE, :bn, :bp, :b N, :ls, :bd
    - :sp, :vs, :close, :only, and Ctrl-W then s v h j k l w c o
    - :find QUERY, :fb QUERY, :pick, :pickb for fuzzy finding
    - :r FILE inserts a file below the cursor line.

    ## Options

    - :set number relativenumber wrap bidi mouse expandtab autoindent syntax
    - :set ts=4 sw=4 ft=python, and :set noOPTION turns an option off.
    - :set with no argument shows the current values.
    - Put permanent settings in ~/.config/qalam/keymap.conf and run :reload.

    ## Mapping keys

    - :map normal <Space>w :w<CR>
    - :unmap normal <Space>w
    - In the config file: map MODE KEYS KEYS, alias NAME COMMAND, color NAME CODE

    ## Mouse

    - Click places the cursor or focuses a split. Drag selects. The wheel scrolls.
    - :set nomouse gives drag-select back to your terminal.

    ## Right-to-left text

    - :set bidi draws lines in visual order. h and l then follow the screen.
    - Use it in terminals that do not reorder Arabic or Hebrew themselves.
    )HELP";
        return text;
    }
}