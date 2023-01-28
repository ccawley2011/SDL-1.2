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

#include "SDL_video.h"
#include "SDL_mouse.h"
#include "../SDL_sysvideo.h"
#include "../SDL_pixels_c.h"
#include "SDL_vgavideo.h"
#include "../doscommon/SDL_dosevents_c.h"

#include <dos.h>
#include <conio.h>

#define _THIS SDL_VideoDevice *this

static int VGA_VideoInit (_THIS, SDL_PixelFormat *vformat);
static SDL_Rect **VGA_ListModes (_THIS, SDL_PixelFormat *format, Uint32 flags);
static SDL_Surface *VGA_SetVideoMode (_THIS, SDL_Surface *current, int width, int height, int bpp, Uint32 flags);
static int VGA_SetColors (_THIS, int firstcolor, int ncolors, SDL_Color *colors);
static void VGA_UpdateRects (_THIS, int nrects, SDL_Rect *rects);
static void VGA_VideoQuit (_THIS);
static void VGA_InitOSKeymap(_THIS);
static void VGA_PumpEvents(_THIS);

static int VGA_Available() 
{
    return 1;
}

static void VGA_DeleteDevice(SDL_VideoDevice *device)
{
    SDL_free(device->hidden);
    SDL_free(device);
}

static SDL_VideoDevice *VGA_CreateDevice(int devindex)
{
    SDL_VideoDevice *this;
    
    this = (SDL_VideoDevice *)SDL_malloc(sizeof(SDL_VideoDevice));
    if (this) {
	SDL_memset(this, 0, sizeof *this);
	this->hidden = (struct SDL_PrivateVideoData *)SDL_malloc(sizeof(struct SDL_PrivateVideoData));
    }
    if (!this || !this->hidden) {
	SDL_OutOfMemory();
	if (this)
	    SDL_free(this);
	return 0;
    }
    SDL_memset(this->hidden, 0, sizeof(struct SDL_PrivateVideoData));

    /* Set the function pointers */
    this->VideoInit = VGA_VideoInit;
    this->ListModes = VGA_ListModes;
    this->SetVideoMode = VGA_SetVideoMode;
    this->SetColors = VGA_SetColors;
    this->UpdateRects = VGA_UpdateRects;
    this->VideoQuit = VGA_VideoQuit;
    this->InitOSKeymap = VGA_InitOSKeymap;
    this->PumpEvents = VGA_PumpEvents;

    this->free = VGA_DeleteDevice;

    return this;
}

VideoBootStrap VGA_bootstrap = {
    "vga", "VGA mode 13h driver",
    VGA_Available, VGA_CreateDevice
};

static int VGA_VideoInit(_THIS, SDL_PixelFormat *vformat)
{
    vformat->BitsPerPixel = 8;
    this->info.current_w = 320;
    this->info.current_h = 200;

    if (DOS_InitEvents(&this->hidden->eventdata)) {
        return -1;
    }

    return 0;
}

static SDL_Rect **VGA_ListModes(_THIS, SDL_PixelFormat *format, Uint32 flags)
{
    static SDL_Rect r = { 0, 0, 320, 200 };
    static SDL_Rect *rs[2] = { &r, 0 };
    return rs;
}

static void VGA_SetMode(Uint8 mode)
{
    union REGS regs;

    regs.h.ah = 0x00;
    regs.h.al = mode;
    int386(0x10, &regs, &regs);
}

static SDL_Surface *VGA_SetVideoMode(_THIS, SDL_Surface *current, int width, int height, int bpp,
				     Uint32 flags)
{
    /* Allocate the back buffer */
    if (this->hidden->buffer) SDL_free(this->hidden->buffer);
    this->hidden->buffer = SDL_calloc(1, width * height);
    if (!this->hidden->buffer) {
        SDL_OutOfMemory();
        return NULL;
    }

    /* Switch to 256 colour mode */
    VGA_SetMode(0x13);

    if (!SDL_ReallocFormat(current, 8, 0, 0, 0, 0)) {
	return NULL;
    }

    /* Allocate the new pixel format for the screen */
    current->flags = flags | SDL_FULLSCREEN;
    current->w = width;
    current->h = height;
    current->pitch = current->w;
    current->pixels = this->hidden->buffer;

    return current;
}

static int VGA_SetColors(_THIS, int firstcolor, int ncolors, SDL_Color *colors)
{
    int i;

    outp(0x03c8, firstcolor);
    for (i = 0; i < ncolors; i++) {
        outp(0x03c9, colors[i].r>>2);
        outp(0x03c9, colors[i].g>>2);
        outp(0x03c9, colors[i].b>>2);
    }

    return 1;
}

static void VGA_VideoQuit(_THIS)
{
    if (this->hidden->buffer) {
        SDL_free(this->hidden->buffer);
        this->hidden->buffer = NULL;
    }

    DOS_QuitEvents(&this->hidden->eventdata);

    /* Switch to 80x25 text mode */
    VGA_SetMode(0x03);
}

static void VGA_UpdateRects(_THIS, int numrects, SDL_Rect *rects) 
{
    SDL_Rect *rect;
    int i, j;
    Uint32 dst;
    const Uint8 *src;
    const Uint8 *pixels = (const Uint8 *)this->screen->pixels;
    int pitch = this->screen->pitch;

    /* Wait for the start of a vertical blanking interval. */
    while ( (inp(0x03DA) & 0x08));
    while (!(inp(0x03DA) & 0x08));

    for (i = 0; i < numrects; ++i) {
        rect = &rects[i];

        src = pixels  + (rect->y * pitch) + rect->x;
        dst = 0xA0000 + (rect->y * 320)   + rect->x;

        for (j = 0; j < rect->h; j++) {
            SDL_memcpy((void *)dst, src, rect->w);
            src += pitch;
            dst += 320;
        }
    }
}

static void VGA_InitOSKeymap(_THIS)
{
    DOS_InitOSKeymap(&this->hidden->eventdata);
}

static void VGA_PumpEvents(_THIS)
{
    DOS_PumpEvents(&this->hidden->eventdata);
}
