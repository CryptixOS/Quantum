/*
 * Created by v1tr10l7 on 17.11.2024.
 * Copyright (c) 2024-2026, Szymon Zemke <v1tr10l7@proton.me>
 *
 * SPDX-License-Identifier: GPL-3
 */
#include <Core/Log.hpp>
#include <Terminal/VirtualConsole.hpp>

#include <Prism/Utility/Math.hpp>

void VirtualConsole::SetActive(bool active)
{
    m_Active = active;
    if (m_Active && m_Initialized) Refresh();
}

void VirtualConsole::PutChar(u64 c)
{
    PutCharImpl(c);

    Flush();
}
void VirtualConsole::PutCharImpl(u64 c)
{
    if (!m_Initialized) return;

    switch (m_State)
    {
        case State::eNormal: break;
        case State::eEscape: OnEscapeChar(c); return;
        case State::eControlSequenceEntry:
        case State::eControlSequenceParams: OnCsi(c); return;
        // Operating system commands (e.g. window title) are terminated either
        // by BEL, or by ST (ESC \), only OSC 52 (clipboard) is supported
        case State::eOsCommand:
            if (c == '\a' || c == 0x9c)
            {
                m_State = State::eNormal;
                OnOsc();
            }
            else if (c == '\e') m_State = State::eOsCommandEscape;
            else if (m_OscCollect && m_OscBuffer.size() < MAX_OSC_LENGTH)
                m_OscBuffer += static_cast<char>(c);
            return;
        case State::eOsCommandEscape:
            if (c == '\\')
            {
                m_State = State::eNormal;
                OnOsc();
            }
            else m_State = State::eOsCommand;
            return;
        case State::eSelectCharset:
            m_LineDrawing[m_CharsetSlot] = c == '0';
            m_State                      = State::eNormal;
            return;
        case State::eIgnoreNext: m_State = State::eNormal; return;

        default: m_State = State::eNormal; break;
    }

    // UTF-8 continuation bytes
    if (m_PendingBytes > 0)
    {
        if ((c & 0xc0) == 0x80)
        {
            m_CodePoint = (m_CodePoint << 6) | (c & 0x3f);
            if (--m_PendingBytes == 0) PrintCodePoint(m_CodePoint);
            return;
        }

        // Malformed sequence
        m_PendingBytes = 0;
        RawPutChar(0xfe);
    }

    auto [x, y] = GetCursorPos();
    switch (c)
    {
        case '\0': return;
        case '\a': Bell(); return;
        case '\b':
            if (x > 0) SetCursorPos(x - 1, y);
            return;
        case '\t':
        {
            usize nextStop = (x / m_TabSize + 1) * m_TabSize;
            if (nextStop >= m_Size.ws_col) nextStop = m_Size.ws_col - 1;
            SetCursorPos(nextStop, y);
            return;
        }
        case '\n':
        case '\v':
        case '\f': LineFeed(); return;
        case '\r': SetCursorPos(0, y); return;
        // Activate the G1 character set
        case 0x0e: m_ActiveCharset = 1; return;
        // Activate the G0 character set
        case 0x0f: m_ActiveCharset = 0; return;
        case 0x18:
        case 0x1a: m_State = State::eNormal; return;
        case '\e': m_State = State::eEscape; return;
        case 0x7f: return;
        case 0x9b:
            m_EscapeValues.Fill(0);
            m_EscapeValueCount = 0;
            m_State            = State::eControlSequenceEntry;
            return;
    }

    if (c >= 0x20 && c <= 0x7e)
    {
        if (m_LineDrawing[m_ActiveCharset] && DecSpecialPrint(c)) return;
        return RawPutChar(c);
    }

    // UTF-8 lead bytes
    if (c >= 0xc0 && c < 0xf8)
    {
        if (c < 0xe0) m_PendingBytes = 1, m_CodePoint = c & 0x1f;
        else if (c < 0xf0) m_PendingBytes = 2, m_CodePoint = c & 0x0f;
        else m_PendingBytes = 3, m_CodePoint = c & 0x07;
        return;
    }

    RawPutChar(0xfe);
}

