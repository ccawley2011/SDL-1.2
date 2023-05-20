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

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/* Not yet in the mingw32 cross-compile headers */
#ifndef CDS_FULLSCREEN
#define CDS_FULLSCREEN	4
#endif

#include "SDL_syswm.h"
#include "../SDL_sysvideo.h"
#include "../SDL_pixels_c.h"
#include "../../events/SDL_sysevents.h"
#include "../../events/SDL_events_c.h"
#include "../windib/SDL_gapidibvideo.h"
#include "../windib/SDL_dibvideo.h"
#include "../wincommon/SDL_syswm_c.h"
#include "../wincommon/SDL_sysmouse_c.h"
#include "../windib/SDL_dibevents_c.h"
#include "../wincommon/SDL_wingl_c.h"

/* TODO: Figure out why this crashes on Windows 3.1 */
#define NO_CHANGEDISPLAYSETTINGS
/* #define NO_GAMMA_SUPPORT */

/* WinG function pointers for video and events */
#define WINGAPI WINAPI
HDC     (WINGAPI *pWinGCreateDC)( void );
BOOL    (WINGAPI *pWinGRecommendDIBFormat)( BITMAPINFO FAR *pFormat );
HBITMAP (WINGAPI *pWinGCreateBitmap)( HDC WinGDC, BITMAPINFO const FAR *pHeader, void FAR *FAR *ppBits );
UINT    (WINGAPI *pWinGGetDIBColorTable)( HDC WinGDC, UINT StartIndex, UINT NumberOfEntries, RGBQUAD FAR *pColors );
BOOL    (WINGAPI *pWinGBitBlt)( HDC hdcDest, int nXOriginDest, int nYOriginDest, int nWidthDest, int nHeightDest, HDC hdcSrc, int nXOriginSrc, int nYOriginSrc );

/* Initialization/Query functions */
static int WING_VideoInit(_THIS, SDL_PixelFormat *vformat);
static SDL_Rect **WING_ListModes(_THIS, SDL_PixelFormat *format, Uint32 flags);
SDL_Surface *WING_SetVideoMode(_THIS, SDL_Surface *current, int width, int height, int bpp, Uint32 flags);
static int WING_SetColors(_THIS, int firstcolor, int ncolors,
			 SDL_Color *colors);
static void WING_CheckGamma(_THIS);
void WING_SwapGamma(_THIS);
void WING_QuitGamma(_THIS);
int WING_SetGammaRamp(_THIS, Uint16 *ramp);
int WING_GetGammaRamp(_THIS, Uint16 *ramp);
static void WING_VideoQuit(_THIS);

/* Hardware surface functions */
static int WING_AllocHWSurface(_THIS, SDL_Surface *surface);
static int WING_LockHWSurface(_THIS, SDL_Surface *surface);
static void WING_UnlockHWSurface(_THIS, SDL_Surface *surface);
static void WING_FreeHWSurface(_THIS, SDL_Surface *surface);

/* Windows message handling functions */
static void WING_GrabStaticColors(HWND window);
static void WING_ReleaseStaticColors(HWND window);
static void WING_Activate(_THIS, BOOL active, BOOL minimized);
static void WING_RealizePalette(_THIS);
static void WING_PaletteChanged(_THIS, HWND window);
static void WING_WinPAINT(_THIS, HDC hdc);

/* WinG driver bootstrap functions */

static int WING_Available(void)
{
	HINSTANCE WinGDLL;
	int wing_ok;

	/* Version check WING32.DLL (Is WinG okay?) */
	wing_ok = 0;
	WinGDLL = LoadLibrary(TEXT("WING32.DLL"));
	if ( WinGDLL != NULL ) {
		wing_ok = 1;

		/* Clean up.. */
		FreeLibrary(WinGDLL);
	}
	return(wing_ok);
}

/* Functions for loading the WinG functions dynamically */
static HINSTANCE WinGDLL = NULL;

static void WING_Unload(void)
{
	if ( WinGDLL != NULL ) {
		FreeLibrary(WinGDLL);
		pWinGCreateDC = NULL;
		pWinGRecommendDIBFormat = NULL;
		pWinGCreateBitmap = NULL;
		pWinGGetDIBColorTable = NULL;
		pWinGBitBlt = NULL;
		WinGDLL = NULL;
	}
}

static int WING_Load(void)
{
	int status;

	WING_Unload();
	WinGDLL = LoadLibrary(TEXT("WING32.DLL"));
	if ( WinGDLL != NULL ) {
		pWinGCreateDC = (void *)GetProcAddress(WinGDLL,
					TEXT("WinGCreateDC"));
		pWinGRecommendDIBFormat = (void *)GetProcAddress(WinGDLL,
					TEXT("WinGRecommendDIBFormat"));
		pWinGCreateBitmap = (void *)GetProcAddress(WinGDLL,
					TEXT("WinGCreateBitmap"));
		pWinGGetDIBColorTable = (void *)GetProcAddress(WinGDLL,
					TEXT("WinGGetDIBColorTable"));
		pWinGBitBlt = (void *)GetProcAddress(WinGDLL,
					TEXT("WinGBitBlt"));
	}
	if ( WinGDLL && pWinGCreateDC && pWinGRecommendDIBFormat &&
	     pWinGCreateBitmap && pWinGGetDIBColorTable && pWinGBitBlt ) {
		status = 0;
	} else {
		WING_Unload();
		status = -1;
	}
	return status;
}

