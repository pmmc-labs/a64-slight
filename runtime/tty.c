// tty.c -- the terminal: raw mode, decoding keys, and the screen's size.
//
// The scheduler (process.c) reads stdin, terminal or not, and hands the
// bytes here a key at a time. A key becomes ts-slight's (key mods...): a
// string for a printable key (one UTF-8 character), or a name such as
// :ArrowUp (rt.h, RT_KEY_...), then the modifiers held, in the order :ctrl
// :alt :shift. As in readline, an upper-case letter comes with :shift, and
// ESC before a key means Alt.
//
// An escape sequence has to arrive whole, as terminals send them: an ESC
// at the end of what has been read is the Escape key.

#include "rt.h"

#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#define ESC 0x1b

static struct termios cooked;           // the terminal as it was
static int            raw;

static void cook(void) {
    if (raw) tcsetattr(STDIN_FILENO, TCSADRAIN, &cooked);
    raw = 0;
}

// Raw mode as Node's (libuv's): keys arrive as they're typed, unechoed,
// and Ctrl-C is a byte rather than a signal. Output is still processed,
// so \n still starts a new line.
void rt_tty_raw(int on) {
    static int registered;
    if (!on) {
        cook();
        return;
    }
    if (raw || !isatty(STDIN_FILENO) || tcgetattr(STDIN_FILENO, &cooked) != 0) return;
    struct termios t = cooked;
    t.c_iflag &= ~(tcflag_t)(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    t.c_cflag |= CS8;
    t.c_lflag &= ~(tcflag_t)(ECHO | ICANON | IEXTEN | ISIG);
    t.c_cc[VMIN]  = 1;
    t.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSADRAIN, &t) != 0) return;
    raw = 1;
    if (!registered) atexit(cook);
    registered = 1;
}

// --- keys ---------------------------------------------------------------------

typedef struct {                        // (key_t is POSIX's)
    int                  name;          // RT_KEY_..., unless there's text
    const unsigned char *text;          // a printable key's bytes...
    size_t               len;           // ...and how many; 0 for a name
    int                  ctrl, alt, shift;
} keypress_t;

static const unsigned char letters[] = "abcdefghijklmnopqrstuvwxyz";

// The rest of an escape sequence after ESC [ or ESC O: parameters (digits
// and semicolons), then a final byte. The second parameter, if any, is
// xterm's modifiers, plus 1: shift 1, alt 2, ctrl 4. Returns how many
// bytes it took.
static size_t sequence(const unsigned char *in, size_t n, keypress_t *k) {
    int    param[2] = { 0, 0 }, np = 0;
    size_t i        = 0;
    for (; i < n && ((in[i] >= '0' && in[i] <= '9') || in[i] == ';'); i++) {
        if (in[i] == ';') np++;
        else if (np < 2 && param[np] < 1000) param[np] = param[np] * 10 + (in[i] - '0');
    }
    if (i == n) return n;               // no final byte: unidentified
    unsigned char final = in[i++];
    if (final >= 'A' && final <= 'D')      k->name = RT_KEY_UP + (final - 'A');
    else if (final == 'H')                 k->name = RT_KEY_HOME;
    else if (final == 'F')                 k->name = RT_KEY_END;
    else if (final >= 'P' && final <= 'S') k->name = RT_KEY_F1 + (final - 'P');
    else if (final == 'Z')                 k->name = RT_KEY_TAB, k->shift = 1;
    else if (final == '~') {
        static const signed char tilde[25] = {
            [1] = RT_KEY_HOME,  [2] = RT_KEY_INSERT, [3] = RT_KEY_DELETE, [4] = RT_KEY_END,
            [5] = RT_KEY_PAGE_UP, [6] = RT_KEY_PAGE_DOWN, [7] = RT_KEY_HOME, [8] = RT_KEY_END,
            [11] = RT_KEY_F1, [12] = RT_KEY_F2, [13] = RT_KEY_F3, [14] = RT_KEY_F4, [15] = RT_KEY_F5,
            [17] = RT_KEY_F6, [18] = RT_KEY_F7, [19] = RT_KEY_F8, [20] = RT_KEY_F9, [21] = RT_KEY_F10,
            [23] = RT_KEY_F11, [24] = RT_KEY_F12,
        };
        if (param[0] < 25 && tilde[param[0]]) k->name = tilde[param[0]];
    }
    int mods = param[1] - 1;
    if (np >= 1 && mods > 0) {
        k->shift |= mods & 1;
        k->alt   |= (mods & 2) != 0;
        k->ctrl  |= (mods & 4) != 0;
    }
    return i;
}

