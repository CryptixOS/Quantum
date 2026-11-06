/*
 * Created by v1tr10l7 on 05.11.2026.
 * Copyright (c) 2024-2026, Szymon Zemke <v1tr10l7@proton.me>
 *
 * SPDX-License-Identifier: GPL-3
 */
#include <Core/Log.hpp>
#include <Graphics/SDLFramebuffer.hpp>

#include <Prism/Utility/Math.hpp>

#include <SDL.h>

SDLFramebuffer::SDLFramebuffer(usize width, usize height)
{
    m_Width        = width;
    m_Height       = height;
    m_Pitch        = width * sizeof(u32);
    m_BitsPerPixel = 32;

    // SDL_PIXELFORMAT_ARGB8888
    RedMaskShift   = 16;
    GreenMaskShift = 8;
    BlueMaskShift  = 0;
}
SDLFramebuffer::~SDLFramebuffer()
{
    // Debugging aid, dumps the last frame
    if (const char* path = getenv("QUANTUM_SCREENSHOT"); path && m_Pixels)
    {
        SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormatFrom(
            m_Pixels, m_Width, m_Height, 32, m_Pitch, SDL_PIXELFORMAT_ARGB8888);
        if (surface)
        {
            SDL_SaveBMP(surface, path);
            SDL_FreeSurface(surface);
        }
    }

    if (m_Texture) SDL_DestroyTexture(m_Texture);
    if (m_Renderer) SDL_DestroyRenderer(m_Renderer);
    if (m_Window) SDL_DestroyWindow(m_Window);
    delete[] m_Pixels;

    SDL_Quit();
}

bool SDLFramebuffer::Initialize()
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0)
    {
        LogError("SDL: Failed to initialize -> {}", SDL_GetError());
        return false;
    }

    m_Window = SDL_CreateWindow("Quantum Terminal", SDL_WINDOWPOS_CENTERED,
                                SDL_WINDOWPOS_CENTERED, m_Width, m_Height, 0);
    if (!m_Window)
    {
        LogError("SDL: Failed to create the window -> {}", SDL_GetError());
        return false;
    }

    m_Renderer = SDL_CreateRenderer(m_Window, -1, 0);
    if (!m_Renderer)
    {
        LogError("SDL: Failed to create the renderer -> {}", SDL_GetError());
        return false;
    }

    m_Texture = SDL_CreateTexture(m_Renderer, SDL_PIXELFORMAT_ARGB8888,
                                  SDL_TEXTUREACCESS_STREAMING, m_Width,
                                  m_Height);
    if (!m_Texture)
    {
        LogError("SDL: Failed to create the texture -> {}", SDL_GetError());
        return false;
    }

    m_Pixels = new u32[m_Width * m_Height]{};
    m_Address = m_Pixels;
    m_Dirty   = true;

    SDL_StartTextInput();
    LogInfo("SDL: Created {}x{} window", m_Width, m_Height);
    return true;
}

void SDLFramebuffer::Present()
{
    if (!m_Dirty) return;

    SDL_UpdateTexture(m_Texture, nullptr, m_Pixels, m_Pitch);
    SDL_RenderClear(m_Renderer);
    SDL_RenderCopy(m_Renderer, m_Texture, nullptr, nullptr);
    SDL_RenderPresent(m_Renderer);

    m_Dirty = false;
}

// SDL only gained access to the primary selection in 2.26
#if SDL_VERSION_ATLEAST(2, 26, 0)
    #define QUANTUM_SDL_PRIMARY_SELECTION 1
#else
    #define QUANTUM_SDL_PRIMARY_SELECTION 0
#endif

void SDLFramebuffer::SetClipboard(ClipboardKind kind, StringView text)
{
    std::string copy(text.Raw(), text.Size());

#if QUANTUM_SDL_PRIMARY_SELECTION
    i32 status = kind == ClipboardKind::ePrimary
                   ? SDL_SetPrimarySelectionText(copy.c_str())
                   : SDL_SetClipboardText(copy.c_str());
#else
    // Keep the primary selection in memory instead
    if (kind == ClipboardKind::ePrimary)
        return Framebuffer::SetClipboard(kind, text);
    i32 status = SDL_SetClipboardText(copy.c_str());
#endif

    // Keep a copy around either way, so that pasting still works when there
    // is no clipboard manager, or the request failed
    Framebuffer::SetClipboard(kind, text);
    if (status != 0)
        LogWarn("SDL: Failed to set the clipboard -> {}", SDL_GetError());
}
std::string SDLFramebuffer::GetClipboard(ClipboardKind kind)
{
#if QUANTUM_SDL_PRIMARY_SELECTION
    char* text = kind == ClipboardKind::ePrimary ? SDL_GetPrimarySelectionText()
                                                 : SDL_GetClipboardText();
#else
    if (kind == ClipboardKind::ePrimary) return Framebuffer::GetClipboard(kind);
    char* text = SDL_GetClipboardText();
#endif

    // SDL returns an empty string both on failure, and when the clipboard is
    // empty
    std::string contents = text ? text : "";
    SDL_free(text);

    if (contents.empty()) return Framebuffer::GetClipboard(kind);
    return contents;
}

static bool TranslateButton(u8 button, MouseButton& out)
{
    switch (button)
    {
        case SDL_BUTTON_LEFT: out = MouseButton::eLeft; return true;
        case SDL_BUTTON_MIDDLE: out = MouseButton::eMiddle; return true;
        case SDL_BUTTON_RIGHT: out = MouseButton::eRight; return true;

        default: break;
    }

    return false;
}