static void WING_DeleteDevice(SDL_VideoDevice *device)
{
	if ( device ) {
		if ( device->hidden ) {
			if ( device->hidden->dibInfo ) {
				SDL_free( device->hidden->dibInfo );
			}
			SDL_free(device->hidden);
		}
		if ( device->gl_data ) {
			SDL_free(device->gl_data);
		}
		SDL_free(device);
	}
	WING_Unload();
}

static SDL_VideoDevice *WING_CreateDevice(int devindex)
{
	SDL_VideoDevice *device;

	/* Load WinG */
	if ( WING_Load() < 0 ) {
		return(NULL);
	}

	/* Initialize all variables that we clean on shutdown */
	device = (SDL_VideoDevice *)SDL_malloc(sizeof(SDL_VideoDevice));
	if ( device ) {
		SDL_memset(device, 0, (sizeof *device));
		device->hidden = (struct SDL_PrivateVideoData *)
				SDL_malloc((sizeof *device->hidden));
		if(device->hidden){
			SDL_memset(device->hidden, 0, (sizeof *device->hidden));
			device->hidden->dibInfo = (DibInfo *)SDL_malloc((sizeof(DibInfo)));
			if(device->hidden->dibInfo == NULL)
			{
				SDL_free(device->hidden);
				device->hidden = NULL;
			}
		}
		
		device->gl_data = (struct SDL_PrivateGLData *)
				SDL_malloc((sizeof *device->gl_data));
	}
	if ( (device == NULL) || (device->hidden == NULL) ||
		                 (device->gl_data == NULL) ) {
		SDL_OutOfMemory();
		WING_DeleteDevice(device);
		return(NULL);
	}
	SDL_memset(device->hidden->dibInfo, 0, (sizeof *device->hidden->dibInfo));
	SDL_memset(device->gl_data, 0, (sizeof *device->gl_data));

	/* Set the function pointers */
	device->VideoInit = WING_VideoInit;
	device->ListModes = WING_ListModes;
	device->SetVideoMode = WING_SetVideoMode;
	device->UpdateMouse = WIN_UpdateMouse;
	device->SetColors = WING_SetColors;
	device->UpdateRects = NULL;
	device->VideoQuit = WING_VideoQuit;
	device->AllocHWSurface = WING_AllocHWSurface;
	device->CheckHWBlit = NULL;
	device->FillHWRect = NULL;
	device->SetHWColorKey = NULL;
	device->SetHWAlpha = NULL;
	device->LockHWSurface = WING_LockHWSurface;
	device->UnlockHWSurface = WING_UnlockHWSurface;
	device->FlipHWSurface = NULL;
	device->FreeHWSurface = WING_FreeHWSurface;
	device->SetGammaRamp = WING_SetGammaRamp;
	device->GetGammaRamp = WING_GetGammaRamp;
#if SDL_VIDEO_OPENGL
	device->GL_LoadLibrary = WIN_GL_LoadLibrary;
	device->GL_GetProcAddress = WIN_GL_GetProcAddress;
	device->GL_GetAttribute = WIN_GL_GetAttribute;
	device->GL_MakeCurrent = WIN_GL_MakeCurrent;
	device->GL_SwapBuffers = WIN_GL_SwapBuffers;
#endif
	device->SetCaption = WIN_SetWMCaption;
	device->SetIcon = WIN_SetWMIcon;
	device->IconifyWindow = WIN_IconifyWindow;
	device->GrabInput = WIN_GrabInput;
	device->GetWMInfo = WIN_GetWMInfo;
	device->FreeWMCursor = WIN_FreeWMCursor;
	device->CreateWMCursor = WIN_CreateWMCursor;
	device->ShowWMCursor = WIN_ShowWMCursor;
	device->WarpWMCursor = WIN_WarpWMCursor;
	device->CheckMouseMode = WIN_CheckMouseMode;
	device->InitOSKeymap = DIB_InitOSKeymap;
	device->PumpEvents = DIB_PumpEvents;

	/* Set up the windows message handling functions */
	WIN_Activate = WING_Activate;
	WIN_RealizePalette = WING_RealizePalette;
	WIN_PaletteChanged = WING_PaletteChanged;
	WIN_WinPAINT = WING_WinPAINT;
	HandleMessage = DIB_HandleMessage;

	device->free = WING_DeleteDevice;

	/* We're finally ready */
	return device;
}

VideoBootStrap WING_bootstrap = {
	"wing", "Win32s WinG",
	WING_Available, WING_CreateDevice
};

static int cmpmodes(const void *va, const void *vb)
{
    SDL_Rect *a = *(SDL_Rect **)va;
    SDL_Rect *b = *(SDL_Rect **)vb;
    if ( a->w == b->w )
        return b->h - a->h;
    else
        return b->w - a->w;
}