// How many bytes the UTF-8 character at in takes, or 0 if it isn't one.
static size_t utf8(const unsigned char *in, size_t n) {
    size_t len = in[0] >= 0xf0 && in[0] <= 0xf4 ? 4 : in[0] >= 0xe0 ? 3 : in[0] >= 0xc2 ? 2 : 0;
    if (len > n) return 0;
    for (size_t i = 1; i < len; i++) {
        if ((in[i] & 0xc0) != 0x80) return 0;
    }
    return len;
}

// The key at in (n > 0 bytes); returns how many bytes it took.
static size_t decode(const unsigned char *in, size_t n, keypress_t *k) {
    unsigned char c = in[0];
    if (c == ESC) {
        if (n == 1) {
            k->name = RT_KEY_ESCAPE;
            return 1;
        }
        if ((in[1] == '[' || in[1] == 'O') && n > 2) return 2 + sequence(in + 2, n - 2, k);
        k->alt = 1;                     // ESC, then a key: Alt and that key
        return 1 + decode(in + 1, n - 1, k);
    }
    if (c == '\r' || c == '\n')      k->name = RT_KEY_ENTER;
    else if (c == '\t')              k->name = RT_KEY_TAB;
    else if (c == 0x7f || c == '\b') k->name = RT_KEY_BACKSPACE;
    else if (c >= 1 && c <= 26)      k->text = &letters[c - 1], k->len = 1, k->ctrl = 1;
    else if (c >= ' ' && c < 0x7f)   k->text = in, k->len = 1, k->shift = c >= 'A' && c <= 'Z';
    else if ((k->len = utf8(in, n))) k->text = in;
    return k->len ? k->len : 1;
}

static rt_value_t cons_at(rt_value_t *cell, rt_value_t car, rt_value_t cdr) {
    cell[0] = car;
    cell[1] = cdr;
    return (rt_value_t)cell | RT_TAG_LIST;
}

size_t rt_key(const unsigned char *in, size_t n, void (*emit)(rt_value_t key)) {
    keypress_t k    = { .name = RT_KEY_UNIDENTIFIED };
    size_t     used = decode(in, n, &k);
    if (k.ctrl && k.len == 1 && k.text[0] == 'c') exit(130);   // as ts-slight did
    // (key mods...), on the C stack: emit copies it
    _Alignas(16) uint64_t   text[2];    // a string box: the header, then up to 7 bytes and a NUL
    _Alignas(16) rt_value_t cells[8];
    rt_value_t key = rt_symbol(RT_SYM_KEYS + (uint64_t)k.name);
    if (k.len) {
        text[0] = k.len << RT_BOX_SIZE_SHIFT | RT_BOX_STRING;
        memcpy(&text[1], k.text, k.len);
        ((char *)&text[1])[k.len] = 0;
        key = (rt_value_t)text | RT_TAG_BOXED;
    }
    rt_value_t list = RT_NIL;
    if (k.shift) list = cons_at(cells + 6, rt_symbol(RT_SYM_KEYS + RT_KEY_SHIFT), list);
    if (k.alt)   list = cons_at(cells + 4, rt_symbol(RT_SYM_KEYS + RT_KEY_ALT), list);
    if (k.ctrl)  list = cons_at(cells + 2, rt_symbol(RT_SYM_KEYS + RT_KEY_CTRL), list);
    emit(cons_at(cells, key, list));
    return used;
}

// --- the screen ---------------------------------------------------------------

static rt_value_t screen(int rows) {
    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0 && w.ws_row && w.ws_col) return rt_int(rows ? w.ws_row : w.ws_col);
    return rt_int(rows ? 24 : 80);
}

rt_value_t rt_screen_rows(const char *site) {
    (void)site;
    return screen(1);
}

rt_value_t rt_screen_cols(const char *site) {
    (void)site;
    return screen(0);
}
