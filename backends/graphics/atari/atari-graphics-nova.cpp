/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#define FORCE_TEXT_CONSOLE

#define FORBIDDEN_SYMBOL_ALLOW_ALL

#include "backends/platform/atari/dlmalloc.h"
#include "backends/graphics/atari/atari-graphics-nova.h"

#include <mint/cookie.h>
#include <mint/falcon.h>
#include <mint/osbind.h>
#include <mint/sysvars.h>
#include <gem.h>

#include "backends/keymapper/action.h"
#include "backends/keymapper/keymap.h"

#include "common/config-manager.h"
#include "common/str.h"
#include "common/textconsole.h"	// for warning() & error()
#include "common/translation.h"
#include "engines/engine.h"
#include "graphics/blit.h"
#include "gui/ThemeEngine.h"

//#define SCREEN_ACTIVE

static nova_xcb_t* s_nova_xcb;
static nova_bibres_t s_oldRes;
static nova_bibres_t s_res200;

static uint16_t s_oldpalsize = 0;
static uint16 s_oldpal[256*3];
static _RGB s_novapal[256];


static int initNovaModes() {
	if ((Getcookie(C_NOVA, (long*)&s_nova_xcb) != C_FOUND) || !s_nova_xcb) {
		warning("Nova cookie not found");
		return 0;
	}
	if (s_nova_xcb->version != NOVA_VERSION) {
		warning("Nova version invalid %08x / %08x", s_nova_xcb->version, (uint32_t)NOVA_VERSION);
		return 0;
	}

	char filename[32];
    strcpy(filename, "c:\\auto\\sta_vdi.bib");
	{
		long oldssp = Super(SUP_SET);
    	filename[0] = 'a'+ *((volatile unsigned short *)0x446);
		Super((void *)oldssp);		
	}

	FILE* f = fopen(filename, "rb");
	if (!f) {
		warning("Nova bib not found");
		return 0;
	}

	fseek(f, 0, SEEK_END);
	unsigned int fsize = (int)ftell(f);
	fseek(f, 0, SEEK_SET);

	for (int fpos = 0, m = 0; (fpos + sizeof(nova_bibres_t)) <= fsize; fpos += sizeof(nova_bibres_t), m++)
	{
		nova_bibres_t res;
		fread(&res, 1, sizeof(nova_bibres_t), f);
        if (m == s_nova_xcb->resolution) {
            memcpy(&s_oldRes, &res, sizeof(nova_bibres_t));
			debug(" nova: %dx%d %dbpp (desktop)", res.real_x+1, res.real_y+1, res.planes);
        } else {
			debug(" nova: %dx%d %dbpp", res.real_x+1, res.real_y+1, res.planes);
		}
		if (res.planes == 8) {
			if ((res.real_x == 319) && (res.real_y == 199)) {
				memcpy(&s_res200, &res, sizeof(nova_bibres_t));
			}
		}
	}

	fclose(f);
	return (s_res200.real_x) ? 1 : 0;
}

static void setNovaMode(nova_bibres_t* res) {
	long oldssp = Super(SUP_SET);
    __asm__ __volatile__ (
            "moveql	#0,%%d0\n\t"
            "movel	%0,%%a0\n\t"
            "movel	%1,%%a1\n\t"
            "jsr	%%a1@\n\t"
			"movel	%2,%%a0\n\t"
			"jsr	%%a0@\n\t"
        : : "g"(res), "g"(s_nova_xcb->p_changeres), "g"(s_nova_xcb->p_vsync)
        : "d0", "d1", "d2", "a0", "a1", "a2", "cc", "memory"
    );
	Super((void *)oldssp);		
}

static void setNovaPalette(int start, int count, _RGB* pal) {
	long oldssp = Super(SUP_SET);
	for (int i=start; i<(start+count) && (i < 256); i++, pal++) {
		uint32_t* ps = (uint32_t*)pal;
		uint32_t* pd = (uint32_t*)&s_novapal[i];
		if (*ps != *pd) {
			*pd = *ps;
			char* colors = &pal->red;
			__asm__ __volatile__ (
					"movel	%0,%%d0\n\t"
					"movel	%1,%%a0\n\t"
					"movel	%2,%%a1\n\t"
					"jsr	%%a1@"
				: : "g"(i), "g"(colors), "g"(s_nova_xcb->p_setcolor)
				: "d0", "d1", "d2", "a0", "a1", "a2", "cc", "memory"
			);
		}
	}
	Super((void *)oldssp);		
}


#define MAX_HZ_SHAKE 16 // Falcon only
#define MAX_V_SHAKE  16

static int s_shakeXOffset;
static int s_shakeYOffset;

static Graphics::Surface *s_screenSurf;
static void VblHandler() {
	if (s_screenSurf) {
#ifdef SCREEN_ACTIVE
		const int bitsPerPixel = (s_screenSurf->format == PIXELFORMAT_RGB121 ? 4 : 8);
		uintptr p = (uintptr)s_screenSurf->getBasePtr(0, MAX_V_SHAKE + s_shakeYOffset);

		if (!s_tt) {
			s_shakeXOffset = -s_shakeXOffset;

			if (s_shakeXOffset >= 0) {
				p += MAX_HZ_SHAKE;
				*((volatile char *)0xFFFF8265) = s_shakeXOffset;
			} else {
				*((volatile char *)0xFFFF8265) = MAX_HZ_SHAKE + s_shakeXOffset;
			}

			// subtract 4 or 8 words if scrolling
			*((volatile short *)0xFFFF820E) = s_shakeXOffset == 0
			   ? (2 * MAX_HZ_SHAKE * bitsPerPixel / 8) / 2
			   : (2 * MAX_HZ_SHAKE * bitsPerPixel / 8) / 2 - bitsPerPixel;
		}

		union { byte c[4]; uintptr p; } sptr;
		sptr.p = p;

		*((volatile byte *)0xFFFF8201) = sptr.c[1];
		*((volatile byte *)0xFFFF8203) = sptr.c[2];
		*((volatile byte *)0xFFFF820D) = sptr.c[3];
#endif
		s_screenSurf = nullptr;
	}
}

static uint32 InstallVblHandler() {
	uint32 installed = 0;
	*vblsem = 0;  // lock vbl

	for (int i = 0; i < *nvbls; ++i) {
		if (!(*_vblqueue)[i]) {
			(*_vblqueue)[i] = VblHandler;
			installed = 1;
			break;
		}
	}

	*vblsem = 1;  // unlock vbl
	return installed;
}