static int WING_AddMode(_THIS, int bpp, int w, int h)
{
	SDL_Rect *mode;
	int i, index;
	int next_mode;

	/* Check to see if we already have this mode */
	if ( bpp < 8 || bpp > 32 ) {  /* Not supported */
		return(0);
	}
	index = ((bpp+7)/8)-1;
	for ( i=0; i<SDL_nummodes[index]; ++i ) {
		mode = SDL_modelist[index][i];
		if ( (mode->w == w) && (mode->h == h) ) {
			return(0);
		}
	}

	/* Set up the new video mode rectangle */
	mode = (SDL_Rect *)SDL_malloc(sizeof *mode);
	if ( mode == NULL ) {
		SDL_OutOfMemory();
		return(-1);
	}
	mode->x = 0;
	mode->y = 0;
	mode->w = w;
	mode->h = h;

	/* Allocate the new list of modes, and fill in the new mode */
	next_mode = SDL_nummodes[index];
	SDL_modelist[index] = (SDL_Rect **)
	       SDL_realloc(SDL_modelist[index], (1+next_mode+1)*sizeof(SDL_Rect *));
	if ( SDL_modelist[index] == NULL ) {
		SDL_OutOfMemory();
		SDL_nummodes[index] = 0;
		SDL_free(mode);
		return(-1);
	}
	SDL_modelist[index][next_mode] = mode;
	SDL_modelist[index][next_mode+1] = NULL;
	SDL_nummodes[index]++;

	return(0);
}

static void WING_CreatePalette(_THIS, int bpp)
{
/*	RJR: March 28, 2000
	moved palette creation here from "WING_VideoInit" */

	LOGPALETTE *palette;
	HDC hdc;
	int ncolors;

	ncolors = (1 << bpp);
	palette = (LOGPALETTE *)SDL_malloc(sizeof(*palette)+
				ncolors*sizeof(PALETTEENTRY));
	palette->palVersion = 0x300;
	palette->palNumEntries = ncolors;
	hdc = GetDC(SDL_Window);
	GetSystemPaletteEntries(hdc, 0, ncolors, palette->palPalEntry);
	ReleaseDC(SDL_Window, hdc);
	screen_pal = CreatePalette(palette);
	screen_logpal = palette;
}

int WING_VideoInit(_THIS, SDL_PixelFormat *vformat)
{
	const char *env = NULL;
	int binfo_size;
	BITMAPINFO *binfo;
#ifndef NO_CHANGEDISPLAYSETTINGS
	int i;
	DEVMODE settings;
#endif

	/* Create the window */
	if ( DIB_CreateWindow(this) < 0 ) {
		return(-1);
	}

#if SDL_AUDIO_DRIVER_DSOUND
	DX5_SoundFocus(SDL_Window);
#endif

	/* Determine the screen depth */
	vformat->BitsPerPixel = 8;
	binfo_size = sizeof(*binfo) + (3 * sizeof(DWORD));
	binfo = (BITMAPINFO *)SDL_malloc(binfo_size);
	if ( binfo ) {
		if ( pWinGRecommendDIBFormat(binfo) ) {
			vformat->BitsPerPixel = binfo->bmiHeader.biBitCount;

			switch (vformat->BitsPerPixel) {
			case 15:
				vformat->Rmask = 0x00007c00;
				vformat->Gmask = 0x000003e0;
				vformat->Bmask = 0x0000001f;
				vformat->BitsPerPixel = 16;
				break;
			case 16:
				vformat->Rmask = 0x0000f800;
				vformat->Gmask = 0x000007e0;
				vformat->Bmask = 0x0000001f;
				break;
			case 24:
			case 32:
				/* GDI defined as 8-8-8 */
				vformat->Rmask = 0x00ff0000;
				vformat->Gmask = 0x0000ff00;
				vformat->Bmask = 0x000000ff;
				break;
			default:
				break;
			}

			if (binfo->bmiHeader.biCompression == BI_BITFIELDS) {
				vformat->Rmask = ((Uint32*)binfo->bmiColors)[0];
				vformat->Gmask = ((Uint32*)binfo->bmiColors)[1];
				vformat->Bmask = ((Uint32*)binfo->bmiColors)[2];
			}
		}
		SDL_free(binfo);
	}

	/* See if gamma is supported on this screen */
	WING_CheckGamma(this);

#ifndef NO_CHANGEDISPLAYSETTINGS

	settings.dmSize = sizeof(DEVMODE);
	settings.dmDriverExtra = 0;
	/* Query for the desktop resolution */
	SDL_desktop_mode.dmSize = sizeof(SDL_desktop_mode);
	SDL_desktop_mode.dmDriverExtra = 0;
	EnumDisplaySettings(NULL, ENUM_CURRENT_SETTINGS, &SDL_desktop_mode);
	this->info.current_w = SDL_desktop_mode.dmPelsWidth;
	this->info.current_h = SDL_desktop_mode.dmPelsHeight;

	/* Query for the list of available video modes */
	for ( i=0; EnumDisplaySettings(NULL, i, &settings); ++i ) {
		WING_AddMode(this, settings.dmBitsPerPel,
			settings.dmPelsWidth, settings.dmPelsHeight);
	}
	/* Sort the mode lists */
	for ( i=0; i<NUM_MODELISTS; ++i ) {
		if ( SDL_nummodes[i] > 0 ) {
			SDL_qsort(SDL_modelist[i], SDL_nummodes[i], sizeof *SDL_modelist[i], cmpmodes);
		}
	}
#else
	// WinCE and fullscreen mode:
	// We use only vformat->BitsPerPixel that allow SDL to
	// emulate other bpp (8, 32) and use triple buffer, 
	// because SDL surface conversion is much faster than the WinCE one.
	// Although it should be tested on devices with graphics accelerator.

	WING_AddMode(this, vformat->BitsPerPixel,
			GetDeviceCaps(GetDC(NULL), HORZRES), 
			GetDeviceCaps(GetDC(NULL), VERTRES));

#endif /* !NO_CHANGEDISPLAYSETTINGS */

	/* Grab an identity palette if we are in a palettized mode */
	if ( vformat->BitsPerPixel <= 8 ) {
	/*	RJR: March 28, 2000
		moved palette creation to "WING_CreatePalette" */
		WING_CreatePalette(this, vformat->BitsPerPixel);
	}

	/* Fill in some window manager capabilities */
	this->info.wm_available = 1;

	/* Allow environment override of screensaver disable. */
	env = SDL_getenv("SDL_VIDEO_ALLOW_SCREENSAVER");
	if ( env ) {
		allow_screensaver = SDL_atoi(env);
	} else {
#ifdef SDL_VIDEO_DISABLE_SCREENSAVER
		allow_screensaver = 0;
#else
		allow_screensaver = 1;
#endif
	}

	/* We're done! */
	return(0);
}

