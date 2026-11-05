/*
 * Created by v1tr10l7 on 30.03.2025.
 * Copyright (c) 2024-2026, Szymon Zemke <v1tr10l7@proton.me>
 *
 * SPDX-License-Identifier: GPL-3
 */
#include <Core/Log.hpp>
#include <Terminal/Font.hpp>
#include <Terminal/FramebufferConsole.hpp>

#include <Prism/Utility/Math.hpp>

#define EnsureLoaded()                                                         \
    if (!m_Initialized) Load();

constexpr usize     MAX_FONT_GLYPHS     = 256;
constexpr Color     DEFAULT_BACKGROUND  = Color(0xa0'00'00'00);
constexpr Color     DEFAULT_BACKDROP    = Color(0x00'00'00'00);

constexpr usize     BUILTIN_FONT_WIDTH  = 8;
constexpr usize     BUILTIN_FONT_HEIGHT = 16;

FramebufferConsole* FramebufferConsole::Create(Framebuffer&   framebuffer,
                                               const Options& options)
{
    auto console = new FramebufferConsole(framebuffer, options);
    if (!console->Load())
    {
        delete console;
        return nullptr;
    }

    return console;
}
bool FramebufferConsole::Load()
{
    if (!m_Framebuffer.Address()) return false;
    else if (m_Initialized) return true;

    auto fontAddress = const_cast<u8*>(Meta_fonts_font_bin);

    m_Margin         = 64;
    m_MarginGradient = 4;
    SetCanvas(m_Options.Background.Raw(), m_Options.Background.Size());

    SetTextForeground(AnsiColor::eDefault);
    SetTextBackground(AnsiColor::eDefault);

    SetFont({
        .Address     = fontAddress,
        .Width       = BUILTIN_FONT_WIDTH,
        .Height      = BUILTIN_FONT_HEIGHT,
        .Spacing     = 1,
        .ScaleX      = m_Options.FontScale,
        .ScaleY      = m_Options.FontScale,
        .GlyphWidth  = BUILTIN_FONT_WIDTH * 1,
        .GlyphHeight = BUILTIN_FONT_HEIGHT * 1,
    });

    m_OffsetX = m_Margin
              + ((m_Framebuffer.Width() - m_Margin * 2) % m_Font.GlyphWidth)
                    / 2;
    m_OffsetY = m_Margin
              + ((m_Framebuffer.Height() - m_Margin * 2) % m_Font.GlyphHeight)
                    / 2;

    usize queueEntryCount = m_Size.ws_row * m_Size.ws_col;
    m_Grid.Resize(queueEntryCount);

    for (usize i = 0; i < queueEntryCount; i++)
    {
        m_Grid[i].CodePoint  = ' ';
        m_Grid[i].Foreground = m_CurrentState.TextForeground;
        m_Grid[i].Background = m_CurrentState.TextBackground;
    }

    // NOTE: m_Map stores pointers into m_Queue, it must never reallocate
    m_Queue.Reserve(queueEntryCount);
    m_Map.Resize(queueEntryCount);
    for (auto& entry : m_Map) entry = nullptr;

    m_Initialized = true;
    Reset();
    Refresh();

    return true;
}

void FramebufferConsole::Clear(u32, bool move)
{
    EnsureLoaded();

    Character empty;
    empty.CodePoint  = ' ';
    empty.Foreground = m_CurrentState.TextForeground;
    empty.Background = m_CurrentState.TextBackground;

    for (usize i = 0; i < m_Size.ws_row * m_Size.ws_col; i++)
        EnqueueChar(&empty, i % m_Size.ws_col, i / m_Size.ws_col);

    if (move)
    {
        m_CurrentState.CursorX = 0;
        m_CurrentState.CursorY = 0;
    }
}

void FramebufferConsole::RawPutChar(u8 c)
{
    EnsureLoaded();
    if (m_CurrentState.CursorX >= m_Size.ws_col)
    {
        m_CurrentState.CursorX = 0;
        m_CurrentState.CursorY++;
        if (m_CurrentState.CursorY == m_ScrollBottomMargin)
        {
            m_CurrentState.CursorY--;
            ScrollDown();
        }
        if (m_CurrentState.CursorY >= m_Size.ws_row)
            m_CurrentState.CursorY = m_Size.ws_row - 1;
    }

    Character ch;
    ch.CodePoint  = c;
    ch.Foreground = m_CurrentState.TextForeground;
    ch.Background = m_CurrentState.TextBackground;
    EnqueueChar(&ch, m_CurrentState.CursorX++, m_CurrentState.CursorY);
}
void FramebufferConsole::MoveCharacter(usize newX, usize newY, usize oldX,
                                       usize oldY)
{
    EnsureLoaded();
    if (oldX >= m_Size.ws_col || oldY >= m_Size.ws_row || newX >= m_Size.ws_col
        || newY >= m_Size.ws_row)
        return;

    usize      i = oldX + oldY * m_Size.ws_col;

    QueueItem* q = m_Map[i];
    Character  c = q ? q->Character : m_Grid[i];

    EnqueueChar(&c, newX, newY);
}

void FramebufferConsole::Scroll(isize count)
{
    EnsureLoaded();
    bool revScroll = false;
    if (count < 0)
    {
        revScroll = true;
        count *= -1;
    }

    for (isize i = 0; i < count; i++) revScroll ? ScrollUp() : ScrollDown();
}

void FramebufferConsole::ScrollDown()
{
    EnsureLoaded();
    for (usize i = (m_ScrollTopMargin + 1) * m_Size.ws_col;
         i < m_ScrollBottomMargin * m_Size.ws_col; i++)
    {
        QueueItem* q = m_Map[i];
        Character  c = q ? q->Character : m_Grid[i];
        EnqueueChar(&c, (i - m_Size.ws_col) % m_Size.ws_col,
                    (i - m_Size.ws_col) / m_Size.ws_col);
    }

    Character empty;
    empty.CodePoint  = ' ';
    empty.Foreground = m_CurrentState.TextForeground;
    empty.Background = m_CurrentState.TextBackground;
    for (usize i = 0; i < m_Size.ws_col; i++)
        EnqueueChar(&empty, i, m_ScrollBottomMargin - 1);
}
void FramebufferConsole::ScrollUp()
{
    EnsureLoaded();
    for (usize i = (m_ScrollBottomMargin - 1) * m_Size.ws_col - 1;
         i >= m_ScrollTopMargin * m_Size.ws_col; i--)
    {
        if (i == static_cast<usize>(-1)) break;
        QueueItem* q = m_Map[i];
        Character  c = q ? q->Character : m_Grid[i];
        EnqueueChar(&c, (i + m_Size.ws_col) % m_Size.ws_col,
                    (i + m_Size.ws_col) / m_Size.ws_col);
        if (i == 0) break;
    }

    Character empty;
    empty.CodePoint  = ' ';
    empty.Foreground = m_CurrentState.TextForeground;
    empty.Background = m_CurrentState.TextBackground;
    for (usize i = 0; i < m_Size.ws_col; i++)
        EnqueueChar(&empty, i, m_ScrollTopMargin);
}

void FramebufferConsole::Refresh()
{
    EnsureLoaded();
    if (!m_Active) return;

    for (usize y = 0; y < m_Framebuffer.Height(); y++)
        for (usize x = 0; x < m_Framebuffer.Width(); x++)
            m_Framebuffer.PutPixel(x, y, CanvasPixel(x, y));

    // Make sure that pending changes are drawn as well
    for (usize i = 0; i < m_Size.ws_row * m_Size.ws_col; i++)
    {
        usize x = i % m_Size.ws_col;
        usize y = i / m_Size.ws_col;

        PlotChar(m_Map[i] ? &m_Map[i]->Character : &m_Grid[i], x, y);
    }

    DrawCursor();
    m_OldCursorX = m_CurrentState.CursorX;
    m_OldCursorY = m_CurrentState.CursorY;
}
void FramebufferConsole::Flush()
{
    EnsureLoaded();
    DrawCursor();
    while (!m_Queue.Empty())
    {
        QueueItem q      = m_Queue.PopBackElement();

        usize     offset = q.Y * m_Size.ws_col + q.X;
        if (!m_Map[offset]) continue;

        PlotChar(&q.Character, q.X, q.Y);
        m_Grid[offset] = q.Character;
        m_Map[offset]  = nullptr;
    }

    if (m_OldCursorX != m_CurrentState.CursorX
        || m_OldCursorY != m_CurrentState.CursorY)
    {
        if (m_OldCursorX < m_Size.ws_col && m_OldCursorY < m_Size.ws_row)
            PlotChar(&m_Grid[m_OldCursorX + m_OldCursorY * m_Size.ws_col],
                     m_OldCursorX, m_OldCursorY);
    }

    m_OldCursorX = m_CurrentState.CursorX;
    m_OldCursorY = m_CurrentState.CursorY;
}

void FramebufferConsole::ShowCursor()
{
    m_CursorVisible = true;
    DrawCursor();
}
bool FramebufferConsole::HideCursor()
{
    bool wasVisible = m_CursorVisible;
    m_CursorVisible = false;

    // Redraw the cell under the cursor without the inverted colors
    usize x         = m_CurrentState.CursorX;
    usize y         = m_CurrentState.CursorY;
    if (VerifyBounds(x, y))
    {
        usize      i = x + y * m_Size.ws_col;
        QueueItem* q = m_Map[i];
        PlotChar(q ? &q->Character : &m_Grid[i], x, y);
    }

    return wasVisible;
}

KeyValuePair<usize, usize> FramebufferConsole::GetCursorPos()
{
    usize x = m_CurrentState.CursorX >= m_Size.ws_col ? m_Size.ws_col - 1
                                                      : m_CurrentState.CursorX;
    usize y = m_CurrentState.CursorY >= m_Size.ws_row ? m_Size.ws_row - 1
                                                      : m_CurrentState.CursorY;

    return {x, y};
}
void FramebufferConsole::SetCursorPos(usize xpos, usize ypos)
{
    if (xpos >= m_Size.ws_col)
        xpos = static_cast<i32>(xpos) < 0 ? 0 : m_Size.ws_col - 1;
    if (ypos >= m_Size.ws_row)
        ypos = static_cast<i32>(ypos) < 0 ? 0 : m_Size.ws_row - 1;

    m_CurrentState.CursorX = xpos;
    m_CurrentState.CursorY = ypos;
}

void FramebufferConsole::SaveState() { m_SavedState = m_CurrentState; }
void FramebufferConsole::RestoreState() { m_CurrentState = m_SavedState; }

void FramebufferConsole::SwapPalette()
{
    Swap(m_CurrentState.TextForeground, m_CurrentState.TextBackground);
}

inline constexpr Color s_AnsiColors[]
    = {Color::AnsiBlack(), Color::AnsiRed(), Color::AnsiGreen(),
       Color::AnsiYellow(), Color::AnsiBlue(), Color::AnsiMagenta(),
       // Cyan, White, ---
       Color::AnsiCyan(), Color::AnsiWhite(), Color(0x00555555ull),
       // Default, BrightBlack, BrightRed
       Color(0x00ff5555), Color::AnsiBrightBlack(), Color::AnsiBrightRed(),
       // Bright Green, Bright Yellow, Bright Blue
       Color::AnsiBrightGreen(), Color::AnsiBrightYellow(),
       Color::AnsiBrightBlue(),
       // Bright Magenta, Bright Cyan, Bright White
       Color::AnsiBrightMagenta(), Color::AnsiBrightCyan(),
       Color::AnsiBrightWhite()};

// 8 ansi colors(0-7) + empty entry(8) + default(9) + 8 bright ansi
// colors(10-17)
static_assert(sizeof(s_AnsiColors) / sizeof(s_AnsiColors[0]) == 8 * 2 + 2);

void FramebufferConsole::SetTextForeground(AnsiColor color)
{
    if (color == AnsiColor::eDefault)
        return SetTextForegroundRgb(DEFAULT_TEXT_FOREGROUND);
    else if (color == AnsiColor::eDefaultBright)
        return SetTextForegroundRgb(DEFAULT_TEXT_FOREGROUND_BRIGHT);
    else if (ToUnderlying(color) >= ToUnderlying(AnsiColor::eDefaultBright))
    {
        LogWarn("VT100: Invalid color -> {:#x}", ToUnderlying(color));
        return;
    }

    SetTextForegroundRgb(s_AnsiColors[ToUnderlying(color)]);
}
void FramebufferConsole::SetTextBackground(AnsiColor color)
{
    if (color == AnsiColor::eDefault)
        return SetTextBackgroundRgb(DEFAULT_TEXT_BACKGROUND);
    else if (color == AnsiColor::eDefaultBright)
        return SetTextBackgroundRgb(DEFAULT_TEXT_BACKGROUND_BRIGHT);
    else if (ToUnderlying(color) >= ToUnderlying(AnsiColor::eDefaultBright))
    {
        LogWarn("VT100: Invalid color -> {:#x}", ToUnderlying(color));
        return;
    }

    SetTextBackgroundRgb(s_AnsiColors[ToUnderlying(color)]);
}

void FramebufferConsole::SetTextForegroundRgb(Color rgb)
{
    m_CurrentState.TextForeground = rgb;
}
void FramebufferConsole::SetTextBackgroundRgb(Color rgb)
{
    m_CurrentState.TextBackground = rgb;
}

void FramebufferConsole::Destroy()
{
    m_CanvasPixels.Clear();
    m_Canvas.Address = nullptr;
}

void FramebufferConsole::PlotChar(Character* c, usize xpos, usize ypos)
{
    if (!m_Active || !VerifyBounds(xpos, ypos)) return;
    Color defaultBg  = DEFAULT_TEXT_BACKGROUND;

    xpos             = m_OffsetX + xpos * m_Font.GlyphWidth;
    ypos             = m_OffsetY + ypos * m_Font.GlyphHeight;

    bool* glyph      = &m_Glyphs[c->CodePoint * m_Font.Height * m_Font.Width];
    Color foreground = c->Foreground;
    Color background = c->Background;

    for (usize gy = 0; gy < m_Font.GlyphHeight; gy++)
    {
        usize fy = gy / m_Font.ScaleY;
        for (usize fx = 0; fx < m_Font.Width; fx++)
        {
            bool draw  = glyph[fy * m_Font.Width + fx];
            auto color = draw ? foreground : background;
            for (usize i = 0; i < m_Font.ScaleX; i++)
            {
                usize gx    = m_Font.ScaleX * fx + i;
                // The default background is transparent
                Color pixel = color == defaultBg
                                ? CanvasPixel(xpos + gx, ypos + gy)
                                : color;

                m_Framebuffer.PutPixel(xpos + gx, ypos + gy, pixel);
            }
        }
    }
}

void FramebufferConsole::EnqueueChar(Character* c, usize x, usize y)
{
    if (x >= m_Size.ws_col || y >= m_Size.ws_row) return;
    usize      i = y * m_Size.ws_col + x;

    QueueItem* q = m_Map[i];
    if (!q)
    {
        if (m_Grid[i] == *c) return;
        q        = &m_Queue.EmplaceBack();
        q->X     = x;
        q->Y     = y;
        m_Map[i] = q;
    }

    q->Character = *c;
}

void FramebufferConsole::DrawCursor()
{
    if (!m_CursorVisible
        || !VerifyBounds(m_CurrentState.CursorX, m_CurrentState.CursorY))
        return;
    usize i = m_CurrentState.CursorX + m_CurrentState.CursorY * m_Size.ws_col;

    Character  c;
    QueueItem* q = m_Map[i];
    if (q) c = q->Character;
    else c = m_Grid[i];

    Swap(c.Foreground, c.Background);
    PlotChar(&c, m_CurrentState.CursorX, m_CurrentState.CursorY);
    if (q)
    {
        m_Grid[i] = q->Character;
        m_Map[i]  = nullptr;
    }
}

void FramebufferConsole::SetFont(const Font& font)
{
    m_Font = font;

    if (m_Font.ScaleX < 1 && m_Font.ScaleY < 1)
    {
        m_Font.ScaleX = 1;
        m_Font.ScaleY = 1;

        if (m_Framebuffer.Width() >= (1920 + 1920 / 3)
            && m_Framebuffer.Height() >= (1080 + 1080 / 3))
        {
            m_Font.ScaleX = 2;
            m_Font.ScaleY = 2;
        }
        if (m_Framebuffer.Width() >= (3840 + 3840 / 3)
            && m_Framebuffer.Height() >= (2160 + 2160 / 3))
        {
            m_Font.ScaleX = 4;
            m_Font.ScaleY = 4;
        }
    }

    m_Font.Width += m_Font.Spacing;
    m_Glyphs.Resize(MAX_FONT_GLYPHS * m_Font.Height * m_Font.Width);

    for (usize i = 0; i < MAX_FONT_GLYPHS; i++)
    {
        u8* glyph = m_Font.Address.Offset<Pointer>(i * m_Font.Height).As<u8>();
        for (usize y = 0; y < m_Font.Height; y++)
        {
            for (usize x = 0; x < m_Font.Width; x++)
            {
                usize offset
                    = i * m_Font.Height * m_Font.Width + y * m_Font.Width + x;

                bool draw = glyph[y] & (0x80 >> x);
                // Box drawing characters are extended into the spacing column
                if (x >= 8) draw = i >= 0xc0 && i <= 0xdf && glyph[y] & Bit(0);
                m_Glyphs[offset] = draw;
            }
        }
    }

    m_Font.GlyphWidth  = m_Font.Width * m_Font.ScaleX;
    m_Font.GlyphHeight = m_Font.Height * m_Font.ScaleY;

    m_Size.ws_col
        = (m_Framebuffer.Width() - m_Margin * 2) / m_Font.GlyphWidth;
    m_Size.ws_row
        = (m_Framebuffer.Height() - m_Margin * 2) / m_Font.GlyphHeight;
    m_Size.ws_xpixel = m_Framebuffer.Width();
    m_Size.ws_ypixel = m_Framebuffer.Height();
}
void FramebufferConsole::SetCanvas(u8* image, usize size)
{
    if (image && size > 0 && !m_Image.LoadFromMemory(image, size))
        LogError("VT: Failed to load the canvas!");

    m_Canvas.Width   = m_Image ? m_Image.Width() : 0;
    m_Canvas.Height  = m_Image ? m_Image.Height() : 0;

    m_Margin         = 64;
    m_MarginGradient = 4;

    if (m_Framebuffer.Width() > m_Canvas.Width
        || m_Framebuffer.Height() > m_Canvas.Height)
    {
        // TODO(v1tr10l7): Stretch
        m_Canvas.Width  = m_Framebuffer.Width();
        m_Canvas.Height = m_Framebuffer.Height();
    }

    usize bgCanvasSize = m_Framebuffer.Width() * m_Framebuffer.Height();
    m_CanvasPixels.Resize(bgCanvasSize);
    m_Canvas.Address = m_CanvasPixels.Raw();
    m_Canvas.Size    = bgCanvasSize;

    GenerateGradient();
}
void FramebufferConsole::GenerateGradient()
{
    const bool  hasImage   = static_cast<bool>(m_Image);
    Pointer     pixels     = hasImage ? m_Image.Pixels() : nullptr;
    const usize imagePitch = hasImage ? m_Image.Pitch() : 0;
    const usize columnSize = hasImage ? m_Image.BitsPerPixel() / 8 : 0;
    const usize fbWidth    = m_Framebuffer.Width();
    const usize fbHeight   = m_Framebuffer.Height();

    // Center the image on the screen
    const isize displacementX
        = static_cast<isize>(fbWidth / 2) - static_cast<isize>(m_Image.Width() / 2);
    const isize displacementY = static_cast<isize>(fbHeight / 2)
                              - static_cast<isize>(m_Image.Height() / 2);

    Rectangle gradientArea(m_Margin - m_MarginGradient,
                           m_Margin - m_MarginGradient,
                           fbWidth - (m_Margin - m_MarginGradient) * 2,
                           fbHeight - (m_Margin - m_MarginGradient) * 2);

    for (usize y = 0; y < fbHeight; y++)
    {
        isize imageY       = static_cast<isize>(y) - displacementY;
        usize canvasOffset = fbWidth * y;
        for (usize x = 0; x < fbWidth; x++)
        {
            isize imageX = static_cast<isize>(x) - displacementX;
            Color color  = DEFAULT_BACKDROP;

            if (hasImage && imageX >= 0 && imageY >= 0
                && static_cast<usize>(imageX) < m_Image.Width()
                && static_cast<usize>(imageY) < m_Image.Height())
            {
                // Decoded images are always stored as 0xAARRGGBB
                u32 pixel = *pixels
                                 .Offset<Pointer>(imageX * columnSize
                                                  + imageY * imagePitch)
                                 .As<u32>();
                color = Color(pixel >> 16, pixel >> 8, pixel);
            }

            if (gradientArea.InBounds(x, y))
                color = color.Blend(DEFAULT_BACKGROUND);
            else color = BlendMargin(x, y, color);
            m_CanvasPixels[canvasOffset + x] = color;
        }
    }
}

Color FramebufferConsole::BlendMargin(usize x, usize y, Color backgroundPixel)
{
    usize gradientStopX = m_Framebuffer.Width() - m_Margin;
    usize gradientStopY = m_Framebuffer.Height() - m_Margin;
    usize distanceX     = x - gradientStopX;
    usize distanceY     = y - gradientStopY;

    if (x < m_Margin) distanceX = m_Margin - x;
    if (y < m_Margin) distanceY = m_Margin - y;

    usize distance = Math::Sqrt(
        static_cast<u64>(distanceX) * static_cast<u64>(distanceX)
        + static_cast<u64>(distanceY) * static_cast<u64>(distanceY));
    if (x >= m_Margin && x < gradientStopX) distance = distanceY;
    else if (y >= m_Margin && y < gradientStopY) distance = distanceX;

    if (distance > m_MarginGradient) return backgroundPixel;

    u8 gradientStep = (0xff - DEFAULT_BACKGROUND.Alpha()) / m_MarginGradient;
    u8 newAlpha     = DEFAULT_BACKGROUND.Alpha() + gradientStep * distance;

    return backgroundPixel.Blend({DEFAULT_BACKGROUND, newAlpha});
}