isize VirtualConsole::PrintString(StringView str)
{
    if (!m_Initialized) return 0;

    isize nwritten = 0;
    for (auto c : str) PutCharImpl(static_cast<u8>(c)), ++nwritten;
    Flush();

    return nwritten;
}

void VirtualConsole::Bell() {}

void VirtualConsole::Respond(StringView data)
{
    if (m_Client) m_Client->SendToClient(data);
}
void VirtualConsole::OnOsc()
{
    if (!m_OscCollect) return;
    m_OscCollect = false;

    std::string payload;
    payload.swap(m_OscBuffer);

    // OSC 52 ; <selections> ; <base64 data>
    if (!payload.starts_with("52;")) return;
    usize separator = payload.find(';', 3);
    if (separator == std::string::npos) return;

    StringView selections(payload.data() + 3, separator - 3);
    StringView data(payload.data() + separator + 1,
                    payload.size() - separator - 1);

    // Reading the clipboard back would let any program running inside of the
    // terminal snoop on it, so queries are ignored
    if (data == "?" || !m_Client) return;

    std::string text;
    if (!Clipboard::Base64Decode(data, text))
    {
        LogTrace("VirtualConsole: OSC 52 with malformed base64 data");
        return;
    }

    bool clipboard = selections.Empty();
    bool primary   = false;
    for (auto c : selections)
    {
        if (c == 'c') clipboard = true;
        else if (c == 'p' || c == 's') primary = true;
    }

    StringView view(text.data(), text.size());
    if (clipboard) m_Client->SetClipboard(ClipboardKind::eClipboard, view);
    if (primary) m_Client->SetClipboard(ClipboardKind::ePrimary, view);
}
u32 VirtualConsole::Param(usize index, u32 defaultValue) const
{
    if (index >= m_EscapeValueCount || m_EscapeValues[index] == 0)
        return defaultValue;
    return m_EscapeValues[index];
}

void VirtualConsole::LineFeed()
{
    auto [x, y] = GetCursorPos();
    if (y == m_ScrollBottomMargin - 1) ScrollDown();
    else if (y + 1 < m_Size.ws_row) SetCursorPos(x, y + 1);
}
void VirtualConsole::ReverseLineFeed()
{
    auto [x, y] = GetCursorPos();
    if (y == m_ScrollTopMargin) ScrollUp();
    else if (y > 0) SetCursorPos(x, y - 1);
}