/* We support any format at any dimension */
SDL_Rect **WING_ListModes(_THIS, SDL_PixelFormat *format, Uint32 flags)
{
	if ( (flags & SDL_FULLSCREEN) == SDL_FULLSCREEN ) {
		return(SDL_modelist[((format->BitsPerPixel+7)/8)-1]);
	} else {
		return((SDL_Rect **)-1);
	}
}


/* Various screen update functions available */
static void WING_NormalUpdate(_THIS, int numrects, SDL_Rect *rects);

static void WING_ResizeWindow(_THIS, int width, int height, int prev_width, int prev_height, Uint32 flags)
{
	RECT bounds;
	int x, y;

	/* Resize the window */
	if ( !SDL_windowid && !IsZoomed(SDL_Window) ) {
		HWND top;
		UINT swp_flags;
		const char *window = NULL;
		const char *center = NULL;

		if ( width != prev_width || height != prev_height ) {
			window = SDL_getenv("SDL_VIDEO_WINDOW_POS");
			center = SDL_getenv("SDL_VIDEO_CENTERED");
			if ( window ) {
				if ( SDL_sscanf(window, "%d,%d", &x, &y) == 2 ) {
					SDL_windowX = x;
					SDL_windowY = y;
				}
				if ( SDL_strcmp(window, "center") == 0 ) {
					center = window;
				}
			}
		}
		swp_flags = (SWP_NOCOPYBITS | SWP_SHOWWINDOW);

		bounds.left = SDL_windowX;
		bounds.top = SDL_windowY;
		bounds.right = SDL_windowX+width;
		bounds.bottom = SDL_windowY+height;
		AdjustWindowRectEx(&bounds, GetWindowLong(SDL_Window, GWL_STYLE), (GetMenu(SDL_Window) != NULL), 0);
		width = bounds.right-bounds.left;
		height = bounds.bottom-bounds.top;
		if ( (flags & SDL_FULLSCREEN) ) {
			x = (GetSystemMetrics(SM_CXSCREEN)-width)/2;
			y = (GetSystemMetrics(SM_CYSCREEN)-height)/2;
		} else if ( center ) {
			x = (GetSystemMetrics(SM_CXSCREEN)-width)/2;
			y = (GetSystemMetrics(SM_CYSCREEN)-height)/2;
		} else if ( SDL_windowX || SDL_windowY || window ) {
			x = bounds.left;
			y = bounds.top;
		} else {
			x = y = -1;
			swp_flags |= SWP_NOMOVE;
		}
		if ( flags & SDL_FULLSCREEN ) {
			top = HWND_TOPMOST;
		} else {
			top = HWND_NOTOPMOST;
		}
		SetWindowPos(SDL_Window, top, x, y, width, height, swp_flags);
		if ( !(flags & SDL_FULLSCREEN) ) {
			SDL_windowX = SDL_bounds.left;
			SDL_windowY = SDL_bounds.top;
		}
		if ( GetParent(SDL_Window) == NULL ) {
			SetForegroundWindow(SDL_Window);
		}
	}
}

