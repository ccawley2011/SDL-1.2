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

#ifdef SDL_JOYSTICK_RISCOS

/*
   RISC OS - Joystick support by Alan Buckley (alan_baa@hotmail.com) - 10 April 2003

   Note: Currently assumes joystick is present if joystick module is loaded
   and that there is one joystick with eight buttons.
*/

/* This is the system specific header for the SDL joystick API */

#include "SDL_events.h"
#include "SDL_joystick.h"
#include "../SDL_sysjoystick.h"
#include "../SDL_joystick_c.h"

#include "kernel.h"

#ifndef Joystick_Read
#define Joystick_Read 0x43F40
#endif

struct joystick_dev_info
{
	unsigned char id;
	unsigned char pad[3];
	const char *dev_path;
	const char *manufacturer;
	const char *product;
	const char *serial;
	int num_axes;
	int num_buttons;
};

struct joystick_hwdata 
{
	int id;
	int joystate;
};

#define MAX_JOYSTICKS 255
static char	*SYS_JoystickName[MAX_JOYSTICKS];

#define HAS_STRUCT_ENTRY(st, m, size) (size > (offsetof(st, m) + sizeof(((st *)0)->m)))

static char *CreateDefaultJoystickName(int id)
{
	char product[24];
	SDL_snprintf(product, SDL_arraysize(product), "RISC OS Joystick %d", id);
	return SDL_strdup(product);
}

/* Function to scan the system for joysticks.
 * This function should set SDL_numjoysticks to the number of available
 * joysticks.  Joystick 0 should be the system default joystick.
 * It should return number of joysticks, or -1 on an unrecoverable fatal error.
 */
int SDL_SYS_JoystickInit(void)
{
	_kernel_swi_regs regs;
	const struct joystick_dev_info *info;
	const Uint8 *data;
	int size, items, i;

	 /* Try to read joystick 0 */
	regs.r[0] = 0;
	if (_kernel_swi(Joystick_Read, &regs, &regs) != NULL)
	{
		/* Switch fails so it looks like there's no joystick here */
		return 0;
	}

	regs.r[0] = 0x2FF;
	if (_kernel_swi(Joystick_Read, &regs, &regs) != NULL)
	{
		/* The enumeration API is not available, so assume we only have one joystick. */
		SYS_JoystickName[0] = CreateDefaultJoystickName(0);
		return 1;
	}

	size = regs.r[0];
	items = regs.r[1];
	data = (const Uint8 *)regs.r[2];
	for (i = 0; i < items; i++) {
		info = (const struct joystick_dev_info *)data;

		/* TODO: Should IDs, axis counts and button counts be stored ahead of time? */
		if (HAS_STRUCT_ENTRY(struct joystick_dev_info, product, size) && info->product) {
			SYS_JoystickName[i] = SDL_strdup(info->product);
		} else {
			SYS_JoystickName[i] = CreateDefaultJoystickName(i);
		}
		data += size;
	}

	return items;
}

/* Function to get the device-dependent name of a joystick */
const char *SDL_SYS_JoystickName(int index)
{
	if ( SYS_JoystickName[index] != NULL ) {
		return(SYS_JoystickName[index]);
	} else {
		SDL_SetError("Unable to get name of joystick %d", index);
		return(NULL);
	}
}

/* Function to open a joystick for use.
   The joystick to open is specified by the index field of the joystick.
   This should fill the nbuttons and naxes fields of the joystick structure.
   It returns 0, or -1 if there is an error.
 */
int SDL_SYS_JoystickOpen(SDL_Joystick *joystick)
{
	const struct joystick_dev_info *info;
	_kernel_swi_regs regs;
	int size, items;

	if(!(joystick->hwdata=SDL_malloc(sizeof(struct joystick_hwdata)))) {
		SDL_OutOfMemory();
		return -1;
	}

	/* If we can't get the exact count of buttons, assume max of 8 for now. */
	joystick->nbuttons=8;
	joystick->nhats=0;
	joystick->nballs=0;
	joystick->naxes=2;
	joystick->hwdata->joystate=0;

	regs.r[0] = 0x200 | joystick->index;
	if (_kernel_swi(Joystick_Read, &regs, &regs) != NULL)
	{
		size = regs.r[0];
		items = regs.r[1];
		info = (const struct joystick_dev_info *)regs.r[2];

		if (!size || !items) {
			SDL_SetError("Invalid data returned from Joystick_Read");
			return -1;
		}

		joystick->hwdata->id = info->id;
		if (HAS_STRUCT_ENTRY(struct joystick_dev_info, num_buttons, size))
			joystick->nbuttons = info->num_buttons;
		if (HAS_STRUCT_ENTRY(struct joystick_dev_info, num_axes, size))
			joystick->naxes = info->num_axes;
	}
	else
	{
		joystick->hwdata->id = joystick->index;
	}

	return 0;

}

/* Function to update the state of a joystick - called as a device poll.
 * This function shouldn't update the joystick structure directly,
 * but instead should call SDL_PrivateJoystick*() to deliver events
 * and update joystick device state.
 */
void SDL_SYS_JoystickUpdate(SDL_Joystick *joystick)
{
	_kernel_swi_regs regs;
	regs.r[0] = joystick->hwdata->id;

	if (_kernel_swi(Joystick_Read, &regs, &regs) == NULL)
	{
		int newstate = regs.r[0];
		int oldstate = joystick->hwdata->joystate;
		if (newstate != oldstate)
		{
			if ((newstate & 0xFF) != (oldstate & 0xFF))
			{
				int y = regs.r[0] & 0xFF;
				/* Convert to signed values */
				if (y >= 128) y -= 256;
				SDL_PrivateJoystickAxis(joystick,1,-y * 256); /* Up and down opposite to result in SDL */
			}
			if ((newstate & 0xFF00) != (oldstate & 0xFF00))
			{
				int x = (regs.r[0] & 0xFF00) >> 8;
				if (x >= 128) x -= 256;
				SDL_PrivateJoystickAxis(joystick,0,x * 256);
			}

			if ((newstate & 0xFF0000) != (oldstate & 0xFF0000))
			{
				int buttons = (regs.r[0] & 0xFF0000) >> 16;
				int oldbuttons = (oldstate & 0xFF0000) >> 16;
				int i;
				for (i = 0; i < joystick->nbuttons; i++)
				{
					if ((buttons & (1<<i)) != (oldbuttons & (1<<i)))
					{
						if (buttons & (1<<i)) SDL_PrivateJoystickButton(joystick,i,SDL_PRESSED);
						else SDL_PrivateJoystickButton(joystick,i,SDL_RELEASED);
					}
				}
			}
			joystick->hwdata->joystate = newstate;
		}		
	}

	return;
}

/* Function to close a joystick after use */
void SDL_SYS_JoystickClose(SDL_Joystick *joystick)
{
	if (joystick->hwdata != NULL) {
		/* free system specific hardware data */
		SDL_free(joystick->hwdata);
		joystick->hwdata = NULL;
	}
}

/* Function to perform any system-specific joystick related cleanup */
void SDL_SYS_JoystickQuit(void)
{
	int i;
	for (i = 0; i < SDL_arraysize(SYS_JoystickName); i++) {
		if ( SYS_JoystickName[i] != NULL ) {
			SDL_free(SYS_JoystickName[i]);
			SYS_JoystickName[i] = NULL;
		}
	}
}

#endif /* SDL_JOYSTICK_RISCOS */
