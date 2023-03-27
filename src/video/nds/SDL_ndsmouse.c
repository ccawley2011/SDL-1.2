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

#include "SDL_mouse.h"
#include "../../events/SDL_events_c.h"
#include "../SDL_cursor_c.h"
#include "SDL_ndsmouse_c.h"


typedef struct { u32 data[8]; } Tile;
typedef struct { Tile data[8][4]; } Sprite;

/* The implementation dependent data for the window manager cursor */
struct WMcursor {
	Sprite *data;
	SpriteSize size;
	int hot_x, hot_y;
};

void NDS_FreeWMCursor(_THIS, WMcursor *cursor)
{
	oamFreeGfx(&oamMain, cursor->data);
	SDL_free(cursor);
}

WMcursor *NDS_CreateWMCursor(_THIS, Uint8 *data, Uint8 *mask, int w, int h, int hot_x, int hot_y)
{
	WMcursor *cursor;
	void *cursor_data;
	Uint8 data_byte, mask_byte;
	Uint32 out_word = 0;
	Uint16 *ptr;
	int x, y, k;

	/* Allocate the cursor memory */
	cursor = (WMcursor *)SDL_malloc(sizeof(WMcursor));
	if (cursor == NULL) {
		SDL_OutOfMemory();
		return(NULL);
	}

	/* Mix the mask and the data */
	cursor_data = oamAllocateGfx(&oamMain, SpriteSize_32x64, SpriteColorFormat_16Color);
	if (cursor_data == NULL) {
		SDL_free(cursor);
		SDL_OutOfMemory();
		return(NULL);
	}

	cursor->size = SpriteSize_32x64;
	cursor->hot_x = hot_x;
	cursor->hot_y = hot_y;
	cursor->data = cursor_data;


	/**
	 * Data / Mask Resulting pixel on screen
	 *  0 / 1 White
	 *  1 / 1 Black
	 *  0 / 0 Transparent
	 *  1 / 0 Inverted color if possible, black if not.
	 */

	for (y = 0; y < h; y++) {
		int ytile = div32(y, 8);
		int ydata = mod32(y, 8);
		for (x = 0; x < w; x += 8) {
			int xtile = div32(x, 8);
			data_byte = *data;
			mask_byte = *mask;
			out_word = 0;
			for (k = 0; k < 8; k++)
			{
				out_word <<= 4;
				out_word |= ((data_byte & 1) << 1) | (mask_byte & 1);
				if ((k&3) == 3) ptr++;
				data_byte >>= 1;
				mask_byte >>= 1;
			}
			cursor->data->data[ytile][xtile].data[ydata] = out_word;
			data++;
			mask++;
		}
	}

	return(cursor);
}

static void NDS_UpdateCursor(_THIS, WMcursor *wmcursor, int x, int y)
{
	if (wmcursor == NULL) {
		oamSetHidden(&oamMain, 0, 1);
	} else {
		oamSet(&oamMain, 0, x - wmcursor->hot_x, y - wmcursor->hot_y, 0, 0, wmcursor->size,
		       SpriteColorFormat_16Color, wmcursor->data, -1, 0, 0, 0, 0, 0);
	}
	oamUpdate(&oamMain);
}

int NDS_ShowWMCursor(_THIS, WMcursor *wmcursor)
{
	/* Draw the cursor at the appropriate location */
	NDS_UpdateCursor(this, wmcursor, this->hidden->cursor_x, this->hidden->cursor_y);

	return(1);
}

void NDS_MoveWMCursor(_THIS, int x, int y)
{
	this->hidden->cursor_x = (x * 256) / this->hidden->w;
	this->hidden->cursor_y = (y * 192) / this->hidden->h;

	NDS_UpdateCursor(this, SDL_cursor->wm_cursor, this->hidden->cursor_x, this->hidden->cursor_y);
}