static uint32 UninstallVblHandler() {
	uint32 uninstalled = 0;
	*vblsem = 0;  // lock vbl

	for (int i = 0; i < *nvbls; ++i) {
		if ((*_vblqueue)[i] == VblHandler) {
			(*_vblqueue)[i] = NULL;
			uninstalled = 1;
			break;
		}
	}

	*vblsem = 1;  // unlock vbl
	return uninstalled;
}

void AtariGraphicsShutdown() {
	debug("AtariGraphicsShutdown");
	Supexec(UninstallVblHandler);

	// restore video mode
	if (s_oldRes.real_x) {
		setNovaMode(&s_oldRes);
		memset(&s_oldRes, 0, sizeof(nova_bibres_t));
	}

	// restore palette
	if (s_oldpalsize) {
		int16_t dummy; int16_t vdiHandlep = graf_handle(&dummy, &dummy, &dummy, &dummy);
		for (uint16 i = 0; i < s_oldpalsize; i++) {
			vs_color(vdiHandlep, i, (int16_t*)&s_oldpal[i * 3]);
		}
		s_oldpalsize = 0;
	}
}

AtariGraphicsManager::AtariGraphicsManager() {
	debug("AtariGraphicsManager()");

	// no BDF scaling please
	ConfMan.registerDefault("gui_disable_fixed_font_scaling", true);

	// make the standard GUI renderer default (!DISABLE_FANCY_THEMES implies anti-aliased rendering in ThemeEngine.cpp)
	// (and without DISABLE_FANCY_THEMES we can't use 640x480 themes)
	const char *standardThemeEngineName = GUI::ThemeEngine::findModeConfigName(GUI::ThemeEngine::kGfxStandard);
	if (!ConfMan.hasKey("gui_renderer"))
		ConfMan.set("gui_renderer", standardThemeEngineName);

	// make the built-in theme default to avoid long loading times
	if (!ConfMan.hasKey("gui_theme"))
		ConfMan.set("gui_theme", "builtin");

#ifndef DISABLE_FANCY_THEMES
	// make "themes" the default theme path
	if (!ConfMan.hasKey("themepath"))
		ConfMan.setPath("themepath", "themes");
#endif

	ConfMan.flushToDisk();

	if (initNovaModes() < 1) {
		error("No valid Nova gfxmodes");
	}

	// backup palette
	int16_t dummy; int16_t vdiHandlep = graf_handle(&dummy, &dummy, &dummy, &dummy);
    s_oldpalsize = ((vdiHandlep >= 0) && (s_nova_xcb->planes <= 8)) ? (1 << s_nova_xcb->planes) : 0;
    for (uint16_t i = 0; i < s_oldpalsize; i++) {
        vq_color(vdiHandlep, i, 1, (int16_t*)&s_oldpal[i * 3]);
    }

	// Generate RGB332/RGB121 palette for the overlay
	const Graphics::PixelFormat &format = getOverlayFormat();
	const int paletteSize = getOverlayPaletteSize();
	for (int i = 0; i < paletteSize; i++) {
		_overlayPalette.falcon[i].red    = ((i >> format.rShift) & format.rMax()) << format.rLoss;
		_overlayPalette.falcon[i].green |= ((i >> format.gShift) & format.gMax()) << format.gLoss;
		_overlayPalette.falcon[i].blue  |= ((i >> format.bShift) & format.bMax()) << format.bLoss;
	}

	allocateSurfaces();

	if (!Supexec(InstallVblHandler)) {
		error("VBL handler was not installed");
	}

	setNovaMode(&s_res200);
	memset(s_novapal, 0xff, sizeof(_RGB) * 256);
	for (int i=0; i<256; i++) {
		uint32_t black = 0;
		setNovaPalette(i, 1, (_RGB*)&black);
	}

	g_system->getEventManager()->getEventDispatcher()->registerObserver(this, 10, false);
}

AtariGraphicsManager::~AtariGraphicsManager() {
	debug("~AtariGraphicsManager()");

	g_system->getEventManager()->getEventDispatcher()->unregisterObserver(this);

	// this must be done here, too otherwise freeSurfaces() could release a surface
	// still accessed by the vbl handler
	Supexec(UninstallVblHandler);

	freeSurfaces();

	AtariGraphicsShutdown();
}

bool AtariGraphicsManager::hasFeature(OSystem::Feature f) const {
	switch (f) {
	case OSystem::Feature::kFeatureAspectRatioCorrection:
		return false;
	case OSystem::Feature::kFeatureCursorPalette:
		// FIXME: pretend to have cursor palette at all times, this function
		// can get (and it is) called any time, before and after showOverlay()
		// (overlay cursor uses the cross if kFeatureCursorPalette returns false
		// here too soon)
		//debug("hasFeature(kFeatureCursorPalette): %d", isOverlayVisible());
		//return isOverlayVisible();
		return true;
	default:
		return false;
	}
}

void AtariGraphicsManager::setFeatureState(OSystem::Feature f, bool enable) {
	switch (f) {
	case OSystem::Feature::kFeatureAspectRatioCorrection:
		//debug("setFeatureState(kFeatureAspectRatioCorrection): %d", enable);
		_oldAspectRatioCorrection = _aspectRatioCorrection;
		_aspectRatioCorrection = enable;
		break;
	default:
		break;
	}
}

bool AtariGraphicsManager::getFeatureState(OSystem::Feature f) const {
	switch (f) {
	case OSystem::Feature::kFeatureAspectRatioCorrection:
		//debug("getFeatureState(kFeatureAspectRatioCorrection): %d", _aspectRatioCorrection);
		return _aspectRatioCorrection;
	case OSystem::Feature::kFeatureCursorPalette:
		//debug("getFeatureState(kFeatureCursorPalette): %d", isOverlayVisible());
		//return isOverlayVisible();
		return true;
	default:
		return false;
	}
}

bool AtariGraphicsManager::setGraphicsMode(int mode, uint flags) {
	debug("setGraphicsMode: %d, %d", mode, flags);
	GraphicsMode graphicsMode = (GraphicsMode)mode;
	if (graphicsMode >= GraphicsMode::DirectRendering && graphicsMode <= GraphicsMode::TripleBuffering) {
		_pendingState.mode = graphicsMode;
		return true;
	}
	return false;
}