SDL_Surface *WING_SetVideoMode(_THIS, SDL_Surface *current,
				int width, int height, int bpp, Uint32 flags)
{
	SDL_Surface *video;
	int prev_w, prev_h;
	Uint32 prev_flags;
	DWORD style;
	const DWORD directstyle =
			(WS_POPUP);
	const DWORD windowstyle = 
			(WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX);
	const DWORD resizestyle =
			(WS_THICKFRAME|WS_MAXIMIZEBOX);
	int binfo_size;
	BITMAPINFO *binfo;
	HDC hdc;
	Uint32 Rmask, Gmask, Bmask;

	prev_w = current->w;
	prev_h = current->h;
	prev_flags = current->flags;

	/*
	 * Special case for OpenGL windows...since the app needs to call
	 *  SDL_SetVideoMode() in response to resize events to continue to
	 *  function, but WGL handles the GL context details behind the scenes,
	 *  there's no sense in tearing the context down just to rebuild it
	 *  to what it already was...tearing it down sacrifices your GL state
	 *  and uploaded textures. So if we're requesting the same video mode
	 *  attributes just resize the window and return immediately.
	 */
	if ( SDL_Window &&
	     ((current->flags & ~SDL_ANYFORMAT) == (flags & ~SDL_ANYFORMAT)) &&
	     (current->format->BitsPerPixel == bpp) &&
	     (flags & SDL_OPENGL) && 
	     !(flags & SDL_FULLSCREEN) ) {  /* probably not safe for fs */
		current->w = width;
		current->h = height;
		SDL_resizing = 1;
		WING_ResizeWindow(this, width, height, prev_w, prev_h, flags);
		SDL_resizing = 0;
		return current;
	}

	/* Clean up any GL context that may be hanging around */
	if ( current->flags & SDL_OPENGL ) {
		WIN_GL_ShutDown(this);
	}
	SDL_resizing = 1;

	/*
	 * Always use the recommended bitmap format regardless of what's requested.
	 * On Windows 3.1 this is always 8bpp, even with true colour displays.
	 */
	binfo_size = sizeof(*binfo) + (3 * sizeof(DWORD));
	binfo = (BITMAPINFO *)SDL_malloc(binfo_size);
	if ( binfo ) {
		if ( pWinGRecommendDIBFormat(binfo) ) {
			bpp = binfo->bmiHeader.biBitCount;
		} else {
			bpp = 8;

			binfo->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
			binfo->bmiHeader.biPlanes = 1;
			binfo->bmiHeader.biXPelsPerMeter = 0;
			binfo->bmiHeader.biYPelsPerMeter = 0;
			binfo->bmiHeader.biClrUsed = 0;
			binfo->bmiHeader.biClrImportant = 0;
			binfo->bmiHeader.biBitCount = bpp;
			binfo->bmiHeader.biCompression = BI_RGB;
		}
	} else {
		SDL_OutOfMemory();
		return(NULL);
	}

	/* Recalculate the bitmasks if necessary */
	if ( bpp == current->format->BitsPerPixel ) {
		video = current;
	} else {
		switch (bpp) {
			case 15:
				/* 5-5-5 */
				Rmask = 0x00007c00;
				Gmask = 0x000003e0;
				Bmask = 0x0000001f;
				break;
			case 16:
				/* 5-6-5 */
				Rmask = 0x0000f800;
				Gmask = 0x000007e0;
				Bmask = 0x0000001f;
				break;
			case 24:
			case 32:
				/* GDI defined as 8-8-8 */
				Rmask = 0x00ff0000;
				Gmask = 0x0000ff00;
				Bmask = 0x000000ff;
				break;
			default:
				Rmask = 0x00000000;
				Gmask = 0x00000000;
				Bmask = 0x00000000;
				break;
		}
		video = SDL_CreateRGBSurface(SDL_SWSURFACE,
					0, 0, bpp, Rmask, Gmask, Bmask, 0);
		if ( video == NULL ) {
			SDL_free(binfo);
			SDL_OutOfMemory();
			return(NULL);
		}
	}

	/* Fill in part of the video surface */
	video->flags = 0;	/* Clear flags */
	video->w = width;
	video->h = height;
	video->pitch = SDL_CalculatePitch(video);
	if (!video->pitch) {
		SDL_free(binfo);
		return(NULL);
	}

	/* Small fix for WinCE/Win32 - when activating window
	   SDL_VideoSurface is equal to zero, so activating code
	   is not called properly for fullscreen windows because
	   macros WINGDI_FULLSCREEN uses SDL_VideoSurface
	*/
	SDL_VideoSurface = video;

#ifndef NO_CHANGEDISPLAYSETTINGS
	/* Set fullscreen mode if appropriate */
	if ( (flags & SDL_FULLSCREEN) == SDL_FULLSCREEN ) {
		DEVMODE settings;
		BOOL changed;

		SDL_memset(&settings, 0, sizeof(DEVMODE));
		settings.dmSize = sizeof(DEVMODE);
		settings.dmBitsPerPel = video->format->BitsPerPixel;
		settings.dmPelsWidth = width;
		settings.dmPelsHeight = height;
		settings.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL;
		if ( width <= (int)SDL_desktop_mode.dmPelsWidth &&
		     height <= (int)SDL_desktop_mode.dmPelsHeight ) {
			settings.dmDisplayFrequency = SDL_desktop_mode.dmDisplayFrequency;
			settings.dmFields |= DM_DISPLAYFREQUENCY;
		}
		changed = (ChangeDisplaySettings(&settings, CDS_FULLSCREEN) == DISP_CHANGE_SUCCESSFUL);
		if ( ! changed && (settings.dmFields & DM_DISPLAYFREQUENCY) ) {
			settings.dmFields &= ~DM_DISPLAYFREQUENCY;
			changed = (ChangeDisplaySettings(&settings, CDS_FULLSCREEN) == DISP_CHANGE_SUCCESSFUL);
		}
		if ( changed ) {
			video->flags |= SDL_FULLSCREEN;
			SDL_fullscreen_mode = settings;
		}

	}
#endif /* !NO_CHANGEDISPLAYSETTINGS */

	/* Reset the palette and create a new one if necessary */
	if ( grab_palette ) {
		WING_ReleaseStaticColors(SDL_Window);
		grab_palette = FALSE;
	}
	if ( screen_pal != NULL ) {
	/*	RJR: March 28, 2000
		delete identity palette if switching from a palettized mode */
		DeleteObject(screen_pal);
		screen_pal = NULL;
	}
	if ( screen_logpal != NULL ) {
		SDL_free(screen_logpal);
		screen_logpal = NULL;
	}

	if ( bpp <= 8 )
	{
	/*	RJR: March 28, 2000
		create identity palette switching to a palettized mode */
		WING_CreatePalette(this, bpp);
	}

	style = GetWindowLong(SDL_Window, GWL_STYLE);
	style &= ~(resizestyle|WS_MAXIMIZE);
	if ( (video->flags & SDL_FULLSCREEN) == SDL_FULLSCREEN ) {
		style &= ~windowstyle;
		style |= directstyle;
	} else {
#ifndef NO_CHANGEDISPLAYSETTINGS
		if ( (prev_flags & SDL_FULLSCREEN) == SDL_FULLSCREEN ) {
			ChangeDisplaySettings(NULL, CDS_FULLSCREEN);
		}
#endif
		if ( flags & SDL_NOFRAME ) {
			style &= ~windowstyle;
			style |= directstyle;
			video->flags |= SDL_NOFRAME;
		} else {
			style &= ~directstyle;
			style |= windowstyle;
			if ( flags & SDL_RESIZABLE ) {
				style |= resizestyle;
				video->flags |= SDL_RESIZABLE;
			}
		}
		if (IsZoomed(SDL_Window)) style |= WS_MAXIMIZE;
	}

	/* DJM: Don't piss of anyone who has setup his own window */
	if ( !SDL_windowid )
		SetWindowLong(SDL_Window, GWL_STYLE, style);

	/* Delete the old bitmap if necessary */
	if ( screen_bmp != NULL ) {
		DeleteObject(screen_bmp);
	}
	if ( ! (flags & SDL_OPENGL) ) {
		binfo->bmiHeader.biWidth = video->w;
		binfo->bmiHeader.biHeight = -video->h;	/* -ve for topdown bitmap */
		binfo->bmiHeader.biSizeImage = video->h * video->pitch;

		/* Create the offscreen bitmap buffer */
		hdc = GetDC(SDL_Window);
		screen_bmp = pWinGCreateBitmap(hdc, binfo, (void **)(&video->pixels));
		ReleaseDC(SDL_Window, hdc);
		if ( screen_bmp == NULL ) {
			if ( video != current ) {
				SDL_FreeSurface(video);
			}
			SDL_free(binfo);
			SDL_SetError("Couldn't create DIB section");
			return(NULL);
		}
		this->UpdateRects = WING_NormalUpdate;

		/* Set video surface flags */
		if ( screen_pal && (flags & (SDL_FULLSCREEN|SDL_HWPALETTE)) ) {
			grab_palette = TRUE;
		}
		if ( screen_pal ) {
			/* WinGBitBlt() maps colors for us */
			video->flags |= SDL_HWPALETTE;
		}
	}
	WING_ResizeWindow(this, width, height, prev_w, prev_h, flags);
	SDL_resizing = 0;
	SDL_free(binfo);

	/* Set up for OpenGL */
	if ( flags & SDL_OPENGL ) {
		if ( WIN_GL_SetupWindow(this) < 0 ) {
			return(NULL);
		}
		video->flags |= SDL_OPENGL;
	}

	/* JC 14 Mar 2006
		Flush the message loop or this can cause big problems later
		Especially if the user decides to use dialog boxes or assert()!
	*/
	WIN_FlushMessageQueue();

	/* We're live! */
	return(video);
}

