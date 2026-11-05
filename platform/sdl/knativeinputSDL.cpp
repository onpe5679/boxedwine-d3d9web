/*
 *  Copyright (C) 2012-2025  The BoxedWine Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "boxedwine.h"
extern bool g_boxedwineForceControlModifier;
#include <SDL.h>
#include "../../source/x11/x11.h"
#include "sdlcallback.h"
#include "devinput.h"
#include "knativeinputSDL.h"
#include "knativesystem.h"
#include "kdspaudio.h"

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#define BOXEDWINE_TEXT_INPUT_EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define BOXEDWINE_TEXT_INPUT_EXPORT
#endif

U32 sdlCustomEvent;

KNativeInputSDL::KNativeInputSDL(U32 cx, U32 cy, int scaleX, int scaleY) {
    if (!sdlCustomEvent) {
        sdlCustomEvent = SDL_RegisterEvents(1);
    }
    
    this->width = cx;
    this->height = cy;
    this->scaleX = scaleX;
    this->scaleY = scaleY;
    this->scaleXOffset = 0;
    this->scaleYOffset = 0;    
	SDL_StartTextInput();
}

extern "C" BOXEDWINE_TEXT_INPUT_EXPORT void boxedwine_inject_text_utf8(const char* utf8) {
	if (!utf8 || !*utf8) {
		return;
	}
	// Routing this through SDL_PushEvent(SDL_TEXTINPUT) did not work: measured
	// 2026-07-30, queueTextInput never ran (queued=0) while XLookupString was
	// reaching us normally (lookup=2, badType=0).  The pushed event simply never
	// came back out of the pump, so drive the X11 text channel directly and skip
	// the round trip entirely.  Doc 542.
	XServer* server = XServer::getServer(true);
	if (!server) {
		return;
	}
#ifdef BOXEDWINE_IME_RUNTIME_DEBUG
	server->imeDebugAdd(IME_DEBUG_INJECT_CALLS);
	server->imeDebugAdd(IME_DEBUG_INJECT_UTF8_BYTES, (U32)strlen(utf8));
#endif
	U32 characters = XKeyboard::queueTextInput(utf8);
#ifdef BOXEDWINE_IME_RUNTIME_DEBUG
	server->imeDebugAdd(IME_DEBUG_INJECT_SCALARS, characters);
#endif
	for (U32 i = 0; i < characters; i++) {
		server->key(XKeyboard::textInputKeyCode(), true);
		server->key(XKeyboard::textInputKeyCode(), false);
	}
}

// Doc 542: read the text-injection counters from JS so the loss point can be
// measured instead of inferred.  0 queued, 1 lookups, 2 bad-type, 3 empty,
// 4 served, 5 repeat, 6 queue depth.
extern "C" BOXEDWINE_TEXT_INPUT_EXPORT unsigned int boxedwine_ime_stat(unsigned int index) {
	return XKeyboard::imeStat(index);
}

// Browser touch must use BoxedWine's current logical screen dimensions, not
// SDL's Emscripten touch-to-mouse synthesis.  On mobile the latter can retain
// the initial 800x600 SDL window after the guest switches to 1024x768; the
// result is the characteristic 0.78125 coordinate range where the right and
// bottom 21.875% are unreachable.  These exports contain no static storage so
// the pinned JIT relocation-map ABI remains unchanged.
extern "C" BOXEDWINE_TEXT_INPUT_EXPORT unsigned int boxedwine_pointer_stat(
	unsigned int index) {
	KNativeScreenPtr screen = KNativeSystem::getScreen();
	if (!screen) return 0;
	KNativeInputPtr input = KNativeSystem::getCurrentInput();
	if (!input) return 0;
	if (index == 0) return 1; // ABI version
	if (index == 1) return input->screenWidth();
	if (index == 2) return input->screenHeight();
	return 0;
}

// action: 0=move, 1=primary down, 2=primary up.  Coordinates are already in
// the logical BoxedWine/XServer screen basis returned above.  Do not call
// KNativeInputSDL::mouseMove/mouseButton here: those methods accept SDL-window
// coordinates and apply xFromScreen/yFromScreen.  Use their logical-basis
// counterparts so the existing recorder, audio-unlock and button mapping paths
// are shared without shrinking the reachable range toward the top-left.
extern "C" BOXEDWINE_TEXT_INPUT_EXPORT unsigned int boxedwine_touch_pointer(
	unsigned int action, int x, int y) {
	KNativeScreenPtr screen = KNativeSystem::getScreen();
	if (!screen || action > 2) return 0;
	KNativeInputPtr input = KNativeSystem::getCurrentInput();
	if (!input) return 0;
	const int width = (int)input->screenWidth();
	const int height = (int)input->screenHeight();
	if (width <= 0 || height <= 0) return 0;
	if (x < 0) x = 0;
	if (y < 0) y = 0;
	if (x >= width) x = width - 1;
	if (y >= height) y = height - 1;
	KNativeInputSDLPtr sdlInput = std::static_pointer_cast<KNativeInputSDL>(input);
	if (!sdlInput->mouseMoveLogical(x, y, false)) return 0;
	if (action != 0) {
		if (!sdlInput->mouseButtonLogical(action == 1, 0, x, y)) return 0;
	}
	return 1;
}

// Queue one browser IME state transition.  Unlike boxedwine_inject_text_utf8,
// this is not a committed character stream.  Guest libX11 consumes the private
// action returned by XmbLookupString and invokes Wine's registered preedit
// callbacks; only action 3 returns a committed XLookupChars result.
extern "C" BOXEDWINE_TEXT_INPUT_EXPORT unsigned int boxedwine_ime_preedit_utf8(
	unsigned int action, const char* utf8) {
	XServer* server = XServer::getServer(true);
	if (!server || action < 1 || action > 4) return 0;
	if (!XKeyboard::queuePreeditInput(action, utf8 ? utf8 : "")) return 0;
	server->key(XKeyboard::textInputKeyCode(), true);
	server->key(XKeyboard::textInputKeyCode(), false);
	return 1;
}

// Replace the in-control preview of a browser IME composition atomically.
// SDL_SendKeyboardKey cannot be used here: it queues Backspace for a later
// pump while boxedwine_inject_text_utf8 drives X11 immediately, which can
// insert the new syllable before deleting the old one.  Drive both halves
// through the same XServer in strict order instead.  0x16 is BoxedWine's X11
// keycode for XK_BackSpace (xkeyboard.cpp).
extern "C" BOXEDWINE_TEXT_INPUT_EXPORT unsigned int boxedwine_replace_preedit_utf8(
	const char* utf8, unsigned int eraseCharacters) {
	XServer* server = XServer::getServer(true);
	if (!server) {
		return 0;
	}
	const U32 backspaceKeyCode = 0x16;
	for (U32 i = 0; i < eraseCharacters; i++) {
		server->key(backspaceKeyCode, true);
		server->key(backspaceKeyCode, false);
	}
	if (!utf8 || !*utf8) {
		return 0;
	}
#ifdef BOXEDWINE_IME_RUNTIME_DEBUG
	server->imeDebugAdd(IME_DEBUG_INJECT_CALLS);
	server->imeDebugAdd(IME_DEBUG_INJECT_UTF8_BYTES, (U32)strlen(utf8));
#endif
	U32 characters = XKeyboard::queueTextInput(utf8);
#ifdef BOXEDWINE_IME_RUNTIME_DEBUG
	server->imeDebugAdd(IME_DEBUG_INJECT_SCALARS, characters);
#endif
	for (U32 i = 0; i < characters; i++) {
		server->key(XKeyboard::textInputKeyCode(), true);
		server->key(XKeyboard::textInputKeyCode(), false);
	}
	return characters;
}

#ifdef BOXEDWINE_IME_RUNTIME_DEBUG
// SDL's event-layer keyboard injector is intentionally not part of the public
// SDL.h API, but it is linked by the Emscripten SDL2 port.  Keep this debug-only
// declaration local so production runtimes never acquire the dependency.
extern "C" int SDL_SendKeyboardKey(Uint8 state, SDL_Scancode scancode);

extern "C" BOXEDWINE_TEXT_INPUT_EXPORT unsigned int boxedwine_ime_debug_abi_version() {
	return 1;
}

extern "C" BOXEDWINE_TEXT_INPUT_EXPORT unsigned int boxedwine_ime_debug_word_count() {
	return BOXEDWINE_IME_DEBUG_WORD_COUNT;
}

extern "C" BOXEDWINE_TEXT_INPUT_EXPORT unsigned int boxedwine_ime_debug_stat(unsigned int index) {
	XServer* server = XServer::getServer(true);
	return server ? server->imeDebugGet(index) : 0;
}

extern "C" BOXEDWINE_TEXT_INPUT_EXPORT unsigned int boxedwine_ime_debug_copy_snapshot(
	unsigned int* output, unsigned int wordCount) {
	XServer* server = XServer::getServer(true);
	return server ? server->imeDebugCopy(output, wordCount) : 0;
}

extern "C" BOXEDWINE_TEXT_INPUT_EXPORT void boxedwine_ime_debug_reset() {
	XServer* server = XServer::getServer(true);
	if (server) server->imeDebugReset();
}

// Runtime-probe-only path for sending exactly one guest key press/release pair.
// Return bit 0 means the press event was posted; bit 1 means release was posted.
// SDL_SCANCODE_RETURN is 40, so a successful Enter probe returns 3.
extern "C" BOXEDWINE_TEXT_INPUT_EXPORT unsigned int boxedwine_ime_debug_key(unsigned int scancode) {
	if (!scancode || scancode >= SDL_NUM_SCANCODES) {
		return 0;
	}
	unsigned int result = 0;
	if (SDL_SendKeyboardKey(SDL_PRESSED, (SDL_Scancode)scancode)) result |= 1;
	if (SDL_SendKeyboardKey(SDL_RELEASED, (SDL_Scancode)scancode)) result |= 2;
	return result;
}
#endif

// Docs 542/549: plain XLookupString interception is a dead end because Wine
// rebuilds WM_CHAR from its keycode table.  korean3 instead exposes this FIFO
// through XmbLookupString/XLookupChars.  Keep clipboard + Ctrl+V as a rollback
// fallback for older roots/runtimes that cannot create an XIC.
// Set only while the synthetic Ctrl+V is in flight; see getModifiers above.
bool g_boxedwineForceControlModifier = false;

extern "C" BOXEDWINE_TEXT_INPUT_EXPORT void boxedwine_paste_text_utf8(const char* utf8) {
	if (!utf8 || !*utf8) {
		return;
	}
	KNativeScreenPtr screen = KNativeSystem::getScreen();
	if (screen) {
		screen->clipboardSetText(utf8);
	}
	XServer* server = XServer::getServer(true);
	if (!server) {
		return;
	}
	const U32 controlKeyCode = 0x25; // XK_Control_L
	const U32 vKeyCode = 0x37;       // XK_v
	g_boxedwineForceControlModifier = true;
	server->key(controlKeyCode, true);
	server->key(vKeyCode, true);
	server->key(vKeyCode, false);
	server->key(controlKeyCode, false);
	g_boxedwineForceControlModifier = false;
}

void KNativeInputSDL::runOnUiThread(std::function<void()> callback) {
    DISPATCH_MAIN_THREAD_BLOCK_THIS_BEGIN
        callback();
    DISPATCH_MAIN_THREAD_BLOCK_END
}


void KNativeInputSDL::setScreenSize(U32 cx, U32 cy) {
    width = cx;
    height = cy;
}

U32 KNativeInputSDL::screenWidth() {
    return width;
}

U32 KNativeInputSDL::screenHeight() {
    return height;
}

bool KNativeInputSDL::mouseMove(int x, int y, bool relative) {
#if defined(__EMSCRIPTEN__) && defined(BOXEDWINE_D3D9WEB_INPUT_TRACE)
    const int rawX = x;
    const int rawY = y;
#endif
    x = xFromScreen(x);
    y = yFromScreen(y);
#if defined(__EMSCRIPTEN__) && defined(BOXEDWINE_D3D9WEB_INPUT_TRACE)
    static U32 inputTraceCount = 0;
    if (inputTraceCount < 12 && (rawX || rawY)) {
        klog_fmt("D3D9WEB_INPUT_MAP n=%u raw=%d,%d mapped=%d,%d relative=%d guest=%u,%u",
            inputTraceCount++, rawX, rawY, x, y, relative ? 1 : 0, width, height);
    }
#endif

    return mouseMoveLogical(x, y, relative);
}

bool KNativeInputSDL::mouseMoveLogical(int x, int y, bool relative) {
    XServer* server = XServer::getServer(true);

#ifdef BOXEDWINE_RECORDER
    if (Player::instance) {
        lastX = x;
        lastY = y;
    }
#endif

    if (server) {
        server->mouseMove(x, y, relative);
        return true;
    }
    return false;
}

bool KNativeInputSDL::mouseWheel(int amount, int x, int y) {
    x = xFromScreen(x);
    y = yFromScreen(y);

    checkMousePos(x, y, true);

    XServer* server = XServer::getServer(true);
    if (server) {
        U32 btn = amount > 0 ? 4 : 5;
        server->mouseButton(btn, x, y, true);
        server->mouseButton(btn, x, y, false);
        return true;
    }
    return false;
}

bool KNativeInputSDL::mouseButton(U32 down, U32 button, int x, int y) {
    x = xFromScreen(x);
    y = yFromScreen(y);

    return mouseButtonLogical(down, button, x, y);
}

bool KNativeInputSDL::mouseButtonLogical(U32 down, U32 button, int x, int y) {
    if (KSystem::enableSoundAfterMouseClick) {
        KSystem::enableSoundAfterMouseClick = false;
        KSystem::soundEnabled = true;
        KDspAudio::iterateOpenAudio([](KDspAudioPtr& audio) {
            audio->soundEnabled();
            });
    }
    checkMousePos(x, y, true);

    XServer* server = XServer::getServer(true);
    if (server) {
        U32 btn = button + 1;
        if (btn == 2) {
            btn = 3;
        } else if (btn == 3) {
            btn = 2;
        } else if (btn == 4) {
            btn = 8;
        } else if (btn == 5) {
            btn = 9;
        }
        server->mouseButton(btn, x, y, down ? true : false);
        return true;
    }
    return false;
}

bool KNativeInputSDL::getMousePos(int* x, int* y, bool allowWarp) {
#ifdef BOXEDWINE_RECORDER
    if (Player::instance) {
        *x = lastX;
        *y = lastY;
        return checkMousePos(*x, *y, false);
    }
#endif
    SDL_GetMouseState(x, y);

    *x = xFromScreen(*x);
    *y = yFromScreen(*y);

    if (XServer::getServer(true) && XServer::getServer()->fakeFullScreenWnd) {
        XServer::getServer()->fakeFullScreenWnd->screenToWindow(*x, *y);
    }
    return checkMousePos(*x, *y, false);
}

void KNativeInputSDL::setMousePos(int x, int y) {
#ifdef BOXEDWINE_RECORDER
    if (Player::instance) {
        lastX = x;
        lastY = y;
        return;
    }
#endif

    if (XServer::getServer(true) && XServer::getServer()->fakeFullScreenWnd) {
        XServer::getServer()->fakeFullScreenWnd->screenToWindow(x, y);
    }
    x = xToScreen(x);
    y = yToScreen(y);

    KNativeSystem::warpMouse(x, y);
}

int KNativeInputSDL::xToScreen(int x) {
    return x * (int)scaleX / 100 + (int)scaleXOffset;
}

int KNativeInputSDL::xFromScreen(int x) {
    return (x - (int)scaleXOffset) * 100 / (int)scaleX;
}

int KNativeInputSDL::yToScreen(int y) {
    return y * (int)scaleY / 100 + (int)scaleYOffset;
}

int KNativeInputSDL::yFromScreen(int y) {
    return (y - (int)scaleYOffset) * 100 / (int)scaleY;
}

bool KNativeInputSDL::checkMousePos(int& x, int& y, bool allowWarp) {
    bool warp = false;
    if (x < 0) {
        x = 0;
        warp = true;
    }
    if (x >= (int)width) {
        x = (int)width - 1;
        warp = true;
    }
    if (y < 0) {
        y = 0;
        warp = true;
    }
    if (y >= (int)height) {
        y = (int)height;
        warp = true;
    }
    if (allowWarp && warp) {
        int scaledX = xToScreen(x);
        int scaledY = yToScreen(y);
        KNativeSystem::getScreen()->warpMouse(scaledX, scaledY);
    }
    return allowWarp || !warp;
}

#ifndef KMOD_SCROLL
#define KMOD_SCROLL KMOD_RESERVED
#endif

U32 KNativeInputSDL::getInputModifiers() {
#ifdef BOXEDWINE_RECORDER
    if (Player::instance) {
        return Player::instance->currentInputModifiers;
    }
#endif
    int x, y;
    unsigned int result = SDL_GetMouseState(&x, &y);
    U32 modifiers = 0;
    if (result & SDL_BUTTON_LMASK) {
        modifiers |= NATIVE_LEFT_BUTTON_MASK;
    }
    if (result & SDL_BUTTON_RMASK) {
        modifiers |= NATIVE_RIGHT_BUTTON_MASK;
    }
    if (result & SDL_BUTTON_MMASK) {
        modifiers |= NATIVE_MIDDLE_BUTTON_MASK;
    }
    if (result & SDL_BUTTON_X1MASK) {
        modifiers |= NATIVE_BUTTON_4_MASK;
    }
    if (result & SDL_BUTTON_X2MASK) {
        modifiers |= NATIVE_BUTTON_5_MASK;
    }
    SDL_Keymod mods = SDL_GetModState();
    if (g_boxedwineForceControlModifier) {
        modifiers |= NATIVE_CONTROL_MASK;
    }
    if (mods & KMOD_SHIFT) {
        modifiers |= NATIVE_SHIFT_MASK;
    }
    if (mods & KMOD_CAPS) {
        modifiers |= NATIVE_CAPS_MASK;
    }
    if (mods & KMOD_CTRL) {
        modifiers |= NATIVE_CONTROL_MASK;
    }
    if (mods & KMOD_ALT) {
        modifiers |= NATIVE_CONTROL_MASK;
    }
    if (mods & KMOD_NUM) {
        modifiers |= NATIVE_NUM_MASK;
    }
    if (mods & KMOD_SCROLL) {
        modifiers |= NATIVE_SCROLL_MASK;
    }
    return modifiers;
}

bool KNativeInputSDL::key(U32 sdlScanCode, U32 key, U32 down) {
    XServer* server = XServer::getServer(true);
    if (server) {
        U32 x11Key = XKeyboard::sdl2x11(sdlScanCode);
        if (x11Key) {
            server->key(x11Key, down ? true : false);
        }
        return true;
    }
    return false;
}

bool KNativeInputSDL::waitForEvent(U32 ms) {
    SDL_Event e = { 0 };
    if (SDL_WaitEventTimeout(&e, ms) == 1) {
#ifdef BOXEDWINE_MULTI_THREADED
        if (e.type == sdlCustomEvent) {
            SdlCallback* callback = (SdlCallback*)e.user.data1;
            if (callback->pfn) {
                callback->result = (U32)callback->pfn();
            }
            BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(callback->cond);
            BOXEDWINE_CONDITION_SIGNAL(callback->cond);
            return true;
        }
#endif
        handlSdlEvent(&e);
        return true;
    }
    return false;
}

bool KNativeInputSDL::processEvents() {
    SDL_Event e = {};

    while (SDL_PollEvent(&e) == 1) {
#ifdef BOXEDWINE_MULTI_THREADED
        if (e.type == sdlCustomEvent) {
            SdlCallback* callback = (SdlCallback*)e.user.data1;
            callback->result = (U32)callback->pfn();
            BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(callback->cond);
            BOXEDWINE_CONDITION_SIGNAL(callback->cond);
        } else
#endif
            if (!handlSdlEvent(&e)) {
                return false;
            }
    }
    return true;
}

#ifndef SDLK_NUMLOCK
#define SDLK_NUMLOCK SDL_SCANCODE_NUMLOCKCLEAR
#endif
#ifndef SDLK_SCROLLOCK
#define SDLK_SCROLLOCK SDLK_SCROLLLOCK
#endif

static U32 translate(U32 key) {
    switch (key) {
    case SDLK_ESCAPE:
        return K_KEY_ESC;
    case SDLK_1:
        return K_KEY_1;
    case SDLK_2:
        return K_KEY_2;
    case SDLK_3:
        return K_KEY_3;
    case SDLK_4:
        return K_KEY_4;
    case SDLK_5:
        return K_KEY_5;
    case SDLK_6:
        return K_KEY_6;
    case SDLK_7:
        return K_KEY_7;
    case SDLK_8:
        return K_KEY_8;
    case SDLK_9:
        return K_KEY_9;
    case SDLK_0:
        return K_KEY_0;
    case SDLK_MINUS:
        return K_KEY_MINUS;
    case SDLK_EQUALS:
        return K_KEY_EQUAL;
    case SDLK_BACKSPACE:
        return K_KEY_BACKSPACE;
    case SDLK_TAB:
        return K_KEY_TAB;
    case SDLK_q:
        return K_KEY_Q;
    case SDLK_w:
        return K_KEY_W;
    case SDLK_e:
        return K_KEY_E;
    case SDLK_r:
        return K_KEY_R;
    case SDLK_t:
        return K_KEY_T;
    case SDLK_y:
        return K_KEY_Y;
    case SDLK_u:
        return K_KEY_U;
    case SDLK_i:
        return K_KEY_I;
    case SDLK_o:
        return K_KEY_O;
    case SDLK_p:
        return K_KEY_P;
    case SDLK_LEFTBRACKET:
        return K_KEY_LEFTBRACE;
    case SDLK_RIGHTBRACKET:
        return K_KEY_RIGHTBRACE;
    case SDLK_RETURN:
        return K_KEY_ENTER;
    case SDLK_LCTRL:
        return K_KEY_LEFTCTRL;
    case SDLK_RCTRL:
        return K_KEY_RIGHTCTRL;
    case SDLK_a:
        return K_KEY_A;
    case SDLK_s:
        return K_KEY_S;
    case SDLK_d:
        return K_KEY_D;
    case SDLK_f:
        return K_KEY_F;
    case SDLK_g:
        return K_KEY_G;
    case SDLK_h:
        return K_KEY_H;
    case SDLK_j:
        return K_KEY_J;
    case SDLK_k:
        return K_KEY_K;
    case SDLK_l:
        return K_KEY_L;
    case SDLK_SEMICOLON:
        return K_KEY_SEMICOLON;
    case SDLK_QUOTE:
        return K_KEY_APOSTROPHE;
    case SDLK_BACKQUOTE:
        return K_KEY_GRAVE;
    case SDLK_LSHIFT:
        return K_KEY_LEFTSHIFT;
    case SDLK_RSHIFT:
        return K_KEY_RIGHTSHIFT;
    case SDLK_BACKSLASH:
        return K_KEY_BACKSLASH;
    case SDLK_z:
        return K_KEY_Z;
    case SDLK_x:
        return K_KEY_X;
    case SDLK_c:
        return K_KEY_C;
    case SDLK_v:
        return K_KEY_V;
    case SDLK_b:
        return K_KEY_B;
    case SDLK_n:
        return K_KEY_N;
    case SDLK_m:
        return K_KEY_M;
    case SDLK_COMMA:
        return K_KEY_COMMA;
    case SDLK_PERIOD:
        return K_KEY_DOT;
    case SDLK_SLASH:
        return K_KEY_SLASH;
    case SDLK_LALT:
        return K_KEY_LEFTALT;
    case SDLK_RALT:
        return K_KEY_RIGHTALT;
    case SDLK_SPACE:
        return K_KEY_SPACE;
    case SDLK_CAPSLOCK:
        return K_KEY_CAPSLOCK;
    case SDLK_F1:
        return K_KEY_F1;
    case SDLK_F2:
        return K_KEY_F2;
    case SDLK_F3:
        return K_KEY_F3;
    case SDLK_F4:
        return K_KEY_F4;
    case SDLK_F5:
        return K_KEY_F5;
    case SDLK_F6:
        return K_KEY_F6;
    case SDLK_F7:
        return K_KEY_F7;
    case SDLK_F8:
        return K_KEY_F8;
    case SDLK_F9:
        return K_KEY_F9;
    case SDLK_F10:
        return K_KEY_F10;
    case SDLK_NUMLOCK:
        return K_KEY_NUMLOCK;
    case SDLK_SCROLLOCK:
        return K_KEY_SCROLLLOCK;
    case SDLK_F11:
        return K_KEY_F11;
    case SDLK_F12:
        return K_KEY_F12;
    case SDLK_HOME:
        return K_KEY_HOME;
    case SDLK_UP:
        return K_KEY_UP;
    case SDLK_PAGEUP:
        return K_KEY_PAGEUP;
    case SDLK_LEFT:
        return K_KEY_LEFT;
    case SDLK_RIGHT:
        return K_KEY_RIGHT;
    case SDLK_END:
        return K_KEY_END;
    case SDLK_DOWN:
        return K_KEY_DOWN;
    case SDLK_PAGEDOWN:
        return K_KEY_PAGEDOWN;
    case SDLK_INSERT:
        return K_KEY_INSERT;
    case SDLK_DELETE:
        return K_KEY_DELETE;
    case SDLK_PAUSE:
        return K_KEY_PAUSE;
    default:
        kdebug("Unhandled key: %d", key);
        return 0;
    }
}

static int getMouseButtonFromEvent(SDL_Event* e) {
    if (e->button.button == SDL_BUTTON_LEFT) {
        return 0;
    } else if (e->button.button == SDL_BUTTON_MIDDLE) {
        return 2;
    } else if (e->button.button == SDL_BUTTON_RIGHT) {
        return 1;
    }
    return 0;
}

// return true to continue processing for custom handlers
//
// should only call on main thread
void KNativeInputSDL::processCustomEvents(std::function<bool(bool isKeyDown, int key, bool isF11)> onKey, std::function<bool(bool isButtonDown, int button, int x, int y)> onMouseButton, std::function<bool(int x, int y)> onMouseMove) {
    SDL_Event e = {};

#ifdef _DEBUG
    if (!isMainthread()) {
        kpanic("KNativeInputSDL::processCustomEvents should only be called on main thread");
    }
#endif
    while (SDL_WaitEvent(&e)) {
        if (e.type == SDL_KEYUP) {
            if (!onKey(false, e.key.keysym.sym, e.key.keysym.sym == SDLK_F11)) {
                return;
            }
        } else if (e.type == SDL_MOUSEBUTTONDOWN) {
            if (!onMouseButton(true, getMouseButtonFromEvent(&e), e.motion.x, e.motion.y)) {
                return;
            }
        } else if (e.type == SDL_MOUSEBUTTONUP) {
            if (!onMouseButton(false, getMouseButtonFromEvent(&e), e.motion.x, e.motion.y)) {
                return;
            }
        } else if (e.type == SDL_MOUSEMOTION) {
            if (!onMouseMove(e.motion.x, e.motion.y)) {
                return;
            }
        }
#ifdef BOXEDWINE_MULTI_THREADED
        else if (e.type == sdlCustomEvent) {
            SdlCallback* callback = (SdlCallback*)e.user.data1;
            callback->result = (U32)callback->pfn();
            BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(callback->cond);
            BOXEDWINE_CONDITION_SIGNAL(callback->cond);
        }
#endif
    }
}

bool KNativeInputSDL::handlSdlEvent(SDL_Event* e) {
#ifdef BOXEDWINE_RECORDER
    if (Player::instance) {
        if (e->type == SDL_QUIT) {
            return false;
        }
        return true;
    }
#endif    
    if (e->type == SDL_QUIT) {
        KThread::setCurrentThread(nullptr);
        KProcessPtr p = KSystem::getProcess(10);
        if (p && !KSystem::shutingDown) {
            p->killAllThreads();
            KSystem::eraseProcess(p->id);
            return true;
        }
        return false;
    } else if (e->type == SDL_MOUSEMOTION) {
        BOXEDWINE_RECORDER_HANDLE_MOUSE_MOVE(e->motion.x, e->motion.y);
        if (!mouseMove(e->motion.x, e->motion.y, false)) {
            onMouseMove(e->motion.x, e->motion.y, false);
        }
    } else if (e->type == SDL_MOUSEBUTTONDOWN) {    
        U32 button = getMouseButtonFromEvent(e);
        BOXEDWINE_RECORDER_HANDLE_MOUSE_BUTTON_DOWN(button, e->motion.x, e->motion.y);
        if (!mouseButton(1, button, e->motion.x, e->motion.y)) {
            onMouseButtonDown(button);
        }
    } else if (e->type == SDL_MOUSEBUTTONUP) {      
        U32 button = getMouseButtonFromEvent(e);
        BOXEDWINE_RECORDER_HANDLE_MOUSE_BUTTON_UP(button, e->motion.x, e->motion.y);
        if (!mouseButton(0, button, e->motion.x, e->motion.y)) {
            onMouseButtonUp(button);
        }
    } else if (e->type == SDL_MOUSEWHEEL) {
        // Handle up/down mouse wheel movements
        int x, y;
        SDL_GetMouseState(&x, &y);
        if (!mouseWheel(e->wheel.y * 80, x, y)) {
            onMouseWheel(e->wheel.y);
        }
    } else if (e->type == SDL_TEXTINPUT) {
		// Physical ASCII continues through the existing KEYDOWN path.  Only
		// non-ASCII committed text needs the synthetic X11 text channel.
		bool hasNonAscii = false;
		for (const U8* current = (const U8*)e->text.text; *current; current++) {
			if (*current & 0x80) {
				hasNonAscii = true;
				break;
			}
		}
		XServer* server = XServer::getServer(true);
		if (hasNonAscii && server) {
			U32 characters = XKeyboard::queueTextInput(e->text.text);
			for (U32 i = 0; i < characters; i++) {
				server->key(XKeyboard::textInputKeyCode(), true);
				server->key(XKeyboard::textInputKeyCode(), false);
			}
		}
    } else if (e->type == SDL_KEYDOWN) {       
        if (!BOXEDWINE_RECORDER_HANDLE_KEY_DOWN(e->key.keysym.scancode, e->key.keysym.sym == SDLK_F11)) {
            if (e->key.keysym.sym == SDLK_SCROLLOCK) {
                KSystem::printStacks();
            } else if (!key(e->key.keysym.scancode, e->key.keysym.sym, 1)) {
                onKeyDown(translate(e->key.keysym.sym));
            }
        }
    } else if (e->type == SDL_KEYUP) {
        if (!BOXEDWINE_RECORDER_HANDLE_KEY_UP(e->key.keysym.scancode, e->key.keysym.sym == SDLK_F11)) {
            if (!key(e->key.keysym.scancode, e->key.keysym.sym, 0)) {
                onKeyUp(translate(e->key.keysym.sym));
            }
        }
    } else if (e->type == SDL_WINDOWEVENT) {
        if (e->window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
            for (auto& callback : onFocusGained) {
                callback();
            }
        } else if (e->window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
            for (auto& callback : onFocusLost) {
                callback();
            }
        }
    }
    return true;
}
