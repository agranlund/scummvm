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

#ifndef BACKENDS_GRAPHICS_ATARI_NOVA_H
#define BACKENDS_GRAPHICS_ATARI_NOVA_H

#include "backends/graphics/graphics.h"
#include "backends/platform/atari/nova.h"
#include "common/events.h"

#include <mint/osbind.h>
#include <mint/ostruct.h>
#include <unordered_set>

#include "common/rect.h"
#include "graphics/surface.h"

///////////////////////////////////////////////////////////////////////////////

constexpr Graphics::PixelFormat PIXELFORMAT_CLUT8  = Graphics::PixelFormat::createFormatCLUT8();
constexpr Graphics::PixelFormat PIXELFORMAT_RGB332 = Graphics::PixelFormat(1, 3, 3, 2, 0, 5, 2, 0, 0);

///////////////////////////////////////////////////////////////////////////////

template<>
struct std::hash<Common::Rect> {
	std::size_t operator()(Common::Rect const& rect) const noexcept {
		return 31 * (31 * (31 * rect.left + rect.top) + rect.right) + rect.bottom;
	}
};

///////////////////////////////////////////////////////////////////////////////

class AtariGraphicsManager : public GraphicsManager, Common::EventObserver {
public:
	AtariGraphicsManager();
	virtual ~AtariGraphicsManager();

	bool hasFeature(OSystem::Feature f) const override;
	void setFeatureState(OSystem::Feature f, bool enable) override;
	bool getFeatureState(OSystem::Feature f) const override;

	const OSystem::GraphicsMode *getSupportedGraphicsModes() const override {
		static const OSystem::GraphicsMode graphicsModes[] = {
			{ "single", "Single buffering", (int)GraphicsMode::SingleBuffering },
			{ nullptr, nullptr, 0 }
		};
		return graphicsModes;
	}
	int getDefaultGraphicsMode() const override { return (int)GraphicsMode::SingleBuffering; }
	bool setGraphicsMode(int mode, uint flags = OSystem::kGfxModeNoFlags) override;
	int getGraphicsMode() const override { return (int)_currentState.mode; }

	void initSize(uint width, uint height, const Graphics::PixelFormat *format = NULL) override;

	int getScreenChangeID() const override { return 0; }

	void beginGFXTransaction() override;
	OSystem::TransactionError endGFXTransaction() override;

	int16 getHeight() const override { return _currentState.height; }
	int16 getWidth() const override { return _currentState.width; }
	void setPalette(const byte *colors, uint start, uint num) override;
	void grabPalette(byte *colors, uint start, uint num) const override;
	void copyRectToScreen(const void *buf, int pitch, int x, int y, int w, int h) override;
	Graphics::Surface *lockScreen() override;
	void unlockScreen() override;
	void fillScreen(uint32 col) override;
	void fillScreen(const Common::Rect &r, uint32 col) override;
	void updateScreen() override;
	void setShakePos(int shakeXOffset, int shakeYOffset) override;
	void setFocusRectangle(const Common::Rect& rect) override {}
	void clearFocusRectangle() override {}

	int16 getOverlayWidth() const override { return 320; /*640;*/ }
	int16 getOverlayHeight() const override { return 200; /*480;*/ }
	Graphics::PixelFormat getOverlayFormat() const override { return PIXELFORMAT_RGB332; }

	void showOverlay(bool inGUI) override;
	void hideOverlay() override;
	bool isOverlayVisible() const override { return _overlayVisible; }
	void clearOverlay() override;
	void grabOverlay(Graphics::Surface &surface) const override;
	void copyRectToOverlay(const void *buf, int pitch, int x, int y, int w, int h) override;

	bool showMouse(bool visible) override;
	void warpMouse(int x, int y) override;
	void setMouseCursor(const void *buf, uint w, uint h, int hotspotX, int hotspotY, uint32 keycolor,
						bool dontScale = false, const Graphics::PixelFormat *format = NULL, const byte *mask = NULL) override;
	void setCursorPalette(const byte *colors, uint start, uint num) override;

	Common::Point getMousePosition() const { return _cursor.getPosition(); }
	void updateMousePosition(int deltaX, int deltaY);

	bool notifyEvent(const Common::Event &event) override;
	Common::Keymap *getKeymap() const;

protected:
	typedef void* (*AtariMemAlloc)(size_t bytes);
	typedef void (*AtariMemFree)(void *ptr);

	void allocateSurfaces();
	void freeSurfaces();

	enum class GraphicsMode : int {
		SingleBuffering = 1,
	};

	struct GraphicsState {
		GraphicsState(GraphicsMode mode_)
			: mode(mode_)
			, width(0)
			, height(0) {
		}

		GraphicsMode mode;
		int width;
		int height;
		Graphics::PixelFormat format;
	};
	GraphicsState _pendingState{ (GraphicsMode)getDefaultGraphicsMode() };

private:

	using DirtyRects = std::unordered_set<Common::Rect>;

	enum CustomEventAction {
		kActionToggleAspectRatioCorrection = 100,
	};

	enum SteTtRezValue {
		kRezValueSTLow  = 0,	// 320x200@4bpp, ST palette
		kRezValueSTMid  = 1,	// 640x200@2bpp, ST palette
		kRezValueSTHigh = 2,	// 640x400@1bpp, ST palette
		kRezValueTTLow  = 7,	// 320x480@8bpp, TT palette
		kRezValueTTMid  = 4,	// 640x480@4bpp, TT palette
		kRezValueTTHigh = 6		// 1280x960@1bpp, TT palette
	};