void AtariGraphicsManager::initSize(uint width, uint height, const Graphics::PixelFormat *format) {
	debug("initSize: %d, %d, %d", width, height, format ? format->bytesPerPixel : 1);
	_pendingState.width = width;
	_pendingState.height = height;
	_pendingState.format = format ? *format : PIXELFORMAT_CLUT8;
}

void AtariGraphicsManager::beginGFXTransaction() {
	debug("beginGFXTransaction");
}

OSystem::TransactionError AtariGraphicsManager::endGFXTransaction() {
	debug("endGFXTransaction");

	int error = OSystem::TransactionError::kTransactionSuccess;

	if (_pendingState.format != PIXELFORMAT_CLUT8)
		error |= OSystem::TransactionError::kTransactionFormatNotSupported;

	if (_pendingState.width > getMaximumScreenWidth() || _pendingState.height > getMaximumScreenHeight())
		error |= OSystem::TransactionError::kTransactionSizeChangeFailed;

#if 0
	if (_pendingState.width % 16 != 0 && !hasSuperVidel()) {
		warning("Requested width not divisible by 16, please report");
		error |= OSystem::TransactionError::kTransactionSizeChangeFailed;
	}
#endif

	if (error != OSystem::TransactionError::kTransactionSuccess) {
		warning("endGFXTransaction failed: %02x", (int)error);
		// all our errors are fatal but engine.cpp takes only this one seriously
		error |= OSystem::TransactionError::kTransactionSizeChangeFailed;
		return static_cast<OSystem::TransactionError>(error);
	}

	_chunkySurface.init(_pendingState.width, _pendingState.height, _pendingState.width,
		_chunkySurface.getPixels(), _pendingState.format);

	_screen[FRONT_BUFFER]->reset(_pendingState.width, _pendingState.height);
	_workScreen = _screen[FRONT_BUFFER];

	s_screenSurf = nullptr;
	s_shakeXOffset = 0;
	s_shakeYOffset = 0;

	// in case of resolution change from GUI
	if (_oldWorkScreen)
		_oldWorkScreen = _workScreen;

	_palette.clear();
	_pendingScreenChange = kPendingScreenChangeMode | kPendingScreenChangeScreen | kPendingScreenChangePalette;

	static bool firstRun = true;
	if (firstRun) {
		_cursor.setPosition(getOverlayWidth() / 2, getOverlayHeight() / 2);
		_cursor.swap();
		firstRun = false;
	}

	warpMouse(_pendingState.width / 2, _pendingState.height / 2);

	_currentState = _pendingState;

	return OSystem::kTransactionSuccess;
}

void AtariGraphicsManager::setPalette(const byte *colors, uint start, uint num) {
	//debug("setPalette: %d, %d", start, num);
	_RGB *pal = &_palette.falcon[start];
	for (uint i = 0; i < num; ++i) {
		pal[i].red   = colors[i * 3 + 0];
		pal[i].green = colors[i * 3 + 1];
		pal[i].blue  = colors[i * 3 + 2];
	}
	_pendingScreenChange |= kPendingScreenChangePalette;
}

void AtariGraphicsManager::grabPalette(byte *colors, uint start, uint num) const {
	//debug("grabPalette: %d, %d", start, num);
	const _RGB *pal = &_palette.falcon[start];
	for (uint i = 0; i < num; ++i) {
		*colors++ = pal[i].red;
		*colors++ = pal[i].green;
		*colors++ = pal[i].blue;
	}
}

void AtariGraphicsManager::copyRectToScreen(const void *buf, int pitch, int x, int y, int w, int h) {
	//debug("copyRectToScreen: %d, %d, %d(%d), %d", x, y, w, pitch, h);

	copyRectToScreenInternal(buf, pitch, x, y, w, h,
		PIXELFORMAT_CLUT8,
		_currentState.mode == GraphicsMode::DirectRendering,
		_currentState.mode == GraphicsMode::TripleBuffering);
}

// this is not really locking anything but it's an useful function
// to return current rendering surface :)
Graphics::Surface *AtariGraphicsManager::lockScreen() {
	//debug("lockScreen");

	if (isOverlayVisible() && !isOverlayDirectRendering())
		return &_overlaySurface;
	else if ((isOverlayVisible() && isOverlayDirectRendering()) || _currentState.mode == GraphicsMode::DirectRendering)
		return _workScreen->offsettedSurf;
	else
		return &_chunkySurface;
}

void AtariGraphicsManager::unlockScreen() {
	//debug("unlockScreen: %d x %d", _workScreen->surf.w, _workScreen->surf.h);

	const Graphics::Surface &dstSurface = *lockScreen();

	const bool directRendering = (dstSurface.getPixels() != _chunkySurface.getPixels());
	const Common::Rect rect(dstSurface.w, dstSurface.h);
	_workScreen->addDirtyRect(dstSurface, rect, directRendering);

	// doc says:
	// Unlock the screen framebuffer, and mark it as dirty, i.e. during the
	// next updateScreen() call, the whole screen will be updated.
	//
	// ... so no updateScreen() from here (otherwise Eco Quest's intro is crawling!)
}

void AtariGraphicsManager::fillScreen(uint32 col) {
	debug("fillScreen: %d", col);

	Graphics::Surface *screen = lockScreen();
	screen->fillRect(Common::Rect(screen->w, screen->h), col);
	unlockScreen();
}

void AtariGraphicsManager::fillScreen(const Common::Rect &r, uint32 col) {
	Graphics::Surface *screen = lockScreen();
	if (screen)
		screen->fillRect(r, col);
	unlockScreen();
}

