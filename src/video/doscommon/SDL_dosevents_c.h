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

#ifndef _SDL_dosevents_h
#define _SDL_dosevents_h

#define INTERRUPT_ATTRIBUTES __interrupt __far

#define KEYBUF_SIZE 256
#define KEYBUF_MASK (KEYBUF_SIZE-1)

typedef struct {
    void (INTERRUPT_ATTRIBUTES *prev_kbd_int)();
    Uint8 keybuf[KEYBUF_SIZE];
    int key_write_pos;
    int key_read_pos;
    SDL_bool special;
} DOS_EventData;

extern int  DOS_InitEvents(DOS_EventData *this);
extern void DOS_InitOSKeymap(DOS_EventData *this);
extern void DOS_PumpEvents(DOS_EventData *this);
extern void DOS_QuitEvents(DOS_EventData *this);

#endif