void VirtualConsole::OnEscapeChar(char c)
{
    auto [x, y] = GetCursorPos();
    m_State     = State::eNormal;

    switch (c)
    {
        case '[':
            m_EscapeValues.Fill(0);
            m_EscapeValueCount = 0;
            m_DecPrivate       = false;
            m_State            = State::eControlSequenceEntry;
            return;
        // Operating system command, and device control string
        case ']':
        case 'P':
        case '_':
        case '^':
            m_OscBuffer.clear();
            m_OscCollect = c == ']';
            m_State      = State::eOsCommand;
            return;
        // Reset
        case 'c':
            Reset();
            Clear();
            break;
        // Line Feed
        case 'D': LineFeed(); break;
        // New Line
        case 'E':
            SetCursorPos(0, y);
            LineFeed();
            break;
        // Set tab stop at current column
        case 'H': break;
        // Reverse Linefeed
        case 'M': ReverseLineFeed(); break;
        // DEC private identification
        case 'Z': DA(0); break;
        // Save current state
        case '7': SaveState(); break;
        // Restore saved state
        case '8': RestoreState(); break;
        // Start sequence selecting character set
        case '%':
        case '#': m_State = State::eIgnoreNext; break;
        // Start sequence defining G0 character set
        case '(':
            m_CharsetSlot = 0;
            m_State       = State::eSelectCharset;
            break;
        // Start sequence defining G1 character set
        case ')':
            m_CharsetSlot = 1;
            m_State       = State::eSelectCharset;
            break;
        // Keypad application/numeric mode
        case '=':
        case '>': break;
        case '\\': break;

        default: break;
    }
}
void VirtualConsole::OnCsi(char c)
{
    if (c >= '0' && c <= '9')
    {
        m_State = State::eControlSequenceParams;

        if (m_EscapeValueCount == MAX_ESC_PARAMETER_COUNT) return;
        m_EscapeValues[m_EscapeValueCount] *= 10;
        m_EscapeValues[m_EscapeValueCount] += c - '0';
        return;
    }
    if (c == ';' || c == ':')
    {
        if (m_EscapeValueCount < MAX_ESC_PARAMETER_COUNT) ++m_EscapeValueCount;
        m_State = State::eControlSequenceParams;
        return;
    }
    // Private parameter prefixes
    if (c == '?' || c == '>' || c == '=' || c == '<')
    {
        m_DecPrivate = true;
        return;
    }
    // Intermediate bytes, none of the sequences using them are supported,
    // but they must not terminate the sequence
    if (c >= 0x20 && c <= 0x2f) return;
    // Embedded control characters
    if (c == '\e')
    {
        m_State = State::eEscape;
        return;
    }
    if (static_cast<u8>(c) < 0x20) return;

    if (m_State == State::eControlSequenceParams
        && m_EscapeValueCount < MAX_ESC_PARAMETER_COUNT)
        ++m_EscapeValueCount;
    m_State = State::eNormal;

    switch (c)
    {
        case '@': ICH(Param(0)); break;
        case 'A': CUU(Param(0)); break;
        case 'B': CUD(Param(0)); break;
        case 'a':
        case 'C': CUF(Param(0)); break;
        case 'D': CUB(Param(0)); break;
        case 'E': CNL(Param(0)); break;
        case 'F': CPL(Param(0)); break;
        case '`':
        case 'G': CHA(Param(0)); break;
        case 'f':
        case 'H': CUP(); break;
        case 'J': ED(Param(0, 0)); break;
        case 'K': EL(Param(0, 0)); break;
        case 'L': IL(Param(0)); break;
        case 'M': DL(Param(0)); break;
        case 'P': DCH(Param(0)); break;
        case 'S':
            for (usize i = 0; i < Param(0); i++) ScrollDown();
            break;
        case 'T':
            for (usize i = 0; i < Param(0); i++) ScrollUp();
            break;
        case 'X': ECH(Param(0)); break;
        case 'c':
            if (!m_DecPrivate) DA(Param(0, 0));
            break;
        case 'd': VPA(Param(0)); break;
        case 'e': VPR(Param(0)); break;
        case 'g': TBC(Param(0, 0)); break;
        case 'h': SM(true); break;
        case 'l': SM(false); break;
        case 'm':
            if (!m_DecPrivate) SGR();
            break;
        case 'n': DSR(Param(0, 0)); break;
        case 'r': DECSTBM(); break;
        case 's': SaveState(); break;
        case 'u': RestoreState(); break;
        // Keyboard LEDs
        case 'q': break;
        case ']':
            LogTrace(
                "VirtualConsole: Linux Console Private CSI Sequences are "
                "currently not implemented");
            break;

        default:
            LogTrace(
                "VirtualConsole: Sequence 'ESC[{:c}' is not implemented yet",
                c);
            break;
    }

    m_DecPrivate = false;
}

