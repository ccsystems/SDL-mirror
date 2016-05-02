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

#include "../../SDL_internal.h"

#if SDL_VIDEO_DRIVER_GBM

/* SDL internals */
#include "../SDL_sysvideo.h"
#include "SDL_version.h"
#include "SDL_syswm.h"
#include "SDL_loadso.h"
#include "SDL_events.h"
#include "../../events/SDL_mouse_c.h"
#include "../../events/SDL_keyboard_c.h"

#ifdef SDL_INPUT_LINUXEV
#include "../../core/linux/SDL_evdev.h"
#endif

/* GBM declarations */
#include "SDL_gbmvideo.h"
#include "SDL_gbmevents_c.h"

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

struct drm_fb {
    SDL_DisplayData *displaydata;
    struct gbm_bo *bo;
    uint32_t fb_id;
};

int
GBM_GLES_LoadLibrary(_THIS, const char *path) {
    SDL_DisplayData *displaydata = (SDL_DisplayData *) _this->displays[0].driverdata;
    return SDL_EGL_LoadLibrary(_this, path, (NativeDisplayType)displaydata->gbm);
}

static void
drm_fb_destroy_callback(struct gbm_bo *bo, void *data)
{
    struct drm_fb *fb = data;
    struct gbm_device *gbm = gbm_bo_get_device(bo);

    if (fb->fb_id)
        drmModeRmFB(fb->displaydata->fd, fb->fb_id);

    free(fb);
}

static struct drm_fb * drm_fb_get_from_bo(SDL_DisplayData *data, struct gbm_bo *bo)
{
    struct drm_fb *fb = gbm_bo_get_user_data(bo);
    uint32_t width, height, stride, handle;
    int ret;

    if (fb)
        return fb;

    fb = calloc(1, sizeof(*fb));
    fb->displaydata = data;
    fb->bo = bo;

    width = gbm_bo_get_width(bo);
    height = gbm_bo_get_height(bo);
    stride = gbm_bo_get_stride(bo);
    handle = gbm_bo_get_handle(bo).u32;

    ret = drmModeAddFB(data->fd, width, height, 24, 32, stride, handle, &fb->fb_id);
    if (ret) {
        printf("failed to create fb: %s\n", strerror(errno));
        free(fb);
        return NULL;
    }

    gbm_bo_set_user_data(bo, fb, drm_fb_destroy_callback);

    return fb;
}

static void page_flip_handler(int fd, unsigned int frame,
          unsigned int sec, unsigned int usec, void *data)
{
    int *waiting_for_flip = data;
    *waiting_for_flip = 0;
}

void
GBM_GLES_SwapWindow(_THIS, SDL_Window *window)
{
    static int first_flip = 1;
    fd_set fds;
    SDL_WindowData *wdata = ((SDL_WindowData *) window->driverdata);
    SDL_VideoDisplay *display = SDL_GetDisplayForWindow(window);
    SDL_DisplayData *displaydata = (SDL_DisplayData *) display->driverdata;
    drmEventContext evctx = {
        .version = DRM_EVENT_CONTEXT_VERSION,
        .page_flip_handler = page_flip_handler,
    };
    struct gbm_bo *bo;
    struct drm_fb *fb;
    int waiting_for_flip = 1, ret;

    FD_ZERO(&fds);
    FD_SET(0, &fds);
    FD_SET(displaydata->fd, &fds);

    SDL_EGL_SwapBuffers(_this, wdata->egl_surface);
    bo = gbm_surface_lock_front_buffer(displaydata->surface);
    fb = drm_fb_get_from_bo(displaydata, bo);

    if(first_flip)
    {
        displaydata->bo = NULL;
        ret = drmModeSetCrtc(displaydata->fd, displaydata->crtc_id, fb->fb_id, 0, 0,
            &displaydata->connector_id, 1, displaydata->mode);
        if (ret) {
            printf("failed to set mode: %s", strerror(errno));
        }

        first_flip = 0;
    }

    ret = drmModePageFlip(displaydata->fd, displaydata->crtc_id, fb->fb_id,
                DRM_MODE_PAGE_FLIP_EVENT, &waiting_for_flip);
    if (ret) {
        return;
    }

    while (waiting_for_flip) {
        ret = select(displaydata->fd + 1, &fds, NULL, NULL, NULL);
        if (ret < 0) {
            return;
        } else if (ret == 0) {
            return;
        } else if (FD_ISSET(0, &fds)) {
            return;
        }
        drmHandleEvent(displaydata->fd, &evctx);
    }

    if(displaydata->bo)
        gbm_surface_release_buffer(displaydata->surface, displaydata->bo);
    displaydata->bo = bo;   
}

