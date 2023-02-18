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

#ifdef SDL_CDROM_RISCOS

/* Functions for system-level CD-ROM audio control */

#include <kernel.h>
#include <swis.h>

#include "SDL_cdrom.h"
#include "../SDL_syscdrom.h"

typedef struct {
	Uint32 device;
	Uint32 card;
	Uint32 logical_unit;
	Uint32 driver_handle;
	Uint32 reserved;
} cdfs_control_block;

/* The maximum number of CD-ROM drives we'll detect (Don't change!) */
#define MAX_DRIVES	26	

/* A list of available CD-ROM drives */
static char *SDL_cdlist[MAX_DRIVES];
static cdfs_control_block SDL_cdfs_block[MAX_DRIVES];

/* The system-dependent CD control functions */
static const char *SDL_SYS_CDName(int drive);
static int SDL_SYS_CDOpen(int drive);
static int SDL_SYS_CDGetTOC(SDL_CD *cdrom);
static CDstatus SDL_SYS_CDStatus(SDL_CD *cdrom, int *position);
static int SDL_SYS_CDPlay(SDL_CD *cdrom, int start, int length);
static int SDL_SYS_CDPause(SDL_CD *cdrom);
static int SDL_SYS_CDResume(SDL_CD *cdrom);
static int SDL_SYS_CDStop(SDL_CD *cdrom);
static int SDL_SYS_CDEject(SDL_CD *cdrom);
static void SDL_SYS_CDClose(SDL_CD *cdrom);


/* Add a CD-ROM drive to our list of valid drives */
static void AddDrive(char *drive)
{
	int i;

	if ( SDL_numcds < MAX_DRIVES ) {
		/* Add this drive to our list */
		i = SDL_numcds;
		SDL_cdlist[i] = SDL_strdup(drive);
		if ( SDL_cdlist[i] == NULL ) {
			SDL_OutOfMemory();
			return;
		}
		++SDL_numcds;
#ifdef CDROM_DEBUG
  fprintf(stderr, "Added CD-ROM drive: %s\n", drive);
#endif
	}
}

int  SDL_SYS_CDInit(void)
{
	/* checklist: Drive 'A' - 'Z' */
	int i;
	char drive[12];

	/* Fill in our driver capabilities */
	SDL_CDcaps.Name = SDL_SYS_CDName;
	SDL_CDcaps.Open = SDL_SYS_CDOpen;
	SDL_CDcaps.GetTOC = SDL_SYS_CDGetTOC;
	SDL_CDcaps.Status = SDL_SYS_CDStatus;
	SDL_CDcaps.Play = SDL_SYS_CDPlay;
	SDL_CDcaps.Pause = SDL_SYS_CDPause;
	SDL_CDcaps.Resume = SDL_SYS_CDResume;
	SDL_CDcaps.Stop = SDL_SYS_CDStop;
	SDL_CDcaps.Eject = SDL_SYS_CDEject;
	SDL_CDcaps.Close = SDL_SYS_CDClose;

	/* Scan the system for CD-ROM drives */
	/* TODO: Get the actual number of drives! */
	for ( i=0; i<=0; ++i ) {
		SDL_snprintf(drive, SDL_arraysize(drive), "CDFS::%d.$", i);
		AddDrive(drive);
	}
	SDL_memset(SDL_cdfs_block, 0, sizeof(SDL_cdfs_block));
	return(0);
}

static const char *SDL_SYS_CDName(int drive)
{
	return(SDL_cdlist[drive]);
}

static int SDL_SYS_CDOpen(int drive)
{
	_kernel_oserror *error;
	_kernel_swi_regs regs;
	Uint32 phys_drive;

	regs.r[0] = drive;
	error = _kernel_swi(CDFS_ConvertDriveToDevice, &regs, &regs);
	if (error)
		SDL_SetError("CDFS_ConvertDriveToDevice failed: %s (%i)", error->errmess, error->errnum);

	phys_drive = regs.r[1];
	SDL_cdfs_block[drive].device        = (phys_drive & 7);
	SDL_cdfs_block[drive].card          = (phys_drive & 3) >> 3;
	SDL_cdfs_block[drive].logical_unit  = (phys_drive & 7) >> 5;
	SDL_cdfs_block[drive].driver_handle = (phys_drive & 255) >> 8;
	SDL_cdfs_block[drive].reserved      = (phys_drive & 65535) >> 16;

	return(drive);
}