void AtariGraphicsManager::updateScreen() {
	//debug("updateScreen");

	// avoid falling into the debugger (screen may not not initialized yet)
	Common::setErrorHandler(nullptr);

	// updates outOfScreen OR srcRect/dstRect (only if visible/needed)
	_cursor.update(*lockScreen(), _workScreen->cursorPositionChanged || _workScreen->cursorSurfaceChanged);

	bool screenUpdated = false;

	if (isOverlayVisible()) {
		assert(_workScreen == _screen[OVERLAY_BUFFER]);
		if (isOverlayDirectRendering())
			screenUpdated = updateScreenInternal<true>(Graphics::Surface());
		else
			screenUpdated = updateScreenInternal<false>(_overlaySurface);
	} else {
		switch (_currentState.mode) {
		case GraphicsMode::DirectRendering:
			assert(_workScreen == _screen[FRONT_BUFFER]);
			screenUpdated = updateScreenInternal<true>(Graphics::Surface());
			break;
		case GraphicsMode::SingleBuffering:
			assert(_workScreen == _screen[FRONT_BUFFER]);
			screenUpdated = updateScreenInternal<false>(_chunkySurface);
			break;
		}
	}

	_workScreen->clearDirtyRects();

	if (_pendingScreenChange & kPendingScreenChangePalette) {
		int num = isOverlayVisible() ? getOverlayPaletteSize() : 256;
		setNovaPalette(0, isOverlayVisible() ? getOverlayPaletteSize() : 256, _workScreen->palette->falcon);
	}

	_pendingScreenChange = kPendingScreenChangeNone;


#ifdef SCREEN_ACTIVE
	// first change video mode so we can modify video regs later
	if (_pendingScreenChange & kPendingScreenChangeMode) {
		if (_workScreen->rez != -1) {
			// unfortunately this reinitializes VDI, too
			//Setscreen(SCR_NOCHANGE, SCR_NOCHANGE, _workScreen->rez);
		} else if (_workScreen->mode != -1) {
			//VsetMode(_workScreen->mode);
		}
	}

	if (_pendingScreenChange & kPendingScreenChangeScreen) {
		// calling (V)SetScreen without Vsync() is dangerous (at least on Falcon)
		s_screenSurf = isOverlayVisible() ? &_screen[OVERLAY_BUFFER]->surf : &_screen[FRONT_BUFFER]->surf;
	} else if (screenUpdated && !isOverlayVisible() && _currentState.mode == GraphicsMode::TripleBuffering) {
		// Triple buffer:
		// - alternate BACK_BUFFER1 and BACK_BUFFER2
		// - check if FRONT_BUFFER has been displayed for at least one frame
		// - display the most recent buffer (BACK_BUFFER2 in our case)
		// - alternate BACK_BUFFER2 and FRONT_BUFFER (only if BACK_BUFFER2
		//   has been updated)

		set_sysvar_to_short(vblsem, 0);  // lock vbl

		static long old_vbclock = get_sysvar(_vbclock);
		long curr_vbclock = get_sysvar(_vbclock);

		if (old_vbclock != curr_vbclock) {
			// at least one vbl has passed since setting new video base
			// guard BACK_BUFFER2 from overwriting while presented
			Screen *tmp = _screen[BACK_BUFFER2];
			_screen[BACK_BUFFER2] = _screen[FRONT_BUFFER];
			_screen[FRONT_BUFFER] = tmp;

			old_vbclock = curr_vbclock;
		}

		// swap back buffers
		Screen *tmp = _screen[BACK_BUFFER1];
		_screen[BACK_BUFFER1] = _screen[BACK_BUFFER2];
		_screen[BACK_BUFFER2] = tmp;

		// queue BACK_BUFFER2 with the most recent frame content
		s_screenSurf = &_screen[BACK_BUFFER2]->surf;

		set_sysvar_to_short(vblsem, 1);  // unlock vbl

		_workScreen = _screen[BACK_BUFFER1];
		// BACK_BUFFER2: now contains finished frame
		// FRONT_BUFFER is displayed and still contains previously finished frame
	}

	if (_pendingScreenChange & kPendingScreenChangePalette) {
		if (_tt)
			EsetPalette(0, isOverlayVisible() ? getOverlayPaletteSize() : 256, _workScreen->palette->tt);
		else
			VsetRGB(0, isOverlayVisible() ? getOverlayPaletteSize() : 256, _workScreen->palette->falcon);
	}

	_pendingScreenChange = kPendingScreenChangeNone;

	if (_oldAspectRatioCorrection != _aspectRatioCorrection) {
		if (!isOverlayVisible() && _currentState.height == 200) {
			if (!_vgaMonitor) {
				short mode = VsetMode(VM_INQUIRE);
				if (_aspectRatioCorrection) {
					// 60 Hz
					mode &= ~PAL;
					mode |= NTSC;
				} else {
					// 50 Hz
					mode &= ~NTSC;
					mode |= PAL;
				}
				//VsetMode(mode);
			} else if (hasSuperVidel() || (!_tt && !_ctpci)) {
				if (_aspectRatioCorrection) {
					for (int screenId : { FRONT_BUFFER, BACK_BUFFER1, BACK_BUFFER2 }) {
						Screen *screen = _screen[screenId];
						Graphics::Surface *offsettedSurf = screen->offsettedSurf;

						// erase old screen
						offsettedSurf->fillRect(Common::Rect(offsettedSurf->w, offsettedSurf->h), 0);

						// setup new screen
						screen->oldScreenSurfaceWidth = screen->surf.w;
						screen->oldScreenSurfaceHeight = screen->surf.h;
						screen->oldScreenSurfacePitch = screen->surf.pitch;
						screen->oldOffsettedSurfaceWidth = offsettedSurf->w;
						screen->oldOffsettedSurfaceHeight = offsettedSurf->h;

						screen->surf.w = 320 + 2 * MAX_HZ_SHAKE;
						screen->surf.h = 200 + 2 * MAX_V_SHAKE;
						screen->surf.pitch = screen->surf.w;

						offsettedSurf->init(
							320, 200, screen->surf.pitch,
							screen->surf.getBasePtr((screen->surf.w - 320) / 2, (screen->surf.h - 200) / 2),
							screen->surf.format);

						screen->addDirtyRect(*lockScreen(), Common::Rect(offsettedSurf->w, offsettedSurf->h), _currentState.mode == GraphicsMode::DirectRendering);
					}

					Supexec(shrinkVidelVisibleArea);
				} else {
					for (int screenId : { FRONT_BUFFER, BACK_BUFFER1, BACK_BUFFER2 }) {
						Screen *screen = _screen[screenId];
						Graphics::Surface *offsettedSurf = screen->offsettedSurf;

						assert(screen->oldScreenSurfaceWidth != -1);
						assert(screen->oldScreenSurfaceHeight != -1);
						assert(screen->oldScreenSurfacePitch != -1);
						assert(screen->oldOffsettedSurfaceWidth != -1);
						assert(screen->oldOffsettedSurfaceHeight != -1);

						// erase old screen
						offsettedSurf->fillRect(Common::Rect(offsettedSurf->w, offsettedSurf->h), 0);

						// setup new screen
						screen->surf.w = screen->oldScreenSurfaceWidth;
						screen->surf.h = screen->oldScreenSurfaceHeight;
						screen->surf.pitch = screen->oldScreenSurfacePitch;

						offsettedSurf->init(
							screen->oldOffsettedSurfaceWidth, screen->oldOffsettedSurfaceHeight, screen->surf.pitch,
							screen->surf.getBasePtr(
								(screen->surf.w - screen->oldOffsettedSurfaceWidth) / 2,
								(screen->surf.h - screen->oldOffsettedSurfaceHeight) / 2),
							screen->surf.format);

						screen->oldScreenSurfaceWidth = -1;
						screen->oldScreenSurfaceHeight = -1;
						screen->oldScreenSurfacePitch = -1;
						screen->oldOffsettedSurfaceWidth = -1;
						screen->oldOffsettedSurfaceHeight = -1;

						screen->addDirtyRect(*lockScreen(), Common::Rect(offsettedSurf->w, offsettedSurf->h), _currentState.mode == GraphicsMode::DirectRendering);
					}

					//if (hasSuperVidel())
					//	VsetMode(SVEXT | SVEXT_BASERES(0) | COL80 | BPS8C);	// resync to proper 640x480
					//VsetMode(_workScreen->mode);
				}
			} else {
				// TODO: some tricks with TT's 480 lines?
			}

			_oldAspectRatioCorrection = _aspectRatioCorrection;

			_pendingScreenChange |= kPendingScreenChangeScreen;
			updateScreen();
		} else {
			// ignore new value in overlay
			_aspectRatioCorrection = _oldAspectRatioCorrection;
		}
	}
#endif
	//debug("end of updateScreen");
}