/* We don't actually allow hardware surfaces in the WinG driver */
static int WING_AllocHWSurface(_THIS, SDL_Surface *surface)
{
	return(-1);
}
static void WING_FreeHWSurface(_THIS, SDL_Surface *surface)
{
	return;
}
static int WING_LockHWSurface(_THIS, SDL_Surface *surface)
{
	return(0);
}
static void WING_UnlockHWSurface(_THIS, SDL_Surface *surface)
{
	return;
}

static void WING_NormalUpdate(_THIS, int numrects, SDL_Rect *rects)
{
	HDC hdc, mdc;
	int i;

	hdc = GetDC(SDL_Window);
	if ( screen_pal ) {
		SelectPalette(hdc, screen_pal, FALSE);
	}
	mdc = pWinGCreateDC();
	SelectObject(mdc, screen_bmp);
	for ( i=0; i<numrects; ++i ) {
		pWinGBitBlt(hdc, rects[i].x, rects[i].y, rects[i].w, rects[i].h,
					mdc, rects[i].x, rects[i].y);
	}
	DeleteDC(mdc);
	ReleaseDC(SDL_Window, hdc);
}

static int FindPaletteIndex(LOGPALETTE *pal, BYTE r, BYTE g, BYTE b)
{
	PALETTEENTRY *entry;
	int i;
	int nentries = pal->palNumEntries;

	for ( i = 0; i < nentries; ++i ) {
		entry = &pal->palPalEntry[i];
		if ( entry->peRed == r && entry->peGreen == g && entry->peBlue == b ) {
			return i;
		}
	}
	return -1;
}