void VirtualConsole::Reset()
{
    m_TabSize            = 8;
    m_EscapeValueCount   = 0;
    m_ScrollTopMargin    = 0;
    m_ScrollBottomMargin = m_Size.ws_row;
    m_State              = State::eNormal;
    m_DecPrivate         = false;
    m_CursorKeyMode      = false;
    m_BracketedPaste     = false;
    m_OscCollect         = false;
    m_OscBuffer.clear();
    m_CharsetSlot        = 0;
    m_ActiveCharset      = 0;
    m_LineDrawing        = {false, false};
    m_PendingBytes       = 0;

    if (!m_CursorVisible) ShowCursor();
    m_CursorVisible = true;

    ResetAttributes();
}
bool VirtualConsole::DecSpecialPrint(u8 c)
{
    switch (c)
    {
        case '`': RawPutChar(0x04); break;
        case '0': RawPutChar(0xdb); break;
        case '-': RawPutChar(0x18); break;
        case ',': RawPutChar(0x1b); break;
        case '.': RawPutChar(0x19); break;
        case 'a': RawPutChar(0xb1); break;
        case 'f': RawPutChar(0xf8); break;
        case 'g': RawPutChar(0xf1); break;
        case 'h': RawPutChar(0xb0); break;
        case 'j': RawPutChar(0xd9); break;
        case 'k': RawPutChar(0xbf); break;
        case 'l': RawPutChar(0xda); break;
        case 'm': RawPutChar(0xc0); break;
        case 'n': RawPutChar(0xc5); break;
        case 'q': RawPutChar(0xc4); break;
        case 's': RawPutChar(0x5f); break;
        case 't': RawPutChar(0xc3); break;
        case 'u': RawPutChar(0xb4); break;
        case 'v': RawPutChar(0xc1); break;
        case 'w': RawPutChar(0xc2); break;
        case 'x': RawPutChar(0xb3); break;
        case 'y': RawPutChar(0xf3); break;
        case 'z': RawPutChar(0xf2); break;
        case '~': RawPutChar(0xfa); break;
        case '_': RawPutChar(0xff); break;
        case '+': RawPutChar(0x1a); break;
        case '{': RawPutChar(0xe3); break;
        case '}': RawPutChar(0x9c); break;

        default: return false;
    }

    return true;
}

// The builtin font uses the code page 437 layout, map the most commonly used
// non-ASCII codepoints onto it
void VirtualConsole::PrintCodePoint(u32 codePoint)
{
    struct Mapping
    {
        u32 CodePoint;
        u8  Glyph;
    };
    static constexpr Mapping s_Cp437[] = {
        {0x00a0, ' '},  {0x00a7, 0x15}, {0x00b0, 0xf8}, {0x00b1, 0xf1},
        {0x00b2, 0xfd}, {0x00b7, 0xfa}, {0x00e4, 0x84}, {0x00e9, 0x82},
        {0x00f6, 0x94}, {0x00fc, 0x81}, {0x03c0, 0xe3}, {0x2022, 0x07},
        {0x2026, 0xfa}, {0x2190, 0x1b}, {0x2191, 0x18}, {0x2192, 0x1a},
        {0x2193, 0x19}, {0x2264, 0xf3}, {0x2265, 0xf2}, {0x2500, 0xc4},
        {0x2501, 0xc4}, {0x2502, 0xb3}, {0x2503, 0xb3}, {0x250c, 0xda},
        {0x250f, 0xda}, {0x2510, 0xbf}, {0x2513, 0xbf}, {0x2514, 0xc0},
        {0x2517, 0xc0}, {0x2518, 0xd9}, {0x251b, 0xd9}, {0x251c, 0xc3},
        {0x2523, 0xc3}, {0x2524, 0xb4}, {0x252b, 0xb4}, {0x252c, 0xc2},
        {0x2533, 0xc2}, {0x2534, 0xc1}, {0x253b, 0xc1}, {0x253c, 0xc5},
        {0x254b, 0xc5}, {0x2550, 0xcd}, {0x2551, 0xba}, {0x2554, 0xc9},
        {0x2557, 0xbb}, {0x255a, 0xc8}, {0x255d, 0xbc}, {0x2560, 0xcc},
        {0x2563, 0xb9}, {0x2566, 0xcb}, {0x2569, 0xca}, {0x256c, 0xce},
        {0x256d, 0xda}, {0x256e, 0xbf}, {0x256f, 0xd9}, {0x2570, 0xc0},
        {0x2580, 0xdf}, {0x2584, 0xdc}, {0x2588, 0xdb}, {0x258c, 0xdd},
        {0x2590, 0xde}, {0x2591, 0xb0}, {0x2592, 0xb1}, {0x2593, 0xb2},
        {0x25a0, 0xfe}, {0x25b2, 0x1e}, {0x25ba, 0x10}, {0x25bc, 0x1f},
        {0x25c4, 0x11}, {0x25cf, 0x07}, {0x263a, 0x01}, {0x2665, 0x03},
        {0x2666, 0x04},
    };

    if (codePoint < 0x80) return RawPutChar(codePoint);
    for (const auto& mapping : s_Cp437)
        if (mapping.CodePoint == codePoint) return RawPutChar(mapping.Glyph);

    RawPutChar(0xfe);
}