SDL_EGL_CreateContext_impl(GBM)
SDL_EGL_MakeCurrent_impl(GBM)

static int init_drm(SDL_DisplayData *data)
{
	static const char *modules[] = {
		"i915", "radeon", "nouveau", "vmwgfx", "omapdrm", "exynos", "msm", "tegra"
	};
	drmModeRes *resources;
	drmModeConnector *connector = NULL;
	drmModeEncoder *encoder = NULL;
	int i, area;

	for (i = 0; i < ARRAY_SIZE(modules); i++) {
		printf("trying to load module %s...", modules[i]);
		data->fd = drmOpen(modules[i], NULL);
		if (data->fd < 0) {
			printf("failed.\n");
		} else {
			printf("success.\n");
			break;
		}
	}

	if (data->fd < 0) {
		printf("could not open drm device\n");
		return -1;
	}

	resources = drmModeGetResources(data->fd);
	if (!resources) {
		printf("drmModeGetResources failed: %s\n", strerror(errno));
		return -1;
	}

	/* find a connected connector: */
	for (i = 0; i < resources->count_connectors; i++) {
		connector = drmModeGetConnector(data->fd, resources->connectors[i]);
		if (connector->connection == DRM_MODE_CONNECTED) {
			/* it's connected, let's use this! */
			break;
		}
		drmModeFreeConnector(connector);
		connector = NULL;
	}

	if (!connector) {
		/* we could be fancy and listen for hotplug events and wait for
		 * a connector..
		 */
		printf("no connected connector!\n");
		return -1;
	}

	/* find highest resolution mode: */
	for (i = 0, area = 0; i < connector->count_modes; i++) {
		drmModeModeInfo *current_mode = &connector->modes[i];
		int current_area = current_mode->hdisplay * current_mode->vdisplay;
		if (current_area > area) {
			data->mode = current_mode;
			area = current_area;
		}
	}

	if (!data->mode) {
		printf("could not find mode!\n");
		return -1;
	}

	/* find encoder: */
	for (i = 0; i < resources->count_encoders; i++) {
		encoder = drmModeGetEncoder(data->fd, resources->encoders[i]);
		if (encoder->encoder_id == connector->encoder_id)
			break;
		drmModeFreeEncoder(encoder);
		encoder = NULL;
	}

	if (!encoder) {
		printf("no encoder!\n");
		return -1;
	}

	data->crtc_id = encoder->crtc_id;
	data->connector_id = connector->connector_id;

	return 0;
}