static int SDL_SYS_CDGetTOC(SDL_CD *cdrom)
{
	_kernel_oserror *error;
	_kernel_swi_regs regs;
	Uint8 first_track, last_track;
	Uint8 track_block[5];
	int i;

	regs.r[0] = 0;
	regs.r[1] = (int)&track_block;
	regs.r[7] = (int)&SDL_cdfs_block[cdrom->id];
	error = _kernel_swi(CD_EnquireTrack, &regs, &regs);

	if (error) {
		SDL_SetError("CD_PlayAudio failed: %s (%i)", error->errmess, error->errnum);
		return -1;
	}

	first_track = track_block[0];
	last_track = track_block[1];
	cdrom->numtracks = last_track-first_track+1;

	/* TODO: Implement this! */
#if 0
	for (i = first_track; i <= last_track; i++) {
		regs.r[0] = i;
		regs.r[1] = (int)&track_block;
		regs.r[7] = (int)&SDL_cdfs_block[cdrom->id];
		error = _kernel_swi(CD_EnquireTrack, &regs, &regs);

		unsigned long entry = toc.entry[i];
		cdrom->track[i].id = i+1;
		cdrom->track[i].type = (TOC_CTRL(toc.entry[i])==TRACK_CDDA)?SDL_AUDIO_TRACK:SDL_DATA_TRACK;
		cdrom->track[i].offset = TOC_LBA(entry)-150;
		cdrom->track[i].length = TOC_LBA((i+1<toc.last)?toc.entry[i+1]:toc.leadout_sector)-TOC_LBA(entry);
	}
#endif

	return 0;
}

/* Get CD-ROM status */
static CDstatus SDL_SYS_CDStatus(SDL_CD *cdrom, int *position)
{
	/* TODO: Implement this! */
	return CD_ERROR;
}

/* Start play */
static int SDL_SYS_CDPlay(SDL_CD *cdrom, int start, int length)
{
	_kernel_oserror *error;
	_kernel_swi_regs regs;
	int m, s, f;

	regs.r[0] = 1; /* Use Red Book addressing */
	FRAMES_TO_MSF(start, &m, &s, &f);
	regs.r[1] = (m << 16) | (s << 8) | (f);
	FRAMES_TO_MSF(start+length, &m, &s, &f);
	regs.r[2] = (m << 16) | (s << 8) | (f);
	regs.r[7] = (int)&SDL_cdfs_block[cdrom->id];
	error = _kernel_swi(CD_PlayAudio, &regs, &regs);

	if (error)
		SDL_SetError("CD_PlayAudio failed: %s (%i)", error->errmess, error->errnum);
	return error ? -1 : 0;
}

/* Pause play */
static int SDL_SYS_CDPause(SDL_CD *cdrom)
{
	_kernel_oserror *error;
	_kernel_swi_regs regs;

	regs.r[0] = 1;
	regs.r[7] = (int)&SDL_cdfs_block[cdrom->id];
	error = _kernel_swi(CD_AudioPause, &regs, &regs);

	if (error)
		SDL_SetError("CD_AudioPause failed: %s (%i)", error->errmess, error->errnum);
	return error ? -1 : 0;
}

/* Resume play */
static int SDL_SYS_CDResume(SDL_CD *cdrom)
{
	_kernel_oserror *error;
	_kernel_swi_regs regs;

	regs.r[0] = 0;
	regs.r[7] = (int)&SDL_cdfs_block[cdrom->id];
	error = _kernel_swi(CD_AudioPause, &regs, &regs);

	if (error)
		SDL_SetError("CD_AudioPause failed: %s (%i)", error->errmess, error->errnum);
	return error ? -1 : 0;
}

/* Stop play */
static int SDL_SYS_CDStop(SDL_CD *cdrom)
{
	_kernel_oserror *error;
	_kernel_swi_regs regs;

	regs.r[7] = (int)&SDL_cdfs_block[cdrom->id];
	error = _kernel_swi(CD_StopDisc, &regs, &regs);

	if (error)
		SDL_SetError("CD_StopDisc failed: %s (%i)", error->errmess, error->errnum);
	return error ? -1 : 0;
}

/* Eject the CD-ROM */
static int SDL_SYS_CDEject(SDL_CD *cdrom)
{
	_kernel_oserror *error;
	_kernel_swi_regs regs;

	regs.r[7] = (int)&SDL_cdfs_block[cdrom->id];
	error = _kernel_swi(CD_OpenDrawer, &regs, &regs);

	if (error)
		SDL_SetError("CD_OpenDrawer failed: %s (%i)", error->errmess, error->errnum);
	return error ? -1 : 0;
}

/* Close the CD-ROM handle */
static void SDL_SYS_CDClose(SDL_CD *cdrom)
{
}

void SDL_SYS_CDQuit(void)
{
	int i;

	if ( SDL_numcds > 0 ) {
		for ( i=0; i<SDL_numcds; ++i ) {
			SDL_free(SDL_cdlist[i]);
			SDL_cdlist[i] = NULL;
		}
		SDL_numcds = 0;
	}
}

#endif /* SDL_CDROM_RISCOS */