static usize ClampCoordinate(i32 value, usize limit)
{
    if (value < 0) return 0;
    return Math::Min<usize>(value, limit - 1);
}

static Key TranslateKey(SDL_Keycode keycode)
{
    switch (keycode)
    {
        case SDLK_RETURN:
        case SDLK_KP_ENTER: return Key::eEnter;
        case SDLK_BACKSPACE: return Key::eBackspace;
        case SDLK_TAB: return Key::eTab;
        case SDLK_ESCAPE: return Key::eEscape;
        case SDLK_UP: return Key::eUp;
        case SDLK_DOWN: return Key::eDown;
        case SDLK_LEFT: return Key::eLeft;
        case SDLK_RIGHT: return Key::eRight;
        case SDLK_HOME: return Key::eHome;
        case SDLK_END: return Key::eEnd;
        case SDLK_PAGEUP: return Key::ePageUp;
        case SDLK_PAGEDOWN: return Key::ePageDown;
        case SDLK_INSERT: return Key::eInsert;
        case SDLK_DELETE: return Key::eDelete;
        case SDLK_F1: return Key::eF1;
        case SDLK_F2: return Key::eF2;
        case SDLK_F3: return Key::eF3;
        case SDLK_F4: return Key::eF4;
        case SDLK_F5: return Key::eF5;
        case SDLK_F6: return Key::eF6;
        case SDLK_F7: return Key::eF7;
        case SDLK_F8: return Key::eF8;
        case SDLK_F9: return Key::eF9;
        case SDLK_F10: return Key::eF10;
        case SDLK_F11: return Key::eF11;
        case SDLK_F12: return Key::eF12;

        default: break;
    }

    return Key::eUnknown;
}

void SDLFramebuffer::PollEvents(InputListener& listener)
{
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        switch (event.type)
        {
            case SDL_QUIT: listener.OnQuit(); break;
            case SDL_WINDOWEVENT:
                // Contents of the window might have been lost
                m_Dirty = true;
                break;
            case SDL_TEXTINPUT:
            {
                StringView text(event.text.text);
                if (SDL_GetModState() & KMOD_ALT) listener.OnText("\e");
                listener.OnText(text);
                break;
            }
            case SDL_KEYDOWN:
            {
                auto keycode   = event.key.keysym.sym;
                auto modState  = event.key.keysym.mod;

                u32  modifiers = KeyModifier::eNone;
                if (modState & KMOD_SHIFT) modifiers |= KeyModifier::eShift;
                if (modState & KMOD_ALT) modifiers |= KeyModifier::eAlt;
                if (modState & KMOD_CTRL) modifiers |= KeyModifier::eControl;

                // Alt+F1..F8 switches between virtual consoles
                if ((modifiers & KeyModifier::eAlt) && keycode >= SDLK_F1
                    && keycode <= SDLK_F8)
                {
                    listener.OnSwitchConsole(keycode - SDLK_F1);
                    break;
                }

                // Clipboard shortcuts, the same as in most terminal emulators
                constexpr u32 ctrlShift
                    = KeyModifier::eControl | KeyModifier::eShift;
                if ((modifiers & ctrlShift) == ctrlShift
                    && !(modifiers & KeyModifier::eAlt))
                {
                    if (keycode == SDLK_c)
                    {
                        listener.OnCopy();
                        break;
                    }
                    if (keycode == SDLK_v)
                    {
                        listener.OnPaste(ClipboardKind::eClipboard);
                        break;
                    }
                }
                if (keycode == SDLK_INSERT && modifiers == KeyModifier::eShift)
                {
                    listener.OnPaste(ClipboardKind::ePrimary);
                    break;
                }

                // Control characters are never reported through text input
                if (modifiers & KeyModifier::eControl)
                {
                    char c = 0;
                    if (keycode >= SDLK_a && keycode <= SDLK_z)
                        c = static_cast<char>(keycode - SDLK_a + 1);
                    else if (keycode == SDLK_SPACE || keycode == SDLK_2)
                        c = 0x00;
                    else if (keycode == SDLK_LEFTBRACKET) c = 0x1b;
                    else if (keycode == SDLK_BACKSLASH) c = 0x1c;
                    else if (keycode == SDLK_RIGHTBRACKET) c = 0x1d;
                    else if (keycode == SDLK_6) c = 0x1e;
                    else if (keycode == SDLK_MINUS || keycode == SDLK_SLASH)
                        c = 0x1f;
                    else goto special;

                    if (modifiers & KeyModifier::eAlt) listener.OnText("\e");
                    listener.OnText(StringView(&c, 1));
                    break;
                }

            special:
                Key key = TranslateKey(keycode);
                if (key != Key::eUnknown) listener.OnKey(key, modifiers);
                break;
            }

            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP:
            {
                MouseButton button;
                if (!TranslateButton(event.button.button, button)) break;

                usize x = ClampCoordinate(event.button.x, m_Width);
                usize y = ClampCoordinate(event.button.y, m_Height);
                if (event.type == SDL_MOUSEBUTTONDOWN)
                    listener.OnMouseDown(button, x, y, event.button.clicks);
                else listener.OnMouseUp(button, x, y);
                break;
            }
            case SDL_MOUSEMOTION:
                listener.OnMouseMove(ClampCoordinate(event.motion.x, m_Width),
                                     ClampCoordinate(event.motion.y, m_Height));
                break;

            default: break;
        }
    }
}