void VirtualConsole::ICH(u64 count)
{
    auto [x, y] = GetCursorPos();
    for (isize i = m_Size.ws_col - 1; i >= static_cast<isize>(x); i--)
    {
        MoveCharacter(i + count, y, i, y);
        SetCursorPos(i, y);
        RawPutChar(' ');
    }
    SetCursorPos(x, y);
}
void VirtualConsole::CUU(u64 count)
{
    auto [x, y] = GetCursorPos();
    usize top   = y >= m_ScrollTopMargin ? m_ScrollTopMargin : 0;
    SetCursorPos(x, y >= top + count ? y - count : top);
}
void VirtualConsole::CUD(u64 count)
{
    auto [x, y]  = GetCursorPos();
    usize bottom = y < m_ScrollBottomMargin ? m_ScrollBottomMargin - 1
                                            : m_Size.ws_row - 1;
    SetCursorPos(x, Math::Min<usize>(y + count, bottom));
}
void VirtualConsole::CUF(u64 count)
{
    auto [x, y] = GetCursorPos();
    SetCursorPos(Math::Min<usize>(x + count, m_Size.ws_col - 1), y);
}
void VirtualConsole::CUB(u64 count)
{
    auto [x, y] = GetCursorPos();
    SetCursorPos(x >= count ? x - count : 0, y);
}
void VirtualConsole::CNL(u64 count)
{
    CUD(count);
    SetCursorPos(0, GetCursorPos().Value);
}
void VirtualConsole::CPL(u64 count)
{
    CUU(count);
    SetCursorPos(0, GetCursorPos().Value);
}
void VirtualConsole::CHA(u64 column)
{
    --column;
    if (column >= m_Size.ws_col) column = m_Size.ws_col - 1;

    auto y = GetCursorPos().Value;
    SetCursorPos(column, y);
}
void VirtualConsole::CUP()
{
    usize x = Param(1) - 1;
    usize y = Param(0) - 1;

    if (x >= m_Size.ws_col) x = m_Size.ws_col - 1;
    if (y >= m_Size.ws_row) y = m_Size.ws_row - 1;

    SetCursorPos(x, y);
}