static BOOL CheckPaletteEntry(LOGPALETTE *pal, int index, BYTE r, BYTE g, BYTE b)
{
	PALETTEENTRY *entry;
	BOOL moved = 0;

	entry = &pal->palPalEntry[index];
	if ( entry->peRed != r || entry->peGreen != g || entry->peBlue != b ) {
		int found = FindPaletteIndex(pal, r, g, b);
		if ( found >= 0 ) {
			pal->palPalEntry[found] = *entry;
		}
		entry->peRed = r;
		entry->peGreen = g;
		entry->peBlue = b;
		moved = 1;
	}
	entry->peFlags = 0;

	return moved;
}

int WING_SetColors(_THIS, int firstcolor, int ncolors, SDL_Color *colors)
{
	HDC hdc, mdc;
	RGBQUAD *pal;
	int i;
	int moved_entries = 0;

	/* Update the display palette */
	hdc = GetDC(SDL_Window);
	if ( screen_pal ) {
		PALETTEENTRY *entry;

		for ( i=0; i<ncolors; ++i ) {
			entry = &screen_logpal->palPalEntry[firstcolor+i];
			entry->peRed   = colors[i].r;
			entry->peGreen = colors[i].g;
			entry->peBlue  = colors[i].b;
			entry->peFlags = PC_NOCOLLAPSE;
		}
		/* Check to make sure black and white are in position */
		if ( GetSystemPaletteUse(hdc) != SYSPAL_NOSTATIC256 ) {
			moved_entries += CheckPaletteEntry(screen_logpal, 0, 0x00, 0x00, 0x00);
			moved_entries += CheckPaletteEntry(screen_logpal, screen_logpal->palNumEntries-1, 0xff, 0xff, 0xff);
		}
		/* FIXME:
		   If we don't have full access to the palette, what we
		   really want to do is find the 236 most diverse colors
		   in the desired palette, set those entries (10-245) and
		   then map everything into the new system palette.
		 */

		/* Copy the entries into the system palette */
		UnrealizeObject(screen_pal);
		SetPaletteEntries(screen_pal, 0, screen_logpal->palNumEntries, screen_logpal->palPalEntry);
		SelectPalette(hdc, screen_pal, FALSE);
		RealizePalette(hdc);
	}

	/* Copy palette colors into DIB palette */
	pal = SDL_stack_alloc(RGBQUAD, ncolors);
	for ( i=0; i<ncolors; ++i ) {
		pal[i].rgbRed = colors[i].r;
		pal[i].rgbGreen = colors[i].g;
		pal[i].rgbBlue = colors[i].b;
		pal[i].rgbReserved = 0;
	}

	/* Set the DIB palette and update the display */
	mdc = pWinGCreateDC();
	SelectObject(mdc, screen_bmp);
	pWinGGetDIBColorTable(mdc, firstcolor, ncolors, pal);
	if ( moved_entries || !grab_palette ) {
		pWinGBitBlt(hdc, 0, 0, this->screen->w, this->screen->h,
		       mdc, 0, 0);
	}
	DeleteDC(mdc);
	SDL_stack_free(pal);
	ReleaseDC(SDL_Window, hdc);
	return(1);
}


static void WING_CheckGamma(_THIS)
{
#ifndef NO_GAMMA_SUPPORT
	HDC hdc;
	WORD ramp[3*256];

	/* If we fail to get gamma, disable gamma control */
	hdc = GetDC(SDL_Window);
	if ( ! GetDeviceGammaRamp(hdc, ramp) ) {
		this->GetGammaRamp = NULL;
		this->SetGammaRamp = NULL;
	}
	ReleaseDC(SDL_Window, hdc);
#endif /* !NO_GAMMA_SUPPORT */
}
void WING_SwapGamma(_THIS)
{
#ifndef NO_GAMMA_SUPPORT
	HDC hdc;

	if ( gamma_saved ) {
		hdc = GetDC(SDL_Window);
		if ( SDL_GetAppState() & SDL_APPINPUTFOCUS ) {
			/* About to leave active state, restore gamma */
			SetDeviceGammaRamp(hdc, gamma_saved);
		} else {
			/* About to enter active state, set game gamma */
			GetDeviceGammaRamp(hdc, gamma_saved);
			SetDeviceGammaRamp(hdc, this->gamma);
		}
		ReleaseDC(SDL_Window, hdc);
	}
#endif /* !NO_GAMMA_SUPPORT */
}
void WING_QuitGamma(_THIS)
{
#ifndef NO_GAMMA_SUPPORT
	if ( gamma_saved ) {
		/* Restore the original gamma if necessary */
		if ( SDL_GetAppState() & SDL_APPINPUTFOCUS ) {
			HDC hdc;

			hdc = GetDC(SDL_Window);
			SetDeviceGammaRamp(hdc, gamma_saved);
			ReleaseDC(SDL_Window, hdc);
		}

		/* Free the saved gamma memory */
		SDL_free(gamma_saved);
		gamma_saved = 0;
	}
#endif /* !NO_GAMMA_SUPPORT */
}