void AtariGraphicsManager::setShakePos(int shakeXOffset, int shakeYOffset) {
	//debug("setShakePos: %d, %d", shakeXOffset, shakeYOffset);
	return;
#if 0
	if (_ctpci)
		return;

	if (_tt) {
		// as TT can't horizontally shake anything, do it at least vertically
		s_shakeYOffset = (shakeYOffset == 0 && shakeXOffset != 0) ? shakeXOffset : shakeYOffset;
	} else {
		s_shakeXOffset = shakeXOffset;
		s_shakeYOffset = shakeYOffset;
	}
	_pendingScreenChange |= kPendingScreenChangeScreen;
	updateScreen();
#endif	
}

void AtariGraphicsManager::showOverlay(bool inGUI) {
	debug("showOverlay");

	if (_overlayVisible)
		return;

	if (_currentState.mode == GraphicsMode::DirectRendering) {
		// make sure that _oldCursorRect is used to restore the original game graphics
		// (but only if resolution hasn't changed, see endGFXTransaction())
		bool wasVisible = showMouse(false);

		// revert back but don't update screen
		_cursor.visible = wasVisible;
	}

	_cursor.swap();
	_oldWorkScreen = _workScreen;
	_workScreen = _screen[OVERLAY_BUFFER];

	// do not cache dirtyRects and oldCursorRect
	_workScreen->reset(getOverlayWidth(), getOverlayHeight());

	_pendingScreenChange = kPendingScreenChangeMode | kPendingScreenChangeScreen | kPendingScreenChangePalette;

	_overlayVisible = true;

	updateScreen();
}

void AtariGraphicsManager::hideOverlay() {
	debug("hideOverlay");

	if (!_overlayVisible)
		return;

	_workScreen = _oldWorkScreen;
	_oldWorkScreen = nullptr;
	_cursor.swap();

	_pendingScreenChange = kPendingScreenChangeMode | kPendingScreenChangeScreen | kPendingScreenChangePalette;

	_overlayVisible = false;

	updateScreen();
}

void AtariGraphicsManager::clearOverlay() {
	if (isOverlayDirectRendering())
		return;

	debug("clearOverlay");

	if (!_overlayVisible)
		return;

	const Graphics::Surface &sourceSurface =
		_currentState.mode == GraphicsMode::DirectRendering ? *_screen[FRONT_BUFFER]->offsettedSurf : _chunkySurface;

	const bool upscale = _overlaySurface.w / sourceSurface.w >= 2 && _overlaySurface.h / sourceSurface.h >= 2;

	const int w = upscale ? sourceSurface.w * 2 : sourceSurface.w;
	const int h = upscale ? sourceSurface.h * 2 : sourceSurface.h;

	const int hzOffset = (_overlaySurface.w - w) / 2;
	const int vOffset  = (_overlaySurface.h - h) / 2;

	const int srcPadding = sourceSurface.pitch - sourceSurface.w;
	const int dstPadding = hzOffset * 2 + (upscale ? _overlaySurface.pitch : 0);

	// Transpose from game palette to RGB332/RGB121 (overlay palette)
	const byte *src = (const byte*)sourceSurface.getPixels();
	byte *dst = (byte *)_overlaySurface.getBasePtr(hzOffset, vOffset);

	static const int rShift = _overlaySurface.format.rLoss - _overlaySurface.format.rShift;
	static const int gShift = _overlaySurface.format.gLoss - _overlaySurface.format.gShift;
	static const int bShift = _overlaySurface.format.bLoss - _overlaySurface.format.bShift;
	static const int rMask = _overlaySurface.format.rMax() << _overlaySurface.format.rShift;
	static const int gMask = _overlaySurface.format.gMax() << _overlaySurface.format.gShift;
	static const int bMask = _overlaySurface.format.bMax() << _overlaySurface.format.bShift;

	for (int y = 0; y < sourceSurface.h; y++) {
		for (int x = 0; x < sourceSurface.w; x++) {
			byte pixel;

			const _RGB &col = _palette.falcon[*src++];
			pixel = ((col.red   >> rShift) & rMask)
					| ((col.green >> gShift) & gMask)
					| ((col.blue  >> bShift) & bMask);

			if (upscale) {
				*(dst + _overlaySurface.pitch) = pixel;
				*dst++ = pixel;
				*(dst + _overlaySurface.pitch) = pixel;
			}
			*dst++ = pixel;
		}

		src += srcPadding;
		dst += dstPadding;
	}

	memset(_overlaySurface.getBasePtr(0, 0), 0, vOffset * _overlaySurface.pitch);
	memset(_overlaySurface.getBasePtr(0, _overlaySurface.h - vOffset), 0, vOffset * _overlaySurface.pitch);
	_overlaySurface.fillRect(Common::Rect(0, vOffset, hzOffset, _overlaySurface.h - vOffset), 0);
	_overlaySurface.fillRect(Common::Rect(_overlaySurface.w - hzOffset, vOffset, _overlaySurface.w, _overlaySurface.h - vOffset), 0);
	_screen[OVERLAY_BUFFER]->addDirtyRect(_overlaySurface, Common::Rect(_overlaySurface.w, _overlaySurface.h), false);
}

