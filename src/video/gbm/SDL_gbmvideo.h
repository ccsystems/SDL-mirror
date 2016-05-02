/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2014 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/

#ifndef __SDL_GBMVIDEO_H__
#define __SDL_GBMVIDEO_H__

#include "../../SDL_internal.h"
#include "../SDL_sysvideo.h"
#include "../SDL_egl_c.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include <xf86drm.h>
#include <xf86drmMode.h>
#include <gbm.h>

#include "GLES/gl.h"
#include "EGL/egl.h"
#include "EGL/eglext.h"

typedef struct SDL_VideoData
{
    uint32_t egl_refcount;      /* OpenGL ES reference count              */
} SDL_VideoData;

typedef struct SDL_DisplayData
{
	EGLDisplay display;
	struct gbm_device *gbm;
	struct gbm_surface *surface;
	int fd;
	drmModeModeInfo *mode;
	uint32_t crtc_id;
	uint32_t connector_id;
    struct gbm_bo *bo;
} SDL_DisplayData;

typedef struct SDL_WindowData
{
#if SDL_VIDEO_OPENGL_EGL  
    EGLSurface egl_surface;
#endif    
} SDL_WindowData;


/****************************************************************************/
/* SDL_VideoDevice functions declaration                                    */
/****************************************************************************/

/* Display and window functions */
int GBM_VideoInit(_THIS);
void GBM_VideoQuit(_THIS);
void GBM_GetDisplayModes(_THIS, SDL_VideoDisplay * display);
int GBM_SetDisplayMode(_THIS, SDL_VideoDisplay * display, SDL_DisplayMode * mode);
int GBM_CreateWindow(_THIS, SDL_Window * window);
int GBM_CreateWindowFrom(_THIS, SDL_Window * window, const void *data);
void GBM_SetWindowTitle(_THIS, SDL_Window * window);
void GBM_SetWindowIcon(_THIS, SDL_Window * window, SDL_Surface * icon);
void GBM_SetWindowPosition(_THIS, SDL_Window * window);
void GBM_SetWindowSize(_THIS, SDL_Window * window);
void GBM_ShowWindow(_THIS, SDL_Window * window);
void GBM_HideWindow(_THIS, SDL_Window * window);
void GBM_RaiseWindow(_THIS, SDL_Window * window);
void GBM_MaximizeWindow(_THIS, SDL_Window * window);
void GBM_MinimizeWindow(_THIS, SDL_Window * window);
void GBM_RestoreWindow(_THIS, SDL_Window * window);
void GBM_SetWindowGrab(_THIS, SDL_Window * window, SDL_bool grabbed);
void GBM_DestroyWindow(_THIS, SDL_Window * window);

/* Window manager function */
SDL_bool GBM_GetWindowWMInfo(_THIS, SDL_Window * window,
                             struct SDL_SysWMinfo *info);

/* OpenGL/OpenGL ES functions */
int GBM_GLES_LoadLibrary(_THIS, const char *path);
void *GBM_GLES_GetProcAddress(_THIS, const char *proc);
void GBM_GLES_UnloadLibrary(_THIS);
SDL_GLContext GBM_GLES_CreateContext(_THIS, SDL_Window * window);
int GBM_GLES_MakeCurrent(_THIS, SDL_Window * window, SDL_GLContext context);
int GBM_GLES_SetSwapInterval(_THIS, int interval);
int GBM_GLES_GetSwapInterval(_THIS);
void GBM_GLES_SwapWindow(_THIS, SDL_Window * window);
void GBM_GLES_DeleteContext(_THIS, SDL_GLContext context);

#define GBM_GLES_GetAttribute SDL_EGL_GetAttribute
#define GBM_GLES_GetProcAddress SDL_EGL_GetProcAddress
#define GBM_GLES_UnloadLibrary SDL_EGL_UnloadLibrary
#define GBM_GLES_SetSwapInterval SDL_EGL_SetSwapInterval
#define GBM_GLES_GetSwapInterval SDL_EGL_GetSwapInterval
#define GBM_GLES_DeleteContext SDL_EGL_DeleteContext

#endif /* __SDL_GBMVIDEO_H__ */

/* vi: set ts=4 sw=4 expandtab: */