int WING_SetGammaRamp(_THIS, Uint16 *ramp)
{
#ifdef NO_GAMMA_SUPPORT
	SDL_SetError("SDL compiled without gamma ramp support");
	return -1;
#else
	HDC hdc;
	BOOL succeeded;

	/* Set the ramp for the display */
	if ( ! gamma_saved ) {
		gamma_saved = (WORD *)SDL_malloc(3*256*sizeof(*gamma_saved));
		if ( ! gamma_saved ) {
			SDL_OutOfMemory();
			return -1;
		}
		hdc = GetDC(SDL_Window);
		GetDeviceGammaRamp(hdc, gamma_saved);
		ReleaseDC(SDL_Window, hdc);
	}
	if ( SDL_GetAppState() & SDL_APPINPUTFOCUS ) {
		hdc = GetDC(SDL_Window);
		succeeded = SetDeviceGammaRamp(hdc, ramp);
		ReleaseDC(SDL_Window, hdc);
	} else {
		succeeded = TRUE;
	}
	return succeeded ? 0 : -1;
#endif /* !NO_GAMMA_SUPPORT */
}

int WING_GetGammaRamp(_THIS, Uint16 *ramp)
{
#ifdef NO_GAMMA_SUPPORT
	SDL_SetError("SDL compiled without gamma ramp support");
	return -1;
#else
	HDC hdc;
	BOOL succeeded;

	/* Get the ramp from the display */
	hdc = GetDC(SDL_Window);
	succeeded = GetDeviceGammaRamp(hdc, ramp);
	ReleaseDC(SDL_Window, hdc);
	return succeeded ? 0 : -1;
#endif /* !NO_GAMMA_SUPPORT */
}

void WING_VideoQuit(_THIS)
{
	int i, j;

	/* Destroy the window and everything associated with it */
	if ( SDL_Window ) {
		/* Delete the screen bitmap (also frees screen->pixels) */
		if ( this->screen ) {
			if ( grab_palette ) {
				WING_ReleaseStaticColors(SDL_Window);
			}
#ifndef NO_CHANGEDISPLAYSETTINGS
			if ( this->screen->flags & SDL_FULLSCREEN ) {
				ChangeDisplaySettings(NULL, CDS_FULLSCREEN);
				ShowWindow(SDL_Window, SW_HIDE);
			}
#endif
			if ( this->screen->flags & SDL_OPENGL ) {
				WIN_GL_ShutDown(this);
			}
			this->screen->pixels = NULL;
		}
		if ( screen_pal != NULL ) {
			DeleteObject(screen_pal);
			screen_pal = NULL;
		}
		if ( screen_logpal != NULL ) {
			SDL_free(screen_logpal);
			screen_logpal = NULL;
		}
		if ( screen_bmp ) {
			DeleteObject(screen_bmp);
			screen_bmp = NULL;
		}
		if ( screen_icn ) {
			DestroyIcon(screen_icn);
			screen_icn = NULL;
		}
		WING_QuitGamma(this);
		DIB_DestroyWindow(this);

		SDL_Window = NULL;
	}

	for ( i=0; i < SDL_arraysize(SDL_modelist); ++i ) {
		if ( !SDL_modelist[i] ) {
			continue;
		}
		for ( j=0; SDL_modelist[i][j]; ++j ) {
			SDL_free(SDL_modelist[i][j]);
		}
		SDL_free(SDL_modelist[i]);
		SDL_modelist[i] = NULL;
		SDL_nummodes[i] = 0;
	}
}

/* Exported for the windows message loop only */
static void WING_GrabStaticColors(HWND window)
{
	HDC hdc;

	hdc = GetDC(window);
	SetSystemPaletteUse(hdc, SYSPAL_NOSTATIC256);
	if ( GetSystemPaletteUse(hdc) != SYSPAL_NOSTATIC256 ) {
		SetSystemPaletteUse(hdc, SYSPAL_NOSTATIC);
	}
	ReleaseDC(window, hdc);
}
static void WING_ReleaseStaticColors(HWND window)
{
	HDC hdc;

	hdc = GetDC(window);
	SetSystemPaletteUse(hdc, SYSPAL_STATIC);
	ReleaseDC(window, hdc);
}
static void WING_Activate(_THIS, BOOL active, BOOL minimized)
{
	if ( grab_palette ) {
		if ( !active ) {
			WING_ReleaseStaticColors(SDL_Window);
			WING_RealizePalette(this);
		} else if ( !minimized ) {
			WING_GrabStaticColors(SDL_Window);
			WING_RealizePalette(this);
		}
	}
}
static void WING_RealizePalette(_THIS)
{
	if ( screen_pal != NULL ) {
		HDC hdc;

		hdc = GetDC(SDL_Window);
		UnrealizeObject(screen_pal);
		SelectPalette(hdc, screen_pal, FALSE);
		if ( RealizePalette(hdc) ) {
			InvalidateRect(SDL_Window, NULL, FALSE);
		}
		ReleaseDC(SDL_Window, hdc);
	}
}
static void WING_PaletteChanged(_THIS, HWND window)
{
	if ( window != SDL_Window ) {
		WING_RealizePalette(this);
	}
}

/* Exported for the windows message loop only */
static void WING_WinPAINT(_THIS, HDC hdc)
{
	HDC mdc;

	if ( screen_pal ) {
		SelectPalette(hdc, screen_pal, FALSE);
	}
	mdc = pWinGCreateDC();
	SelectObject(mdc, screen_bmp);
	pWinGBitBlt(hdc, 0, 0, SDL_VideoSurface->w, SDL_VideoSurface->h,
							mdc, 0, 0);
	DeleteDC(mdc);
}