void AtariGraphicsManager::grabOverlay(Graphics::Surface &surface) const {
	debug("grabOverlay: %d(%d), %d", surface.w, surface.pitch, surface.h);

	if (isOverlayDirectRendering()) {
		memset(surface.getPixels(), 0, surface.h * surface.pitch);
	} else {
		assert(surface.w >= _overlaySurface.w);
		assert(surface.h >= _overlaySurface.h);
		assert(surface.format.bytesPerPixel == _overlaySurface.format.bytesPerPixel);
#if 1
		surface.copyRectToSurface(_overlaySurface, 0, 0, Common::Rect(_overlaySurface.w, _overlaySurface.h));
#else
		const byte *src = (const byte *)_overlaySurface.getPixels();
		byte *dst = (byte *)surface.getPixels();
		Graphics::copyBlit(dst, src, surface.pitch,
			_overlaySurface.pitch, _overlaySurface.w, _overlaySurface.h, _overlaySurface.format.bytesPerPixel);
#endif			
	}
}

void AtariGraphicsManager::copyRectToOverlay(const void *buf, int pitch, int x, int y, int w, int h) {
	debug("copyRectToOverlay: %d, %d, %d(%d), %d", x, y, w, pitch, h);

	copyRectToScreenInternal(buf, pitch, x, y, w, h,
		getOverlayFormat(),
		isOverlayDirectRendering(),
		false);
}

bool AtariGraphicsManager::showMouse(bool visible) {
	//debug("showMouse: %d", visible);

	if (_cursor.visible == visible) {
		return visible;
	}

	bool last = _cursor.visible;
	_cursor.visible = visible;

	cursorVisibilityChanged();
	// don't rely on engines to call it (if they don't it confuses the cursor restore logic)
	updateScreen();

	return last;
}

void AtariGraphicsManager::warpMouse(int x, int y) {
	//debug("warpMouse: %d, %d", x, y);

	_cursor.setPosition(x, y);
	cursorPositionChanged();
}

void AtariGraphicsManager::setMouseCursor(const void *buf, uint w, uint h, int hotspotX, int hotspotY, uint32 keycolor,
										  bool dontScale, const Graphics::PixelFormat *format, const byte *mask) {
	//debug("setMouseCursor: %d, %d, %d, %d, %d, %d", w, h, hotspotX, hotspotY, keycolor, format ? format->bytesPerPixel : 1);

	if (mask)
		warning("AtariGraphicsManager::setMouseCursor: Masks are not supported");

	if (format)
		assert(*format == PIXELFORMAT_CLUT8);

	_cursor.setSurface(buf, (int)w, (int)h, hotspotX, hotspotY, keycolor);
	cursorSurfaceChanged();
}

void AtariGraphicsManager::setCursorPalette(const byte *colors, uint start, uint num) {
	debug("setCursorPalette: %d, %d", start, num);

	memcpy(&_cursor.palette[start * 3], colors, num * 3);
	cursorSurfaceChanged();
}

void AtariGraphicsManager::updateMousePosition(int deltaX, int deltaY) {
	//debug("updateMousePosition %d %d", deltaX, deltaY);
	_cursor.updatePosition(deltaX, deltaY, *lockScreen());
	cursorPositionChanged();
}

bool AtariGraphicsManager::notifyEvent(const Common::Event &event) {
	if (event.type != Common::EVENT_CUSTOM_BACKEND_ACTION_START) {
		return false;
	}

	switch ((CustomEventAction) event.customType) {
	case kActionToggleAspectRatioCorrection:
		_aspectRatioCorrection = !_aspectRatioCorrection;
		return true;
	}

	return false;
}

Common::Keymap *AtariGraphicsManager::getKeymap() const {
	Common::Keymap *keymap = new Common::Keymap(Common::Keymap::kKeymapTypeGlobal, "atari-graphics", _("Graphics"));
	Common::Action *act;

	if (hasFeature(OSystem::kFeatureAspectRatioCorrection)) {
		act = new Common::Action("ASPT", _("Toggle aspect ratio correction"));
		act->addDefaultInputMapping("C+A+a");
		act->setCustomBackendActionEvent(kActionToggleAspectRatioCorrection);
		keymap->addAction(act);
	}

	return keymap;
}

void AtariGraphicsManager::allocateSurfaces() {
	for (int i : { FRONT_BUFFER }) {
		_screen[i] = new Screen(this, getMaximumScreenWidth(), getMaximumScreenHeight(), PIXELFORMAT_CLUT8, &_palette);
	}

	_screen[OVERLAY_BUFFER] = new Screen(this, getOverlayWidth(), getOverlayHeight(), getOverlayFormat(), &_overlayPalette);

	_chunkySurface.create(getMaximumScreenWidth(), getMaximumScreenHeight(), PIXELFORMAT_CLUT8);
	_overlaySurface.create(getOverlayWidth(), getOverlayHeight(), getOverlayFormat());
}

void AtariGraphicsManager::freeSurfaces() {
	for (int i : { FRONT_BUFFER, OVERLAY_BUFFER }) {
		//delete _screen[i];
		_screen[i] = nullptr;
	}
	_workScreen = nullptr;

	_chunkySurface.free();
	_overlaySurface.free();
}

