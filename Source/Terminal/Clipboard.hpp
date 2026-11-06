/*
 * Created by v1tr10l7 on 06.11.2026.
 * Copyright (c) 2024-2026, Szymon Zemke <v1tr10l7@proton.me>
 *
 * SPDX-License-Identifier: GPL-3
 */
#pragma once

#include <Core/Prism.hpp>

#include <Prism/String/StringView.hpp>

#include <string>

// X11 style clipboards, the primary selection is set whenever text gets
// selected, the clipboard only on explicit copy requests
enum class ClipboardKind
{
    eClipboard,
    ePrimary,
};

namespace Clipboard
{
    // Unicode code point of a glyph of the builtin, code page 437 font
    u32         Cp437ToUnicode(u8 glyph);
    void        AppendUtf8(std::string& out, u32 codePoint);

    // Returns false if @input is not valid base64
    bool        Base64Decode(StringView input, std::string& out);

    // Converts text taken from a clipboard into the bytes that should be sent
    // to the client, line breaks become carriage returns, as if the text was
    // typed, and when bracketed paste mode is enabled the text is wrapped in
    // ESC[200~ ... ESC[201~
    std::string PreparePaste(StringView text, bool bracketed);
}; // namespace Clipboard
