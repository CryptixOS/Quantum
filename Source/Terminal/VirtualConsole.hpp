/*
 * Created by v1tr10l7 on 23.05.2024.
 * Copyright (c) 2024-2026, Szymon Zemke <v1tr10l7@proton.me>
 *
 * SPDX-License-Identifier: GPL-3
 */
#pragma once

#include <Core/Color.hpp>
#include <Core/Prism.hpp>

#include <Prism/Containers/KeyValuePair.hpp>
#include <Prism/String/StringView.hpp>

#include <sys/ioctl.h>

namespace AnsiColor
{
    constexpr const char* FOREGROUND_BLACK   = "\u001b[30m";
    constexpr const char* FOREGROUND_RED     = "\u001b[31m";
    constexpr const char* FOREGROUND_GREEN   = "\u001b[32m";
    constexpr const char* FOREGROUND_YELLOW  = "\u001b[33m";
    constexpr const char* FOREGROUND_BLUE    = "\u001b[34m";
    constexpr const char* FOREGROUND_MAGENTA = "\u001b[35m";
    constexpr const char* FOREGROUND_CYAN    = "\u001b[36m";
    constexpr const char* FOREGROUND_WHITE   = "\u001b[37m";

    constexpr const char* BACKGROUND_BLACK   = "\u001b[40m";
    constexpr const char* BACKGROUND_RED     = "\u001b[41m";
    constexpr const char* BACKGROUND_GREEN   = "\u001b[42m";
    constexpr const char* BACKGROUND_YELLOW  = "\u001b[43m";
    constexpr const char* BACKGROUND_BLUE    = "\u001b[44m";
    constexpr const char* BACKGROUND_MAGENTA = "\u001b[45m";
    constexpr const char* BACKGROUND_CYAN    = "\u001b[46m";
    constexpr const char* BACKGROUND_WHITE   = "\u001b[47m";
}; // namespace AnsiColor

// Receives the bytes that the console sends back to the client application,
// e.g. replies to device status reports
class ConsoleClient
{
  public:
    virtual ~ConsoleClient()                  = default;
    virtual void SendToClient(StringView data) = 0;
};

// Terminal emulator state machine, extracted from the CryptixOS kernel's
// VirtualConsole, it parses the byte stream written by applications and
// forwards drawing requests to the backend implemented by subclasses
class VirtualConsole
{
  public:
    virtual ~VirtualConsole() = default;

    inline const winsize& GetSize() const { return m_Size; }
    inline bool           GetCursorKeyMode() const { return m_CursorKeyMode; }

    inline void SetClient(ConsoleClient* client) { m_Client = client; }

    // Whether the console currently owns the screen, inactive consoles keep
    // track of their contents but don't draw anything
    inline bool IsActive() const { return m_Active; }
    void        SetActive(bool active);

    virtual void Clear(u32 color = 0xffffffff, bool move = true) = 0;

    void         PutChar(u64 c);
    void         PutCharImpl(u64 c);

    isize        PrintString(StringView str);

    void         Bell();

    virtual void Refresh() = 0;
    virtual void Flush()   = 0;

  protected:
    VirtualConsole() = default;

    bool    m_Initialized        = false;
    bool    m_Active             = false;
    winsize m_Size               = {};
    usize   m_ScrollBottomMargin = 0;
    usize   m_ScrollTopMargin    = 0;

    enum class AnsiColor
    {
        eBlack         = 0x00,
        eRed           = 0x01,
        eGreen         = 0x02,
        eYellow        = 0x03,
        eBlue          = 0x04,
        eMagenta       = 0x05,
        eCyan          = 0x06,
        eWhite         = 0x07,
        eDefault       = 0x09,

        eBrightBlack   = 0x0a,
        eBrightRed     = 0x0b,
        eBrightGreen   = 0x0c,
        eBrightYellow  = 0x0d,
        eBrightBlue    = 0x0e,
        eBrightMagenta = 0x0f,
        eBrightCyan    = 0x10,
        eBrightWhite   = 0x11,
        eDefaultBright = 0x13,
    };