template <bool directRendering>	// hopefully compiler optimizes all the branching out
bool AtariGraphicsManager::updateScreenInternal(const Graphics::Surface &srcSurface) {
	//debug("updateScreenInternal");

	const DirtyRects &dirtyRects  = _workScreen->dirtyRects;
	Graphics::Surface *dstSurface = _workScreen->offsettedSurf;
	bool &cursorPositionChanged   = _workScreen->cursorPositionChanged;
	bool &cursorSurfaceChanged    = _workScreen->cursorSurfaceChanged;
	bool &cursorVisibilityChanged = _workScreen->cursorVisibilityChanged;
	Common::Rect &oldCursorRect   = _workScreen->oldCursorRect;
	const bool &fullRedraw        = _workScreen->fullRedraw;

	bool updated = false;

	const bool cursorDrawEnabled = !_cursor.outOfScreen && _cursor.visible;
	bool drawCursor = cursorDrawEnabled
		&& (cursorPositionChanged || cursorSurfaceChanged || cursorVisibilityChanged || fullRedraw);

	assert(!fullRedraw || oldCursorRect.isEmpty());

	bool restoreCursor = !oldCursorRect.isEmpty()
		&& (cursorPositionChanged || cursorSurfaceChanged || (cursorVisibilityChanged && !_cursor.visible));

	//lockSuperBlitter();

	for (auto it = dirtyRects.begin(); it != dirtyRects.end(); ++it) {
		if (cursorDrawEnabled && !drawCursor)
			drawCursor = it->intersects(_cursor.dstRect);

		if (!directRendering) {
			copyRectToSurface(*dstSurface, srcSurface, it->left, it->top, *it);
			updated = true;
		}
	}

	if (restoreCursor) {
		//debug("Restore cursor: %d %d %d %d", oldCursorRect.left, oldCursorRect.top, oldCursorRect.width(), oldCursorRect.height());
		if (!directRendering) {
			copyRectToSurface(
				*dstSurface, srcSurface,
				oldCursorRect.left, oldCursorRect.top,
				oldCursorRect);
		} else {
			_workScreen->restoreBackground(oldCursorRect);
		}
		oldCursorRect = Common::Rect();
		updated = true;
	}

	//unlockSuperBlitter();

	if (drawCursor) {
		//debug("Redraw cursor: %d %d %d %d", _cursor.dstRect.left, _cursor.dstRect.top, _cursor.dstRect.width(), _cursor.dstRect.height());

		if (cursorSurfaceChanged || _cursor.isClipped()) {
			if (dstSurface->format.isCLUT8())
				_cursor.convertTo<true>(dstSurface->format);
			else
				_cursor.convertTo<false>(dstSurface->format);
			{
				// copy in-place (will do nothing on regular Surface::copyRectToSurface)
				Graphics::Surface surf;
				surf.init(
					_cursor.surface.w,
					_cursor.surface.h,
					_cursor.surface.pitch,
					_cursor.surface.getPixels(),
					_cursor.surface.format);
				copyRectToSurface(
					surf, _cursor.surface,
					0, 0,
					Common::Rect(_cursor.surface.w, _cursor.surface.h));
			}
		}

		if (directRendering)
			_workScreen->storeBackground(_cursor.dstRect);

		dstSurface->copyRectToSurfaceWithKey(
			_cursor.surface,
			_cursor.dstRect.left,
			_cursor.dstRect.top,
			Common::Rect(0, _cursor.srcRect.top, _cursor.surface.w, _cursor.srcRect.bottom),
			0
		);

		cursorPositionChanged = cursorSurfaceChanged = false;
		oldCursorRect = _cursor.dstRect;

		updated = true;
	}

	cursorVisibilityChanged = false;

	return updated;
}

void AtariGraphicsManager::copyRectToScreenInternal(const void *buf, int pitch, int x, int y, int w, int h,
													const Graphics::PixelFormat &format, bool directRendering, bool tripleBuffer) {
	Graphics::Surface &dstSurface = *lockScreen();
	const Common::Rect rect(x, y, x + w, y + h);
	_workScreen->addDirtyRect(dstSurface, rect, directRendering);
	dstSurface.copyRectToSurface(buf, pitch, x, y, w, h);
}

bool AtariGraphicsManager::isOverlayDirectRendering() const {
	// overlay is direct rendered if in the launcher or if game is directly rendered
	// (on SuperVidel we always want to use shading/transparency but its direct rendering is fine and supported)
	/*return !hasSuperVidel()*/
	return false
#ifndef DISABLE_FANCY_THEMES
		   && (ConfMan.getActiveDomain() == nullptr || _currentState.mode == GraphicsMode::DirectRendering)
#endif
		;
}

AtariGraphicsManager::Screen::Screen(AtariGraphicsManager *manager, int width, int height, const Graphics::PixelFormat &format, const Palette *palette_)
	: _manager(manager)
	, palette(palette_) {
	surf.init(width, height, width, Physbase(), format);
	memset(surf.getPixels(), 0, surf.h * surf.pitch);
	_offsettedSurf = surf;
	return;
}

AtariGraphicsManager::Screen::~Screen() {
}

void AtariGraphicsManager::Screen::reset(int width, int height) {
	cursorPositionChanged = true;
	cursorSurfaceChanged = true;
	cursorVisibilityChanged = false;
	clearDirtyRects();
	oldCursorRect = Common::Rect();
	rez = mode = -1;

	_offsettedSurf.fillRect(Common::Rect(_offsettedSurf.w, _offsettedSurf.h), 0);

	if (1 && width <= 320 && height <= 200) {
		surf.w = 320;
		surf.h = 200;
		mode = TV | BPS8C | COL40 | VERTFLAG;
	} else {
		surf.w = 640;
		surf.h = 480;
		mode = VGA | BPS8C | COL80;
	}

#if 0
	// no shaking for CTPCI (yet, in the future perhaps using the blit functions)
	if (!_manager->_ctpci) {
		surf.w += 2 * MAX_HZ_SHAKE;
		surf.h += 2 * MAX_V_SHAKE;
	}
#endif	
	surf.pitch = surf.w;
	_offsettedSurf.init(
		width, height, surf.pitch,
		surf.getBasePtr((surf.w - width) / 2, (surf.h - height) / 2),
		surf.format);
}

void AtariGraphicsManager::Screen::addDirtyRect(const Graphics::Surface &srcSurface, const Common::Rect &rect, bool directRendering) {
	if (fullRedraw)
		return;

	if ((rect.width() == srcSurface.w && rect.height() == srcSurface.h) || dirtyRects.size() == 128) {
		//debug("addDirtyRect[%d]: purge %d x %d", (int)dirtyRects.size(), srcSurface.w, srcSurface.h);
		dirtyRects.clear();
		dirtyRects.emplace(srcSurface.w, srcSurface.h);
		oldCursorRect = Common::Rect();
		fullRedraw = true;
		return;
	}

	dirtyRects.insert(rect);

	if (!oldCursorRect.isEmpty()) {
		if (rect.contains(oldCursorRect)) {
			oldCursorRect = Common::Rect();
		} else if (rect.intersects(oldCursorRect)) {
			if (!directRendering) {
				_manager->copyRectToSurface(
					*offsettedSurf, srcSurface,
					oldCursorRect.left, oldCursorRect.top,
					oldCursorRect);
			} else {
				restoreBackground(oldCursorRect);
			}

			oldCursorRect = Common::Rect();
		}
	}
}