void VirtualConsole::ED(u64 parameter)
{
    auto [x, y] = GetCursorPos();
    auto erase  = [this](usize startX, usize endX, usize row)
    {
        SetCursorPos(startX, row);
        for (usize i = startX; i < endX; i++) RawPutChar(' ');
    };

    switch (parameter)
    {
        // From the cursor to the end of the screen
        case 0:
            erase(x, m_Size.ws_col, y);
            for (usize row = y + 1; row < m_Size.ws_row; row++)
                erase(0, m_Size.ws_col, row);
            break;
        // From the start of the screen up to the cursor
        case 1:
            for (usize row = 0; row < y; row++) erase(0, m_Size.ws_col, row);
            erase(0, x + 1, y);
            break;
        // The whole screen
        case 2:
        case 3: Clear(0, false); break;

        default: break;
    }

    SetCursorPos(x, y);
}
void VirtualConsole::EL(u64 parameter)
{
    auto [x, y]  = GetCursorPos();
    usize startX = x;
    usize endX   = m_Size.ws_col;
    if (parameter == 1)
    {
        startX = 0;
        endX   = x + 1;
    }
    else if (parameter == 2) startX = 0;

    SetCursorPos(startX, y);

    for (usize i = startX; i < endX; i++) RawPutChar(' ');
    SetCursorPos(x, y);
}
void VirtualConsole::IL(u64 count)
{
    auto [x, y] = GetCursorPos();
    if (y < m_ScrollTopMargin || y >= m_ScrollBottomMargin) return;

    usize bottom = m_ScrollBottomMargin;
    if (count > bottom - y) count = bottom - y;

    for (usize row = bottom - 1; row >= y + count; row--)
        for (usize col = 0; col < m_Size.ws_col; col++)
            MoveCharacter(col, row, col, row - count);

    for (usize row = y; row < y + count; row++)
    {
        SetCursorPos(0, row);
        for (usize col = 0; col < m_Size.ws_col; col++) RawPutChar(' ');
    }
    SetCursorPos(x, y);
}
void VirtualConsole::DL(u64 count)
{
    auto [x, y] = GetCursorPos();
    if (y < m_ScrollTopMargin || y >= m_ScrollBottomMargin) return;

    usize bottom = m_ScrollBottomMargin;
    if (count > bottom - y) count = bottom - y;

    for (usize row = y; row + count < bottom; row++)
        for (usize col = 0; col < m_Size.ws_col; col++)
            MoveCharacter(col, row, col, row + count);

    for (usize row = bottom - count; row < bottom; row++)
    {
        SetCursorPos(0, row);
        for (usize col = 0; col < m_Size.ws_col; col++) RawPutChar(' ');
    }
    SetCursorPos(x, y);
}
void VirtualConsole::DCH(u64 count)
{
    auto [x, y] = GetCursorPos();
    if (count > m_Size.ws_col - x) count = m_Size.ws_col - x;

    for (usize col = x; col + count < m_Size.ws_col; col++)
        MoveCharacter(col, y, col + count, y);

    SetCursorPos(m_Size.ws_col - count, y);
    for (usize i = 0; i < count; i++) RawPutChar(' ');
    SetCursorPos(x, y);
}
void VirtualConsole::ECH(u64 count)
{
    auto [x, y] = GetCursorPos();
    if (count > m_Size.ws_col - x) count = m_Size.ws_col - x;

    for (usize i = 0; i < count; i++) RawPutChar(' ');
    SetCursorPos(x, y);
}
void VirtualConsole::DA(u64 parameter)
{
    if (parameter != 0) return;

    // Identify as VT102, same as the Linux console
    Respond("\e[?6c");
}
void VirtualConsole::VPA(u64 row)
{
    --row;
    if (row >= m_Size.ws_row) row = m_Size.ws_row - 1;

    auto x = GetCursorPos().Key;
    SetCursorPos(x, row);
}
void VirtualConsole::VPR(u64 count)
{
    auto [x, y] = GetCursorPos();
    SetCursorPos(x, Math::Min<usize>(y + count, m_Size.ws_row - 1));
}
void VirtualConsole::TBC(u64) {}
void VirtualConsole::SM(bool set)
{
    if (!m_DecPrivate) return;

    for (usize i = 0; i < Math::Max<usize>(m_EscapeValueCount, 1); i++)
    {
        switch (m_EscapeValues[i])
        {
            // Cursor keys send ESC O instead of ESC [
            case 1: m_CursorKeyMode = set; break;
            // Cursor visibility
            case 25:
                m_CursorVisible = set;
                set ? ShowCursor() : (void)HideCursor();
                break;
            // Bracketed paste
            case 2004: m_BracketedPaste = set; break;

            default: break;
        }
    }
}
void VirtualConsole::SGR()
{
    // ESC[m is the same as ESC[0m
    usize count = Math::Max<usize>(m_EscapeValueCount, 1);

    // TODO(v1tr10l7): SGR attributes -> half-bright, italic, underscore,
    // blink, reset selected mapping, select null mapping
    for (usize i = 0; i < count; i++)
    {
        u32 parameter = m_EscapeValues[i];
        switch (parameter)
        {
            // Reset all attributes to their defaults
            case 0: ResetAttributes(); break;
            // set bold
            case 1: m_Bold = true; break;
            // set reverse video
            case 7: m_Reverse = true; break;
            // set normal intensity
            case 21:
            case 22: m_Bold = false; break;
            // reverse video off
            case 27: m_Reverse = false; break;
            // set text foreground color
            case 30 ... 37:
                m_Foreground = {ColorSpec::Kind::eAnsi,
                                static_cast<u8>(parameter - 30), {}};
                break;
            // set default text foreground color
            case 39: m_Foreground = {}; break;
            // set text background color
            case 40 ... 47:
                m_Background = {ColorSpec::Kind::eAnsi,
                                static_cast<u8>(parameter - 40), {}};
                break;
            // set default text background color
            case 49: m_Background = {}; break;
            // set bright text foreground color
            case 90 ... 97:
                m_Foreground = {ColorSpec::Kind::eAnsi,
                                static_cast<u8>(parameter - 90 + 8), {}};
                break;
            // set bright text background color
            case 100 ... 107:
                m_Background = {ColorSpec::Kind::eAnsi,
                                static_cast<u8>(parameter - 100 + 8), {}};
                break;
            // set 256/24-bit text foreground/background color
            case 38:
            case 48:
            {
                ColorSpec& spec
                    = parameter == 38 ? m_Foreground : m_Background;
                if (i + 2 < count && m_EscapeValues[i + 1] == 5)
                {
                    u32 index = m_EscapeValues[i + 2] & 0xff;
                    i += 2;

                    if (index < 16)
                    {
                        spec = {ColorSpec::Kind::eAnsi, static_cast<u8>(index),
                                {}};
                        break;
                    }

                    Color rgb;
                    if (index < 232)
                    {
                        index -= 16;
                        auto level = [](u32 v) -> u8
                        { return v ? 55 + v * 40 : 0; };
                        rgb = Color(level(index / 36), level((index / 6) % 6),
                                    level(index % 6));
                    }
                    else
                    {
                        u8 gray = 8 + (index - 232) * 10;
                        rgb     = Color(gray, gray, gray);
                    }
                    spec = {ColorSpec::Kind::eRgb, 0, rgb};
                }
                else if (i + 4 < count && m_EscapeValues[i + 1] == 2)
                {
                    Color rgb(m_EscapeValues[i + 2], m_EscapeValues[i + 3],
                              m_EscapeValues[i + 4]);
                    spec = {ColorSpec::Kind::eRgb, 0, rgb};
                    i += 4;
                }
                else i = count;
                break;
            }

            default: break;
        }
    }

    ApplyAttributes();
}
void VirtualConsole::DSR(u64 parameter)
{
    auto [x, y] = GetCursorPos();
    if (parameter == 5) Respond("\e[0n");
    else if (parameter == 6)
    {
        auto response = fmt::format("\e[{};{}R", y + 1, x + 1);
        Respond(StringView(response.data(), response.size()));
    }
}
void VirtualConsole::DECSTBM()
{
    usize top    = Param(0) - 1;
    usize bottom = Param(1, m_Size.ws_row);
    if (bottom > m_Size.ws_row) bottom = m_Size.ws_row;
    if (top + 1 >= bottom) return;

    m_ScrollTopMargin    = top;
    m_ScrollBottomMargin = bottom;
    SetCursorPos(0, 0);
}