    virtual void RawPutChar(u8 c) = 0;
    virtual void MoveCharacter(usize newX, usize newY, usize oldX, usize oldY)
        = 0;

    virtual void                       Scroll(isize count = 1)              = 0;
    virtual void                       ScrollDown()                         = 0;
    virtual void                       ScrollUp()                           = 0;

    virtual void                       ShowCursor()                         = 0;
    virtual bool                       HideCursor()                         = 0;

    virtual KeyValuePair<usize, usize> GetCursorPos()                       = 0;
    virtual void                       SetCursorPos(usize xpos, usize ypos) = 0;

    virtual void                       SaveState()                          = 0;
    virtual void                       RestoreState()                       = 0;

    virtual void                       SwapPalette()                        = 0;

    virtual void                       SetTextForeground(AnsiColor color)   = 0;
    virtual void                       SetTextBackground(AnsiColor color)   = 0;

    virtual void                       SetTextForegroundRgb(Color color)    = 0;
    virtual void                       SetTextBackgroundRgb(Color color)    = 0;

    void                               Reset();
    virtual void                       Destroy() = 0;

  private:
    enum class State
    {
        eNormal,
        eEscape,
        eControlSequenceEntry,
        eControlSequenceParams,
        eCsiExpectParameters,
        eOsCommand,
        eOsCommandEscape,
        eDecPrivate,
        eDecParameters,
        eDecCursorMode,
        eSelectCharset,
        eIgnoreNext,
    } m_State                   = State::eNormal;

    ConsoleClient*                      m_Client = nullptr;

    constexpr static usize              MAX_ESC_PARAMETER_COUNT = 16;
    Array<u32, MAX_ESC_PARAMETER_COUNT> m_EscapeValues{};
    usize                               m_EscapeValueCount = 0;
    u8                                  m_TabSize          = 8;

    bool                                m_DecPrivate       = false;
    bool                                m_CursorKeyMode    = false;
    bool                                m_CursorVisible    = true;

    // Character sets
    usize                               m_CharsetSlot      = 0;
    usize                               m_ActiveCharset    = 0;
    Array<bool, 2>                      m_LineDrawing      = {false, false};

    // UTF-8 decoding
    u32                                 m_CodePoint        = 0;
    usize                               m_PendingBytes     = 0;

    // Text attributes, kept here, so that bold/reverse video can be applied
    // on top of the selected colors
    struct ColorSpec
    {
        enum class Kind
        {
            eDefault,
            eAnsi,
            eRgb,
        } Kind    = Kind::eDefault;
        u8    Index = 0;
        Color Rgb   = {};
    };
    ColorSpec    m_Foreground;
    ColorSpec    m_Background;
    bool         m_Bold    = false;
    bool         m_Reverse = false;

    void         ApplyAttributes();
    void         ResetAttributes();

    void         OnEscapeChar(char c);
    void         OnCsi(char c);
    bool         DecSpecialPrint(u8 c);
    void         PrintCodePoint(u32 codePoint);

    void         Respond(StringView data);
    u32          Param(usize index, u32 defaultValue = 1) const;
    void         LineFeed();
    void         ReverseLineFeed();

    // ECMA-48 CSI sequences
    void         ICH(u64 count = 1);
    void         CUU(u64 count = 1);
    void         CUD(u64 count = 1);
    void         CUF(u64 count = 1);
    void         CUB(u64 count = 1);
    void         CNL(u64 count = 1);
    void         CPL(u64 count = 1);
    void         CHA(u64 column = 1);
    void         CUP();
    void         ED(u64 parameter);
    void         EL(u64 parameter);
    void         IL(u64 parameter);
    void         DL(u64 parameter);
    void         DCH(u64 parameter);
    void         ECH(u64 parameter);
    void         HPR(u64 parameter);
    void         DA(u64 parameter);
    void         VPA(u64 row);
    void         VPR(u64 parameter);
    void         TBC(u64 parameter);
    void         SM(bool set);
    void         SGR();
    void         DSR(u64 parameter);
    void         DECSTBM();
};