void AtariGraphicsManager::Screen::storeBackground(const Common::Rect &rect) {

	if (_cursorBackgroundSurf.w != rect.width()
		|| _cursorBackgroundSurf.h != rect.height()
		|| _cursorBackgroundSurf.format != offsettedSurf->format) {
		_cursorBackgroundSurf.create(rect.width(), rect.height(), offsettedSurf->format);
		_cursorBackgroundSurf.pitch = _cursorBackgroundSurf.pitch;
	}

	Graphics::copyBlit(
		(byte *)_cursorBackgroundSurf.getPixels(),
		(const byte *)offsettedSurf->getPixels() + rect.top * offsettedSurf->pitch + rect.left,
		_cursorBackgroundSurf.pitch, offsettedSurf->pitch,
		rect.width(), rect.height(),
		offsettedSurf->format.bytesPerPixel);
}

void AtariGraphicsManager::Screen::restoreBackground(const Common::Rect &rect) {
	Graphics::copyBlit(
		(byte *)offsettedSurf->getPixels() + rect.top * offsettedSurf->pitch + rect.left,
		(const byte *)_cursorBackgroundSurf.getPixels(),
		offsettedSurf->pitch, _cursorBackgroundSurf.pitch,
		rect.width(), rect.height(),
		offsettedSurf->format.bytesPerPixel);
}


void AtariGraphicsManager::Cursor::update(const Graphics::Surface &screen, bool isModified) {
	if (!_buf) {
		outOfScreen = true;
		return;
	}

	if (!visible || !isModified)
		return;

	srcRect = Common::Rect(_width, _height);

	dstRect = Common::Rect(
		_x - _hotspotX,	// left
		_y - _hotspotY,	// top
		_x - _hotspotX + _width,	// right
		_y - _hotspotY + _height);	// bottom

	outOfScreen = !screen.clip(srcRect, dstRect);

	assert(srcRect.width() == dstRect.width());
	assert(srcRect.height() == dstRect.height());
}

void AtariGraphicsManager::Cursor::updatePosition(int deltaX, int deltaY, const Graphics::Surface &screen) {
	_x += deltaX;
	_y += deltaY;

	if (_x < 0)
		_x = 0;
	else if (_x >= screen.w)
		_x = screen.w - 1;

	if (_y < 0)
		_y = 0;
	else if (_y >= screen.h)
		_y = screen.h - 1;
}

void AtariGraphicsManager::Cursor::setSurface(const void *buf, int w, int h, int hotspotX, int hotspotY, uint32 keycolor) {
	if (w == 0 || h == 0 || buf == nullptr) {
		_buf = nullptr;
		return;
	}

	_buf = (const byte *)buf;
	_width = w;
	_height = h;
	_hotspotX = hotspotX;
	_hotspotY = hotspotY;
	_keycolor = keycolor;
}

template <bool isClut8>	// hopefully compiler optimizes all the branching out
void AtariGraphicsManager::Cursor::convertTo(const Graphics::PixelFormat &format) {
	const int cursorWidth = (srcRect.width() + 15) & (-16);
	const int cursorHeight = _height;

	if (surface.w != cursorWidth || surface.h != cursorHeight || surface.format != format) {
		if (!isClut8 && surface.format != format) {
			_rShift = format.rLoss - format.rShift;
			_gShift = format.gLoss - format.gShift;
			_bShift = format.bLoss - format.bShift;

			_rMask = format.rMax() << format.rShift;
			_gMask = format.gMax() << format.gShift;
			_bMask = format.bMax() << format.bShift;
		}

		surface.create(cursorWidth, cursorHeight, format);
		surfaceMask.create(surface.w / 8, surface.h, format);	// 1 bpl
	}

	const int srcRectWidth = srcRect.width();

	const byte *src = _buf + srcRect.left;
	byte *dst = (byte *)surface.getPixels();
	uint16 *dstMask = (uint16 *)surfaceMask.getPixels();
	const int srcPadding = _width - srcRectWidth;
	const int dstPadding = surface.w - srcRectWidth;

	for (int j = 0; j < cursorHeight; ++j) {
		for (int i = 0; i < srcRectWidth; ++i) {
			const uint32 color = *src++;
			const uint16 bit = 1 << (15 - (i % 16));

			if (color != _keycolor) {
				if (!isClut8) {
					// Convert CLUT8 to RGB332/RGB121 palette
					*dst++ = ((palette[color*3 + 0] >> _rShift) & _rMask)
						   | ((palette[color*3 + 1] >> _gShift) & _gMask)
						   | ((palette[color*3 + 2] >> _bShift) & _bMask);
				} else {
					*dst++ = color;
				}

				// clear bit
				*dstMask &= ~bit;
			} else {
				*dst++ = 0x00;

				// set bit
				*dstMask |= bit;
			}

			if (bit == 0x0001)
				dstMask++;
		}

		src += srcPadding;

		if (dstPadding) {
			memset(dst, 0x00, dstPadding);
			dst += dstPadding;

			*dstMask |= ((1 << dstPadding) - 1);
			dstMask++;
		}
	}
}





namespace Graphics {

void Surface::create(int16 width, int16 height, const PixelFormat &f) {
	assert(width >= 0 && height >= 0);
	free();

	w = width;
	h = height;
	format = f;
	pitch = w * format.bytesPerPixel;

	if (width && height) {
#if 0		
		if (s_currentPool) {
			pixels = s_currentPool->calloc(height * pitch, format.bytesPerPixel);
			if (!pixels)
				error("Not enough VRAM to allocate a surface");
		}
		else
#endif		
		{
			pixels = ::calloc(height * pitch, format.bytesPerPixel);
			if (!pixels)
				error("Not enough RAM to allocate a surface");
		}

		//assert(((uintptr)pixels & (MALLOC_ALIGNMENT - 1)) == 0);
	}
}

void Surface::free() {
	if (pixels)
		::free(pixels);

	pixels = nullptr;
	w = h = pitch = 0;
	format = PixelFormat();
}
};



