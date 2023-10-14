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

#include <nds.h>

#include "SDL.h"
#include "SDL_error.h"
#include "SDL_video.h"
#include "SDL_mouse.h"
#include "../SDL_sysvideo.h"
#include "../SDL_pixels_c.h"
#include "../../events/SDL_events_c.h"

#include "SDL_ndsvideo.h"
#include "SDL_ndsevents_c.h"
#include "SDL_ndsmouse_c.h"

#define NDSVID_DRIVER_NAME "nds"

/* Initialization/Query functions */
static int NDS_VideoInit(_THIS, SDL_PixelFormat *vformat);
static SDL_Rect **NDS_ListModes(_THIS, SDL_PixelFormat *format, Uint32 flags);
static SDL_Surface *NDS_SetVideoMode(_THIS, SDL_Surface *current, int width, int height, int bpp, Uint32 flags);
static int NDS_SetColors(_THIS, int firstcolor, int ncolors, SDL_Color *colors);
static void NDS_VideoQuit(_THIS);

/* Hardware surface functions */
static int NDS_AllocHWSurface(_THIS, SDL_Surface *surface);
static int NDS_LockHWSurface(_THIS, SDL_Surface *surface);
static int NDS_FlipHWSurface(_THIS, SDL_Surface *surface);
static void NDS_UnlockHWSurface(_THIS, SDL_Surface *surface);
static void NDS_FreeHWSurface(_THIS, SDL_Surface *surface);

/* etc. */
static void NDS_UpdateRects(_THIS, int numrects, SDL_Rect *rects);

/* NDS driver bootstrap functions */

static int NDS_Available(void)
{
	return 1;
}

static void NDS_DeleteDevice(SDL_VideoDevice *device)
{
	SDL_free(device->hidden);
	SDL_free(device);
}

static int HWAccelBlit(SDL_Surface *src, SDL_Rect *srcrect,
                        SDL_Surface *dst, SDL_Rect *dstrect)
{
	return 0;
}
 
static int CheckHWBlit(_THIS, SDL_Surface *src, SDL_Surface *dst)
{
	if (src->flags & SDL_SRCALPHA) return false;
	if (src->flags & SDL_SRCCOLORKEY) return false;
	if (src->flags & SDL_HWPALETTE) return false;
	if (dst->flags & SDL_SRCALPHA) return false;
	if (dst->flags & SDL_SRCCOLORKEY) return false;
	if (dst->flags & SDL_HWPALETTE) return false;

	if (src->format->BitsPerPixel != dst->format->BitsPerPixel) return false;
	if (src->format->BytesPerPixel != dst->format->BytesPerPixel) return false;

	src->map->hw_blit = HWAccelBlit;
	return true;
}

static SDL_VideoDevice *NDS_CreateDevice(int devindex)
{
	SDL_VideoDevice *device = 0;

	/* Initialize all variables that we clean on shutdown */
	device = (SDL_VideoDevice *)SDL_malloc(sizeof(SDL_VideoDevice));
	if (device) {
		SDL_memset(device, 0, (sizeof *device));
		device->hidden = (struct SDL_PrivateVideoData *)
				SDL_malloc((sizeof *device->hidden));
	}
	if ((device == NULL) || (device->hidden == NULL)) {
		SDL_OutOfMemory();
		if (device) {
			SDL_free(device);
		}
		return(0);
	} 
	SDL_memset(device->hidden, 0, (sizeof *device->hidden));

	/* Set the function pointers */
	device->VideoInit = NDS_VideoInit;
	device->ListModes = NDS_ListModes;
	device->SetVideoMode = NDS_SetVideoMode;
	device->CreateYUVOverlay = NULL;
	device->SetColors = NDS_SetColors;
	device->UpdateRects = NDS_UpdateRects;
	device->VideoQuit = NDS_VideoQuit;
	device->AllocHWSurface = NDS_AllocHWSurface;
	device->CheckHWBlit = CheckHWBlit;
	device->FillHWRect = NULL;
	device->SetHWColorKey = NULL;
	device->SetHWAlpha = NULL;
	device->LockHWSurface = NDS_LockHWSurface;
	device->UnlockHWSurface = NDS_UnlockHWSurface;
	device->FlipHWSurface = NDS_FlipHWSurface;
	device->FreeHWSurface = NDS_FreeHWSurface;
	device->SetCaption = NULL;
	device->SetIcon = NULL;
	device->IconifyWindow = NULL;
	device->GrabInput = NULL;
	device->GetWMInfo = NULL;
	device->FreeWMCursor = NDS_FreeWMCursor;
	device->CreateWMCursor = NDS_CreateWMCursor;
	device->ShowWMCursor = NDS_ShowWMCursor;
	device->MoveWMCursor = NDS_MoveWMCursor;
	device->InitOSKeymap = NDS_InitOSKeymap;
	device->PumpEvents = NDS_PumpEvents;
	device->info.blit_hw = SDL_TRUE;

	device->free = NDS_DeleteDevice;
	return device;
}