static int init_gbm(SDL_DisplayData *data)
{
	data->gbm = gbm_create_device(data->fd);
	data->surface = gbm_surface_create(data->gbm,
			data->mode->hdisplay, data->mode->vdisplay,
			GBM_FORMAT_XRGB8888,
			GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
	if (!data->surface) {
		printf("failed to create gbm surface\n");
		return -1;
	}

	return 0;
}

static int
GBM_Available(void)
{
    return 1;
}

static void
GBM_Destroy(SDL_VideoDevice * device)
{
    /*    SDL_VideoData *phdata = (SDL_VideoData *) device->driverdata; */
    if (device->driverdata != NULL) {
        device->driverdata = NULL;
    }
}

static SDL_VideoDevice *
GBM_Create()
{
    SDL_VideoDevice *device;
    SDL_VideoData *phdata;

    /* Initialize SDL_VideoDevice structure */
    device = (SDL_VideoDevice *) SDL_calloc(1, sizeof(SDL_VideoDevice));
    if (device == NULL) {
        SDL_OutOfMemory();
        return NULL;
    }

    /* Initialize internal data */
    phdata = (SDL_VideoData *) SDL_calloc(1, sizeof(SDL_VideoData));
    if (phdata == NULL) {
        SDL_OutOfMemory();
        SDL_free(device);
        return NULL;
    }

    device->driverdata = phdata;

    /* Setup amount of available displays and current display */
    device->num_displays = 0;

    /* Set device free function */
    device->free = GBM_Destroy;

    /* Setup all functions which we can handle */
    device->VideoInit = GBM_VideoInit;
    device->VideoQuit = GBM_VideoQuit;
    device->GetDisplayModes = GBM_GetDisplayModes;
    device->SetDisplayMode = GBM_SetDisplayMode;
    device->CreateWindow = GBM_CreateWindow;
    device->CreateWindowFrom = GBM_CreateWindowFrom;
    device->SetWindowTitle = GBM_SetWindowTitle;
    device->SetWindowIcon = GBM_SetWindowIcon;
    device->SetWindowPosition = GBM_SetWindowPosition;
    device->SetWindowSize = GBM_SetWindowSize;
    device->ShowWindow = GBM_ShowWindow;
    device->HideWindow = GBM_HideWindow;
    device->RaiseWindow = GBM_RaiseWindow;
    device->MaximizeWindow = GBM_MaximizeWindow;
    device->MinimizeWindow = GBM_MinimizeWindow;
    device->RestoreWindow = GBM_RestoreWindow;
    device->SetWindowGrab = GBM_SetWindowGrab;
    device->DestroyWindow = GBM_DestroyWindow;
    device->GetWindowWMInfo = GBM_GetWindowWMInfo;
    device->GL_LoadLibrary = GBM_GLES_LoadLibrary;
    device->GL_GetProcAddress = GBM_GLES_GetProcAddress;
    device->GL_UnloadLibrary = GBM_GLES_UnloadLibrary;
    device->GL_CreateContext = GBM_GLES_CreateContext;
    device->GL_MakeCurrent = GBM_GLES_MakeCurrent;
    device->GL_SetSwapInterval = GBM_GLES_SetSwapInterval;
    device->GL_GetSwapInterval = GBM_GLES_GetSwapInterval;
    device->GL_SwapWindow = GBM_GLES_SwapWindow;
    device->GL_DeleteContext = GBM_GLES_DeleteContext;

    device->PumpEvents = GBM_PumpEvents;

    return device;
}

VideoBootStrap GBM_bootstrap = {
    "GBM",
    "GBM Video Driver",
    GBM_Available,
    GBM_Create
};

/*****************************************************************************/
/* SDL Video and Display initialization/handling functions                   */
/*****************************************************************************/
int
GBM_VideoInit(_THIS)
{
    SDL_VideoDisplay display;
    SDL_DisplayMode current_mode;
    SDL_DisplayData *data;

    /* Allocate display internal data */
    data = (SDL_DisplayData *) SDL_calloc(1, sizeof(SDL_DisplayData));
    if (data == NULL) {
        return SDL_OutOfMemory();
    }

    if(init_drm(data)) {
        printf("failed to initialize DRM\n");
        return -1;
    }

    if(init_gbm(data)) {
        printf("failed to initialize GBM\n");
        return -1;
    }
    
    SDL_zero(current_mode);
    current_mode.w = data->mode->hdisplay;
    current_mode.h = data->mode->vdisplay;
    current_mode.refresh_rate = 60;
    current_mode.format = SDL_PIXELFORMAT_ABGR8888;
    current_mode.driverdata = NULL;

    SDL_zero(display);
    display.desktop_mode = current_mode;
    display.current_mode = current_mode;

    display.driverdata = data;

    SDL_AddVideoDisplay(&display);

#ifdef SDL_INPUT_LINUXEV    
    SDL_EVDEV_Init();
#endif    
    
    return 1;
}

void
GBM_VideoQuit(_THIS)
{
#ifdef SDL_INPUT_LINUXEV    
    SDL_EVDEV_Quit();
#endif    
}

void
GBM_GetDisplayModes(_THIS, SDL_VideoDisplay * display)
{
    /* Only one display mode available, the current one */
    SDL_AddDisplayMode(display, &display->current_mode);
}

int
GBM_SetDisplayMode(_THIS, SDL_VideoDisplay * display, SDL_DisplayMode * mode)
{
    return 0;
}

int
GBM_CreateWindow(_THIS, SDL_Window * window)
{
    SDL_WindowData *wdata;
    SDL_VideoDisplay *display;
    SDL_DisplayData *displaydata;

    /* Allocate window internal data */
    wdata = (SDL_WindowData *) SDL_calloc(1, sizeof(SDL_WindowData));
    if (wdata == NULL) {
        return SDL_OutOfMemory();
    }
    display = SDL_GetDisplayForWindow(window);
    displaydata = (SDL_DisplayData *) display->driverdata;

    /* Windows have one size for now */
    window->w = display->desktop_mode.w;
    window->h = display->desktop_mode.h;

    /* OpenGL is the law here, buddy */
    window->flags |= SDL_WINDOW_OPENGL;

    if (!_this->egl_data) {
        if (SDL_GL_LoadLibrary(NULL) < 0) {
            return -1;
        }
    }

    wdata->egl_surface = SDL_EGL_CreateSurface(_this, (NativeWindowType)displaydata->surface);
    if (wdata->egl_surface == EGL_NO_SURFACE) {
        SDL_SetError("Could not create EGL surface");
        return SDL_FALSE;
    }

    /* Setup driver data for this window */
    window->driverdata = wdata;
    
    /* One window, it always has focus */
    SDL_SetMouseFocus(window);
    SDL_SetKeyboardFocus(window);

    /* Window has been successfully created */
    return 0;
}

void
GBM_DestroyWindow(_THIS, SDL_Window * window)
{
    SDL_WindowData *data;
        
    if(window->driverdata) {
        data = (SDL_WindowData *) window->driverdata;
        if (data->egl_surface != EGL_NO_SURFACE) {
            SDL_EGL_DestroySurface(_this, data->egl_surface);
            data->egl_surface = EGL_NO_SURFACE;
        }
        SDL_free(window->driverdata);
        window->driverdata = NULL;
    }
}

int
GBM_CreateWindowFrom(_THIS, SDL_Window * window, const void *data)
{
    return -1;
}

void
GBM_SetWindowTitle(_THIS, SDL_Window * window)
{
}
void
GBM_SetWindowIcon(_THIS, SDL_Window * window, SDL_Surface * icon)
{
}
void
GBM_SetWindowPosition(_THIS, SDL_Window * window)
{
}
void
GBM_SetWindowSize(_THIS, SDL_Window * window)
{
}
void
GBM_ShowWindow(_THIS, SDL_Window * window)
{
}
void
GBM_HideWindow(_THIS, SDL_Window * window)
{
}
void
GBM_RaiseWindow(_THIS, SDL_Window * window)
{
}
void
GBM_MaximizeWindow(_THIS, SDL_Window * window)
{
}
void
GBM_MinimizeWindow(_THIS, SDL_Window * window)
{
}
void
GBM_RestoreWindow(_THIS, SDL_Window * window)
{
}
void
GBM_SetWindowGrab(_THIS, SDL_Window * window, SDL_bool grabbed)
{

}

/*****************************************************************************/
/* SDL Window Manager function                                               */
/*****************************************************************************/
SDL_bool
GBM_GetWindowWMInfo(_THIS, SDL_Window * window, struct SDL_SysWMinfo *info)
{
    if (info->version.major <= SDL_MAJOR_VERSION) {
        return SDL_TRUE;
    } else {
        SDL_SetError("application not compiled with SDL %d.%d\n",
                     SDL_MAJOR_VERSION, SDL_MINOR_VERSION);
        return SDL_FALSE;
    }

    /* Failed to get window manager information */
    return SDL_FALSE;
}

#endif /* SDL_VIDEO_DRIVER_GBM */

/* vi: set ts=4 sw=4 expandtab: */
