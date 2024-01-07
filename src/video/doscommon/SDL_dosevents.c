/*
    SDL - Simple DirectMedia Layer
    Copyright (C) 1997-2012 Sam Lantinga

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

    Sam Lantinga
    slouken@libsdl.org
*/
#include "SDL_config.h"

/* Handle the event stream, converting DOS events into SDL events */

#include "../../events/SDL_sysevents.h"
#include "../../events/SDL_events_c.h"
#include "SDL_dosevents_c.h"

#include <dos.h>
#include <conio.h>

static const SDLKey keymap[0x7F] = {
/* 0x00 */      0,          SDLK_ESCAPE,    SDLK_1,           SDLK_2,            SDLK_3,      SDLK_4,       SDLK_5,         SDLK_6,
/* 0x08 */      SDLK_7,     SDLK_8,         SDLK_9,           SDLK_0,            SDLK_MINUS,  SDLK_EQUALS,  SDLK_BACKSPACE, SDLK_TAB,
/* 0x10 */      SDLK_q,     SDLK_w,         SDLK_e,           SDLK_r,            SDLK_t,      SDLK_y,       SDLK_u,         SDLK_i,
/* 0x18 */      SDLK_o,     SDLK_p,         SDLK_LEFTBRACKET, SDLK_RIGHTBRACKET, SDLK_RETURN, SDLK_LCTRL,   SDLK_a,         SDLK_s,
/* 0x20 */      SDLK_d,     SDLK_f,         SDLK_g,           SDLK_h,            SDLK_j,      SDLK_k,       SDLK_l,         SDLK_SEMICOLON,
/* 0x28 */      SDLK_QUOTE, SDLK_BACKQUOTE, SDLK_LSHIFT,      SDLK_BACKSLASH,    SDLK_z,      SDLK_x,       SDLK_c,         SDLK_v,
/* 0x30 */      SDLK_b,     SDLK_n,         SDLK_m,           SDLK_COMMA,        SDLK_PERIOD, SDLK_SLASH,   SDLK_RSHIFT,    0,
/* 0x38 */      SDLK_LALT,  SDLK_SPACE,     SDLK_CAPSLOCK,    SDLK_F1,           SDLK_F2,     SDLK_F3,      SDLK_F4,        SDLK_F5,
/* 0x40 */      SDLK_F6,    SDLK_F7,        SDLK_F8,          SDLK_F9,           SDLK_F10,    SDLK_NUMLOCK, SDLK_SCROLLOCK, SDLK_HOME,
/* 0x48 */      SDLK_UP,    SDLK_PAGEUP,    0,                SDLK_LEFT,         0,           SDLK_RIGHT,   0,              SDLK_END,
/* 0x50 */      SDLK_DOWN,  SDLK_PAGEDOWN,  SDLK_INSERT,      SDLK_DELETE,       0,           0,            SDLK_LESS,      SDLK_F11,
/* 0x58 */      SDLK_F12
};

static const Uint8 mousemap[] = {
    SDL_BUTTON_LEFT,
    SDL_BUTTON_RIGHT,
    SDL_BUTTON_MIDDLE
};

static DOS_EventData *eventdata = NULL;

static void INTERRUPT_ATTRIBUTES DOS_KeyboardInterrupt()
{
    Uint8 *keybuf = eventdata->keybuf;
    int write_pos = eventdata->key_write_pos;
    int scancode;

    scancode = inp(0x60);
    outp(0x20, 0x20);

    keybuf[write_pos++] = scancode;
    eventdata->key_write_pos = write_pos & KEYBUF_MASK;

    _chain_intr(eventdata->prev_kbd_int);
}

int DOS_InitEvents(DOS_EventData *this)
{
    union REGS regs;

    eventdata = this;
    SDL_memset(this, 0, sizeof(*this));

    regs.w.ax = 0;
    int386(0x33, &regs, &regs);
    this->mouse_on = regs.w.ax;
    this->num_buttons = SDL_min(regs.w.bx, SDL_arraysize(mousemap));

    this->prev_kbd_int = _dos_getvect(9);
    _dos_setvect(9, DOS_KeyboardInterrupt);

    return 0;
}

void DOS_QuitEvents(DOS_EventData *this)
{
    if (this->prev_kbd_int) {
        _dos_setvect(9, this->prev_kbd_int);
        this->prev_kbd_int = NULL;
    }

    eventdata = NULL;
}

void DOS_InitOSKeymap(DOS_EventData *this)
{
}

void DOS_PumpEvents(DOS_EventData *this)
{
    const int write_pos = this->key_write_pos;
    int read_pos = this->key_read_pos;
    SDL_keysym keysym;
    union REGS regs;
    int scancode;
    Uint8 state;
    int buttons;
    int changed;
    int i;

    while (read_pos != write_pos)
    {
        scancode = this->keybuf[read_pos++];
        read_pos &= KEYBUF_MASK;

        if (scancode == 0xE0) {
            this->special = SDL_TRUE;
            continue;
        } else if (scancode & 0x80) {
            state = SDL_RELEASED;
        } else {
            state = SDL_PRESSED;
        }

        /* TODO: Support special keys */
        if (this->special) {
            this->special = SDL_FALSE;
            continue;
        }

        keysym.scancode = scancode & 0x7F;
        keysym.sym = keymap[keysym.scancode];
        keysym.mod = KMOD_NONE;
        keysym.unicode = 0; /* TODO: Unicode support? */
        SDL_PrivateKeyboard(state, &keysym);
    }

    this->key_read_pos = read_pos;

    if (this->mouse_on) {
        regs.w.ax = 0x0B;
        int386(0x33, &regs, &regs);
        if (regs.w.cx || regs.w.dx)
            SDL_PrivateMouseMotion(0, 1, regs.w.cx, regs.w.dx);

        regs.w.ax = 0x03;
        int386(0x33, &regs, &regs);

        buttons = regs.w.bx;
        changed = buttons ^ this->prev_buttons;

        for(i = 0; i < this->num_buttons; i++) {
            if ((changed & (1 << i))) {
                SDL_PrivateMouseButton((buttons & (1 << i)) ? SDL_PRESSED : SDL_RELEASED, mousemap[i], 0, 0);
            }
        }
        this->prev_buttons = buttons;
    }
}