VideoBootStrap NDS_bootstrap = {
	NDSVID_DRIVER_NAME, "SDL NDS video driver",
	NDS_Available, NDS_CreateDevice
};

int NDS_VideoInit(_THIS, SDL_PixelFormat *vformat)
{
	/* Determine the screen depth (use default 8-bit depth) */
	/* we change this during the SDL_SetVideoMode implementation... */
	vformat->BitsPerPixel = 8;
	vformat->BytesPerPixel = 1;
	vformat->Rmask = vformat->Gmask = vformat->Bmask = 0;

	videoSetMode(MODE_5_2D | DISPLAY_BG2_ACTIVE);
	videoSetModeSub(MODE_0_2D | DISPLAY_BG0_ACTIVE);

	vramSetPrimaryBanks(VRAM_A_MAIN_BG, VRAM_B_MAIN_BG, VRAM_C_MAIN_BG, VRAM_D_MAIN_BG);
	vramSetBankE(VRAM_E_MAIN_SPRITE);
	vramSetBankH(VRAM_H_SUB_BG);
	vramSetBankI(VRAM_I_LCD);

	oamInit(&oamMain, SpriteMapping_1D_32, 0);

	/* Cursor palette */
	SPRITE_PALETTE[0] = RGB15(31, 31, 31);                     /* Transparent */
	SPRITE_PALETTE[1] = RGB15(31, 31, 31); /* White */
	SPRITE_PALETTE[2] = RGB15(0,  0,  0);  /* Inverted color if possible, black if not. */
	SPRITE_PALETTE[3] = RGB15(0,  0,  0);  /* Black */

	consoleInit(NULL, 0, BgType_Text4bpp, BgSize_T_256x256, 15, 0, false, true);

	/* We're done! */
	return 0;
}

SDL_Rect **NDS_ListModes(_THIS, SDL_PixelFormat *format, Uint32 flags)
{
	return (SDL_Rect **) -1;
}

static Uint16 NDS_GetBackgroundControl(int width, int height, int bpp, int *pitch) {
	if (width <= 128 && height <= 128) {
		*pitch = 128 * bpp;
		return (bpp > 1) ? BG_BMP16_128x128 : BG_BMP8_128x128;
	} else if (width <= 256 && height <= 256) {
		*pitch = 256 * bpp;
		return (bpp > 1) ? BG_BMP16_256x256 : BG_BMP8_256x256;
	} else if (width <= 512 && height <= 256) {
		*pitch = 512 * bpp;
		return (bpp > 1) ? BG_BMP16_512x256 : BG_BMP8_512x256;
	} else if (width <= 512 && height <= 512) {
		*pitch = 512 * bpp;
		return (bpp > 1) ? BG_BMP16_512x512 : BG_BMP8_512x512;
	} else if (width <= 1024 && height <= 512 && bpp == 1) {
		*pitch = 1024;
		return BG_BMP8_1024x512;
	} else if (width <= 512 && height <= 1024 && bpp == 1) {
		*pitch = 512;
		return BG_BMP8_512x1024;
	} else {
		return 0;
	}
}

SDL_Surface *NDS_SetVideoMode(_THIS, SDL_Surface *current,
				int width, int height, int bpp, Uint32 flags)
{
	Uint32 Rmask = 0, Gmask = 0, Bmask = 0, Amask = 0;

	/* Set up the new mode framebuffer */
	current->flags = flags | SDL_FULLSCREEN;
	this->hidden->w = current->w = width;
	this->hidden->h = current->h = height;

	if (bpp > 8) {
		bpp=16;
 		Rmask = 0x0000001F;
		Gmask = 0x000003E0;
		Bmask = 0x00007C00;
		Amask = 0x00008000;
	} else {
		bpp=8;
	}

	/* Allocate the new pixel format for the screen */
	if (!SDL_ReallocFormat(current, bpp, Rmask, Gmask, Bmask, Amask)) {
		SDL_SetError("Couldn't allocate new pixel format for requested mode");
		return (NULL);
	}

	/* Free the previous buffer if one has been allocated */
	if (this->hidden->buffer) {
		SDL_free(this->hidden->buffer);
		this->hidden->buffer = NULL;
	}

	this->hidden->current_bank = 0;
	this->hidden->bank[0] = (void *)BG_BMP_RAM(0);
	this->hidden->bgcnt[0] = NDS_GetBackgroundControl(current->w, current->h, bpp / 8, &this->hidden->bg_pitch);
	dmaFillHalfWords(0, this->hidden->bank[0], this->hidden->bg_pitch * current->h);

	if ((current->flags & SDL_DOUBLEBUF) && current->h <= 256) {
		this->hidden->bank[1] = (void *)BG_BMP_RAM(16);
		this->hidden->bgcnt[1] = this->hidden->bgcnt[0] | BG_MAP_BASE(16);
		dmaFillHalfWords(0, this->hidden->bank[1], this->hidden->bg_pitch * current->h);
	} else {
		current->flags &= ~(SDL_DOUBLEBUF);
	}

	REG_BG2PA = ((width / 256) << 8) | (width % 256) ;
	REG_BG2PB = 0;
	REG_BG2PC = 0;
	REG_BG2PD = ((height / 192) << 8) | ((height % 192) + (height % 192) / 3) ;
	REG_BG2X = 0;
	REG_BG2Y = 0;
	REG_BG2CNT = this->hidden->bgcnt[0];

	/* TODO: The DSi apparently supports 8-bit writes to VRAM. Investigate? */
	if ((current->flags & SDL_HWSURFACE) && bpp > 8) {
		if (current->flags & SDL_DOUBLEBUF) {
			REG_BG2CNT = this->hidden->bgcnt[1];
		}

		current->pixels = this->hidden->bank[0];
		current->pitch = this->hidden->bg_pitch;
	} else {
		current->pitch = SDL_CalculatePitch(current);
		current->pixels = this->hidden->buffer = SDL_calloc(height, current->pitch);
		if (!this->hidden->buffer) {
			SDL_SetError("Couldn't allocate buffer for requested mode");
			return (NULL);
		}
		current->flags &= ~(SDL_HWSURFACE | SDL_DOUBLEBUF);
		current->flags |= SDL_SWSURFACE;
	}

	if (flags & SDL_BOTTOMSCR) {
		lcdMainOnBottom();
	} else if (flags & SDL_TOPSCR) {
		lcdMainOnTop();
	}
	this->hidden->touchscreen = ((REG_POWERCNT & POWER_SWAP_LCDS) == 0);

	/* We're done */
	return current;
}

