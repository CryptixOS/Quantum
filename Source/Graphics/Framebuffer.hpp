/*
 * Created by v1tr10l7 on 05.11.2026.
 * Copyright (c) 2024-2026, Szymon Zemke <v1tr10l7@proton.me>
 *
 * SPDX-License-Identifier: GPL-3
 */
#pragma once

#include <Core/Color.hpp>
#include <Core/Prism.hpp>
#include <Terminal/Clipboard.hpp>

#include <Prism/String/StringView.hpp>

#include <string>

// Keys that don't produce text on their own, and therefore have to be
// translated into escape sequences by the terminal
enum class Key
{
    eUnknown,
    eEnter,
    eBackspace,
    eTab,
    eEscape,
    eUp,
    eDown,
    eLeft,
    eRight,
    eHome,
    eEnd,
    ePageUp,
    ePageDown,
    eInsert,
    eDelete,
    eF1,
    eF2,
    eF3,
    eF4,
    eF5,
    eF6,
    eF7,
    eF8,
    eF9,
    eF10,
    eF11,
    eF12,
};

namespace KeyModifier
{
    constexpr u32 eNone    = 0;
    constexpr u32 eShift   = 1 << 0;
    constexpr u32 eAlt     = 1 << 1;
    constexpr u32 eControl = 1 << 2;
}; // namespace KeyModifier

enum class MouseButton
{
    eLeft,
    eMiddle,
    eRight,
};

class InputListener
{
  public:
    virtual ~InputListener() = default;

    // Raw bytes that should be forwarded as-is to the client
    virtual void OnText(StringView text)              = 0;
    // A non-textual key press
    virtual void OnKey(Key key, u32 modifiers)        = 0;
    // A request to switch to another virtual console
    virtual void OnSwitchConsole(usize index)         = 0;
    // The display was closed
    virtual void OnQuit()                             = 0;

    // Copy the selection to the clipboard (Ctrl+Shift+C)
    virtual void OnCopy() {}
    // Paste from the given clipboard (Ctrl+Shift+V, Shift+Insert, middle
    // click)
    virtual void OnPaste(ClipboardKind) {}

    // Mouse events, coordinates are in framebuffer pixels, @clicks counts
    // consecutive clicks, i.e. 2 for a double click
    virtual void OnMouseDown(MouseButton, usize, usize, u32) {}
    virtual void OnMouseUp(MouseButton, usize, usize) {}
    virtual void OnMouseMove(usize, usize) {}
};

// Abstraction over a linear, 32 bits per pixel framebuffer
//
// The pixel memory is always directly accessible through PutPixel, so that the
// console code can render the same way the kernel does, backends only decide
// where that memory lives, how it reaches the screen and where input comes
// from
class Framebuffer
{
  public:
    virtual ~Framebuffer() = default;

    virtual bool        Initialize()             = 0;
    virtual StringView  Name() const             = 0;

    // Makes all pixels written since the previous call visible
    virtual void        Present()                = 0;

    // File descriptor which becomes readable once there are input events to
    // process, or -1 if the backend has to be polled periodically
    virtual i32         EventFd() const { return -1; }
    // Dispatches all pending input events to @listener
    virtual void        PollEvents(InputListener& listener) = 0;

    // Clipboard access, backends without a system clipboard keep the text in
    // memory, so that copy and paste still work between the virtual consoles
    virtual void        SetClipboard(ClipboardKind kind, StringView text)
    {
        m_Clipboards[ToUnderlying(kind)].assign(text.Raw(), text.Size());
    }
    virtual std::string GetClipboard(ClipboardKind kind)
    {
        return m_Clipboards[ToUnderlying(kind)];
    }

    inline Pointer      Address() const { return m_Address; }
    inline usize        Width() const { return m_Width; }
    inline usize        Height() const { return m_Height; }
    inline usize        Pitch() const { return m_Pitch; }
    inline u16          BitsPerPixel() const { return m_BitsPerPixel; }

    inline void         PutPixel(usize x, usize y, Color color)
    {
        u32   red    = color.Red() << RedMaskShift;
        u32   green  = color.Green() << GreenMaskShift;
        u32   blue   = color.Blue() << BlueMaskShift;

        u32   rgb    = red | green | blue;
        usize offset = y * (m_Pitch / sizeof(u32)) + x;
        m_Address.As<volatile u32>()[offset] = rgb;
        m_Dirty                              = true;
    }

    u8 RedMaskSize    = 8;
    u8 RedMaskShift   = 16;
    u8 GreenMaskSize  = 8;
    u8 GreenMaskShift = 8;
    u8 BlueMaskSize   = 8;
    u8 BlueMaskShift  = 0;

  protected:
    Pointer m_Address      = nullptr;
    usize   m_Width        = 0;
    usize   m_Height       = 0;
    usize   m_Pitch        = 0;
    u16     m_BitsPerPixel = 32;

    bool        m_Dirty         = false;

  private:
    std::string m_Clipboards[2] = {};
};
