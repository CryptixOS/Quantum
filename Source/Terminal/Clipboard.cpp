/*
 * Created by v1tr10l7 on 06.11.2026.
 * Copyright (c) 2024-2026, Szymon Zemke <v1tr10l7@proton.me>
 *
 * SPDX-License-Identifier: GPL-3
 */
#include <Terminal/Clipboard.hpp>

namespace Clipboard
{
    // clang-format off
    static constexpr u16 s_Cp437Low[0x20] = {
        0x0020, 0x263a, 0x263b, 0x2665, 0x2666, 0x2663, 0x2660, 0x2022,
        0x25d8, 0x25cb, 0x25d9, 0x2642, 0x2640, 0x266a, 0x266b, 0x263c,
        0x25ba, 0x25c4, 0x2195, 0x203c, 0x00b6, 0x00a7, 0x25ac, 0x21a8,
        0x2191, 0x2193, 0x2192, 0x2190, 0x221f, 0x2194, 0x25b2, 0x25bc,
    };
    static constexpr u16 s_Cp437High[0x80] = {
        0x00c7, 0x00fc, 0x00e9, 0x00e2, 0x00e4, 0x00e0, 0x00e5, 0x00e7,
        0x00ea, 0x00eb, 0x00e8, 0x00ef, 0x00ee, 0x00ec, 0x00c4, 0x00c5,
        0x00c9, 0x00e6, 0x00c6, 0x00f4, 0x00f6, 0x00f2, 0x00fb, 0x00f9,
        0x00ff, 0x00d6, 0x00dc, 0x00a2, 0x00a3, 0x00a5, 0x20a7, 0x0192,
        0x00e1, 0x00ed, 0x00f3, 0x00fa, 0x00f1, 0x00d1, 0x00aa, 0x00ba,
        0x00bf, 0x2310, 0x00ac, 0x00bd, 0x00bc, 0x00a1, 0x00ab, 0x00bb,
        0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556,
        0x2555, 0x2563, 0x2551, 0x2557, 0x255d, 0x255c, 0x255b, 0x2510,
        0x2514, 0x2534, 0x252c, 0x251c, 0x2500, 0x253c, 0x255e, 0x255f,
        0x255a, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256c, 0x2567,
        0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256b,
        0x256a, 0x2518, 0x250c, 0x2588, 0x2584, 0x258c, 0x2590, 0x2580,
        0x03b1, 0x00df, 0x0393, 0x03c0, 0x03a3, 0x03c3, 0x00b5, 0x03c4,
        0x03a6, 0x0398, 0x03a9, 0x03b4, 0x221e, 0x03c6, 0x03b5, 0x2229,
        0x2261, 0x00b1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00f7, 0x2248,
        0x00b0, 0x2219, 0x00b7, 0x221a, 0x207f, 0x00b2, 0x25a0, 0x00a0,
    };
    // clang-format on

    u32 Cp437ToUnicode(u8 glyph)
    {
        if (glyph < 0x20) return s_Cp437Low[glyph];
        if (glyph < 0x7f) return glyph;
        if (glyph == 0x7f) return 0x2302;
        return s_Cp437High[glyph - 0x80];
    }

    void AppendUtf8(std::string& out, u32 codePoint)
    {
        if (codePoint < 0x80) out += static_cast<char>(codePoint);
        else if (codePoint < 0x800)
        {
            out += static_cast<char>(0xc0 | (codePoint >> 6));
            out += static_cast<char>(0x80 | (codePoint & 0x3f));
        }
        else if (codePoint < 0x10000)
        {
            out += static_cast<char>(0xe0 | (codePoint >> 12));
            out += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3f));
            out += static_cast<char>(0x80 | (codePoint & 0x3f));
        }
        else
        {
            out += static_cast<char>(0xf0 | (codePoint >> 18));
            out += static_cast<char>(0x80 | ((codePoint >> 12) & 0x3f));
            out += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3f));
            out += static_cast<char>(0x80 | (codePoint & 0x3f));
        }
    }

    bool Base64Decode(StringView input, std::string& out)
    {
        auto value = [](char c) -> i32
        {
            if (c >= 'A' && c <= 'Z') return c - 'A';
            if (c >= 'a' && c <= 'z') return c - 'a' + 26;
            if (c >= '0' && c <= '9') return c - '0' + 52;
            if (c == '+') return 62;
            if (c == '/') return 63;
            return -1;
        };

        out.clear();
        u32   accumulator = 0;
        usize bits        = 0;
        bool  padding     = false;
        for (usize i = 0; i < input.Size(); i++)
        {
            char c = input.Raw()[i];
            if (c == '=')
            {
                padding = true;
                continue;
            }
            // Nothing but padding may follow the padding
            if (padding) return false;

            i32 v = value(c);
            if (v < 0) return false;

            accumulator = (accumulator << 6) | v;
            bits += 6;
            if (bits >= 8)
            {
                bits -= 8;
                out += static_cast<char>((accumulator >> bits) & 0xff);
            }
        }

        return true;
    }

    std::string PreparePaste(StringView text, bool bracketed)
    {
        constexpr StringView END_MARKER = "\e[201~";

        std::string          out;
        out.reserve(text.Size() + 12);
        if (bracketed) out += "\e[200~";

        for (usize i = 0; i < text.Size(); i++)
        {
            char c = text.Raw()[i];

            // The pasted text must not be able to terminate the paste early
            if (bracketed && c == '\e' && i + END_MARKER.Size() <= text.Size()
                && StringView(text.Raw() + i, END_MARKER.Size()) == END_MARKER)
            {
                i += END_MARKER.Size() - 1;
                continue;
            }

            if (c == '\r' && i + 1 < text.Size() && text.Raw()[i + 1] == '\n')
                continue;
            out += c == '\n' ? '\r' : c;
        }

        if (bracketed) out.append(END_MARKER.Raw(), END_MARKER.Size());
        return out;
    }
}; // namespace Clipboard