/* We don't actually allow hardware surfaces other than the main one */
static int NDS_AllocHWSurface(_THIS, SDL_Surface *surface)
{
	return(-1);
}

static void NDS_FreeHWSurface(_THIS, SDL_Surface *surface)
{
	return;
}

/* We need to wait for vertical retrace on page flipped displays */
static int NDS_LockHWSurface(_THIS, SDL_Surface *surface)
{
	return 0;
}

static void NDS_UnlockHWSurface(_THIS, SDL_Surface *surface)
{
	return;
}

static int NDS_FlipHWSurface(_THIS, SDL_Surface *surface)
{
	REG_BG2CNT = this->hidden->bgcnt[this->hidden->current_bank];
	this->hidden->current_bank ^= 1;
	surface->pixels = this->hidden->bank[this->hidden->current_bank];

	/* Wait for Vsync */
	while (REG_VCOUNT != 192);
	while (REG_VCOUNT == 192);

	return 0;
}

static void NDS_UpdateRects(_THIS, int numrects, SDL_Rect *rects)
{
	int j;
	unsigned char *to, *from;
	unsigned int pitch = this->screen->pitch;
	unsigned int row;
	unsigned int xmult = this->screen->format->BytesPerPixel;
	unsigned int rowsize;

	DC_FlushRange(this->screen->pixels, this->screen->pitch * this->screen->h);

	for (j = 0; j < numrects; j++, rects++)
	{
		/* Avoid crashes when attempting to DMA copy 0 bytes */
		if (rects->w == 0 || rects->h == 0)
			continue;

		from = (Uint8 *)this->screen->pixels + rects->x * xmult + rects->y * pitch;
		to  = (Uint8 *)this->hidden->bank[this->hidden->current_bank] + rects->x * xmult + rects->y * this->hidden->bg_pitch;
		rowsize = rects->w * xmult;

		/* Ensure that all DMA transfers are halfword aligned */
		if (((uintptr_t)to % 2) == 1) {
			from -= xmult;
			to -= xmult;
			rowsize += xmult;
		}
		if ((rowsize % 2) == 1)
			rowsize += 1;

		if (this->hidden->bg_pitch == pitch && rowsize == pitch) {
			dmaCopy(from, to, pitch * rects->h);
		} else {
			for (row = 0; row < rects->h; row++)
			{
				dmaCopy(from, to, rowsize);
				from += pitch;
				to += this->hidden->bg_pitch;
			}
		}
	}
}

int NDS_SetColors(_THIS, int firstcolor, int ncolors, SDL_Color *colors)
{
	int i, j = firstcolor + ncolors;
	for(i = firstcolor; i < j; i++)
	{
		short r = colors[i].r>>3;
		short g = colors[i].g>>3;
		short b = colors[i].b>>3;
		BG_PALETTE[i] = RGB15(r, g, b);
	} 

	return 0;
}

/* Note:  If we are terminated, this could be called in the middle of
   another SDL video routine -- notably UpdateRects.
*/
void NDS_VideoQuit(_THIS)
{
	if (this->hidden->buffer) {
		SDL_free(this->hidden->buffer);
		this->hidden->buffer = NULL;
	}
}