void VirtualConsole::ResetAttributes()
{
    m_Foreground = {};
    m_Background = {};
    m_Bold       = false;
    m_Reverse    = false;

    ApplyAttributes();
}
void VirtualConsole::ApplyAttributes()
{
    auto toAnsi = [](u8 index) -> AnsiColor
    {
        if (index < 8) return static_cast<AnsiColor>(index);
        return static_cast<AnsiColor>(index - 8
                                      + ToUnderlying(AnsiColor::eBrightBlack));
    };

    switch (m_Foreground.Kind)
    {
        case ColorSpec::Kind::eDefault:
            if (m_Bold) SetTextForeground(AnsiColor::eBrightWhite);
            else SetTextForeground(AnsiColor::eDefault);
            break;
        case ColorSpec::Kind::eAnsi:
        {
            u8 index = m_Foreground.Index;
            if (m_Bold && index < 8) index += 8;
            SetTextForeground(toAnsi(index));
            break;
        }
        case ColorSpec::Kind::eRgb:
            SetTextForegroundRgb(m_Foreground.Rgb);
            break;
    }

    switch (m_Background.Kind)
    {
        case ColorSpec::Kind::eDefault:
            SetTextBackground(AnsiColor::eDefault);
            break;
        case ColorSpec::Kind::eAnsi:
            SetTextBackground(toAnsi(m_Background.Index));
            break;
        case ColorSpec::Kind::eRgb:
            SetTextBackgroundRgb(m_Background.Rgb);
            break;
    }

    if (m_Reverse) SwapPalette();
}