	int16 getMaximumScreenWidth() const { return 320; /*640;*/ }
	int16 getMaximumScreenHeight() const { return 200; /*480;*/ }

	bool updateScreenInternal(const Graphics::Surface &srcSurface);

	void copyRectToScreenInternal(const void *buf, int pitch, int x, int y, int w, int h, const Graphics::PixelFormat &format);

	virtual void copyRectToSurface(Graphics::Surface &dstSurface, const Graphics::Surface &srcSurface, int destX, int destY, const Common::Rect &subRect) const {
		dstSurface.copyRectToSurface(srcSurface, destX, destY, subRect);
	}

	void cursorPositionChanged() {
		_screen[_overlayVisible ? OVERLAY_BUFFER : FRONT_BUFFER]->cursorPositionChanged = true;
	}
	
	void cursorSurfaceChanged() {
		_screen[_overlayVisible ? OVERLAY_BUFFER : FRONT_BUFFER]->cursorSurfaceChanged = true;
	}

	void cursorVisibilityChanged() {
		_screen[_overlayVisible ? OVERLAY_BUFFER : FRONT_BUFFER]->cursorVisibilityChanged = true;
	}

	int getOverlayPaletteSize() const { return 256; }

	bool _aspectRatioCorrection = false;
	bool _oldAspectRatioCorrection = false;
	bool _checkUnalignedPitch = false;

	GraphicsState _currentState{ (GraphicsMode)getDefaultGraphicsMode() };

	enum PendingScreenChange {
		kPendingScreenChangeNone	= 0,
		kPendingScreenChangeMode	= 1<<0,
		kPendingScreenChangeScreen	= 1<<1,
		kPendingScreenChangePalette	= 1<<2
	};
	int _pendingScreenChange = kPendingScreenChangeNone;

	enum {
		FRONT_BUFFER,
		OVERLAY_BUFFER,
		BUFFER_COUNT
	};

	class Palette {
	public:
		void clear() {
			memset(data, 0, sizeof(data));
		}

		uint16 *const tt = reinterpret_cast<uint16*>(data);
		_RGB *const falcon = reinterpret_cast<_RGB*>(data);

	private:
		byte data[256*4] = {};
	};

	struct Screen {
		Screen(AtariGraphicsManager *manager, int width, int height, const Graphics::PixelFormat &format, const Palette *palette);
		~Screen();

		void reset(int width, int height);
		void addDirtyRect(const Graphics::Surface &srcSurface, const Common::Rect &rect);
		void clearDirtyRects() {
			dirtyRects.clear();
			fullRedraw = false;
		}

		void storeBackground(const Common::Rect &rect);
		void restoreBackground(const Common::Rect &rect);

		Graphics::Surface surf;
		const Palette *palette;
		bool cursorPositionChanged = true;
		bool cursorSurfaceChanged = true;
		bool cursorVisibilityChanged = false;
		DirtyRects dirtyRects;
		bool fullRedraw = false;
		Common::Rect oldCursorRect;
		int rez = -1;
		int mode = -1;
		Graphics::Surface *const offsettedSurf = &_offsettedSurf;

		int oldScreenSurfaceWidth = -1;
		int oldScreenSurfaceHeight = -1;
		int oldScreenSurfacePitch = -1;
		int oldOffsettedSurfaceWidth = -1;
		int oldOffsettedSurfaceHeight = -1;

	private:
		const AtariGraphicsManager *_manager;
		Graphics::Surface _offsettedSurf;
		Graphics::Surface _cursorBackgroundSurf;
	};

	Screen *_screen[BUFFER_COUNT] = {};
	Screen *_workScreen = nullptr;
	Screen *_oldWorkScreen = nullptr;	// used in hideOverlay()
	Graphics::Surface _chunkySurface;
	Graphics::Surface _overlaySurface;
	bool _overlayVisible = false;

	struct Cursor {
		void update(const Graphics::Surface &screen, bool isModified);

		bool visible = false;

		// position
		Common::Point getPosition() const { return Common::Point(_x, _y); }
		void setPosition(int x, int y) {
			_x = x;
			_y = y;
		}
		void updatePosition(int deltaX, int deltaY, const Graphics::Surface &screen);
		void swap() {
			const int tmpX = _oldX;
			const int tmpY = _oldY;

			_oldX = _x;
			_oldY = _y;

			_x = tmpX;
			_y = tmpY;
		}

		// surface
		void setSurface(const void *buf, int w, int h, int hotspotX, int hotspotY, uint32 keycolor);
		template <bool isClut8> void convertTo(const Graphics::PixelFormat &format);

		Graphics::Surface surface;
		Graphics::Surface surfaceMask;

		// rects (valid only if !outOfScreen)
		bool isClipped() const { return outOfScreen ? false : _width != srcRect.width(); }
		bool outOfScreen = true;
		Common::Rect srcRect;
		Common::Rect dstRect;

		// palette (only used for the overlay)
		byte palette[256*3] = {};

	private:
		int _x = -1, _y = -1;
		int _oldX = -1, _oldY = -1;

		// related to 'surface'
		const byte *_buf = nullptr;
		int _width;
		int _height;
		int _hotspotX;
		int _hotspotY;
		uint32 _keycolor;

		int _rShift, _gShift, _bShift;
		int _rMask, _gMask, _bMask;
	};
	Cursor _cursor;

	Palette _palette;
	Palette _overlayPalette;
};

#endif
