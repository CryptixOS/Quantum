/*
 * Created by v1tr10l7 on 30.03.2025.
 * Copyright (c) 2024-2026, Szymon Zemke <v1tr10l7@proton.me>
 *
 * SPDX-License-Identifier: GPL-3
 */
#pragma once

#include <Graphics/Framebuffer.hpp>
#include <Graphics/Image.hpp>
#include <Terminal/VirtualConsole.hpp>

struct Rectangle
{
    usize          X;
    usize          Y;
    usize          Width;
    usize          Height;

    constexpr bool InBounds(usize x, usize y)
    {
        return (x >= X && x < X + Width) && (y >= Y && y < Y + Height);
    }
};

class FramebufferConsole final : public VirtualConsole
{
  public:
    struct Options
    {
        // PNG image drawn behind the text, may be empty
        Span<u8> Background = {};
        // 0 picks the scale automatically, based on the resolution
        usize    FontScale  = 0;
    };

    static FramebufferConsole* Create(Framebuffer&   framebuffer,
                                      const Options& options);
    ~FramebufferConsole() override { Destroy(); }

    bool         Load();

    virtual void Clear(u32 = 0xffffffff, bool clear = true) override;

    virtual void Refresh() override;
    virtual void Flush() override;

    // Glyph currently displayed at the given cell, including pending changes
    u32          GetCharacter(usize x, usize y) const
    {
        if (x >= m_Size.ws_col || y >= m_Size.ws_row) return 0;
        usize i = y * m_Size.ws_col + x;
        return m_Map[i] ? m_Map[i]->Character.CodePoint : m_Grid[i].CodePoint;
    }

    // Text selection, used for copying, it is linear, i.e. it covers
    // everything from the start cell up to the end cell, like in the Linux
    // console, it's dropped as soon as any of the selected cells changes
    struct Cell
    {
        usize          X = 0;
        usize          Y = 0;

        constexpr bool operator==(const Cell& other) const = default;
    };

    // Cell under the pixel at @x, @y, clamped to the grid
    Cell         CellAt(usize x, usize y) const;

    // Selects everything between @anchor and @head, inclusive, in either
    // direction
    void         SetSelection(Cell anchor, Cell head);
    // Selects the word, or the run of blanks under @cell
    void         SelectWord(Cell cell);
    void         SelectLine(usize row);
    void         ClearSelection();

    inline bool  HasSelection() const { return m_HasSelection; }
    bool         IsSelected(usize x, usize y) const;
    // Contents of the selection encoded as UTF-8, trailing blanks of every
    // line are removed, and lines are separated with '\n'
    std::string  GetSelectedText() const;

  protected:
    explicit FramebufferConsole(Framebuffer& framebuffer, const Options& options)
        : m_Framebuffer(framebuffer)
        , m_Options(options)
    {
    }

    virtual void RawPutChar(u8 c) override;
    virtual void MoveCharacter(usize newX, usize newY, usize oldX,
                               usize oldY) override;

    virtual void Scroll(isize count = 1) override;

    virtual void ScrollDown() override;
    virtual void ScrollUp() override;

    virtual void ShowCursor() override;
    virtual bool HideCursor() override;

    virtual KeyValuePair<usize, usize> GetCursorPos() override;
    virtual void SetCursorPos(usize xpos, usize ypos) override;

    virtual void SaveState() override;
    virtual void RestoreState() override;

    virtual void SwapPalette() override;

    virtual void SetTextForeground(AnsiColor color) override;
    virtual void SetTextBackground(AnsiColor color) override;

    virtual void SetTextForegroundRgb(Color color) override;
    virtual void SetTextBackgroundRgb(Color color) override;

    virtual void Destroy() override;

  private:
    Framebuffer& m_Framebuffer;
    Options      m_Options;

    struct Font
    {
        Pointer Address;
        usize   Width;
        usize   Height;
        usize   Spacing;
        usize   ScaleX;
        usize   ScaleY;
        usize   GlyphWidth;
        usize   GlyphHeight;
    } m_Font;

    struct Image
    {
        Pointer Address;
        usize   Width;
        usize   Height;
        usize   Size;
    } m_Canvas = {};
    Vector<u32> m_CanvasPixels;
    PNG::Image  m_Image;

    struct ConsoleState
    {
        usize CursorX;
        usize CursorY;
        Color TextForeground;
        Color TextBackground;
        usize Bold;
        usize BackgroundBold;
        usize ReverseVideo;
        usize Charset;
        usize Primary;
        Color Background;
    };

    constexpr static Color DEFAULT_TEXT_FOREGROUND = Color(0x00'aa'aa'aa);
    constexpr static Color DEFAULT_TEXT_BACKGROUND = Color(0xff'ff'ff'ff);
    constexpr static Color DEFAULT_TEXT_FOREGROUND_BRIGHT
        = Color(0xa0'55'55'55);
    constexpr static Color DEFAULT_TEXT_BACKGROUND_BRIGHT
        = Color(0x00'aa'aa'aa);

    ConsoleState m_CurrentState   = {};
    ConsoleState m_SavedState     = {};

    usize        m_OldCursorX     = 0;
    usize        m_OldCursorY     = 0;
    usize        m_OffsetX        = 0;
    usize        m_OffsetY        = 0;
    bool         m_CursorVisible  = true;

    u16          m_Margin         = 0;
    u16          m_MarginGradient = 0;

    struct Character
    {
        u32            CodePoint;
        Color          Foreground;
        Color          Background;

        constexpr bool operator==(const Character& other) const
        {
            return !(CodePoint != other.CodePoint
                     || Foreground != other.Foreground
                     || Background != other.Background);
        }
    };

    struct QueueItem
    {
        usize     X;
        usize     Y;
        Character Character;
    };

    // We use vectors here, so we are cache friendly,
    // as those fields aren't likely to be resized any way
    Vector<QueueItem>  m_Queue;
    Vector<QueueItem*> m_Map;
    Vector<Character>  m_Grid;
    Vector<bool>       m_Glyphs;

    bool               m_HasSelection   = false;
    bool               m_SelectionStale = false;
    Cell               m_SelectionStart = {};
    Cell               m_SelectionEnd   = {};

    void               RedrawRows(usize first, usize last);

    void               PlotChar(Character* c, usize xpos, usize ypos);
    void               EnqueueChar(Character* c, usize x, usize y);

    void               DrawCursor();
    void               SetFont(const Font& font);
    void               SetCanvas(u8* image, usize size);

    void               GenerateGradient();
    Color              BlendMargin(usize x, usize y, Color orig);

    inline Color       CanvasPixel(usize x, usize y) const
    {
        return Color(m_CanvasPixels[y * m_Framebuffer.Width() + x]);
    }

    constexpr bool VerifyBounds(usize x, usize y)
    {
        return x < m_Size.ws_col && y < m_Size.ws_row;
    }
};
