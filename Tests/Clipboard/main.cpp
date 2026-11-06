/*
 * Created by v1tr10l7 on 06.11.2026.
 * Copyright (c) 2025-2026, Szymon Zemke <v1tr10l7@proton.me>
 *
 * SPDX-License-Identifier: GPL-3
 */
#include <Common/MemoryFramebuffer.hpp>
#include <Terminal/Clipboard.hpp>
#include <Terminal/FramebufferConsole.hpp>

#include <cstdio>
#include <cstdlib>
#include <string>

// Selection, copying and pasting, without any display or clipboard manager

struct Recorder final : public ConsoleClient
{
    std::string Data;
    std::string Clipboard;
    std::string Primary;
    usize       ClipboardWrites = 0;

    void        SendToClient(StringView data) override
    {
        Data.append(data.Raw(), data.Size());
    }
    void SetClipboard(ClipboardKind kind, StringView text) override
    {
        auto& target = kind == ClipboardKind::ePrimary ? Primary : Clipboard;
        target.assign(text.Raw(), text.Size());
        ++ClipboardWrites;
    }
};

static int  s_Failures = 0;

static void ExpectEqual(const std::string& actual, const std::string& expected,
                        const char* what)
{
    if (actual == expected)
    {
        std::printf("ok  : %s\n", what);
        return;
    }

    auto escape = [](const std::string& str)
    {
        std::string out;
        for (unsigned char c : str)
        {
            if (c >= 0x20 && c < 0x7f) out += static_cast<char>(c);
            else
            {
                char hex[8];
                std::snprintf(hex, sizeof(hex), "\\x%02x", c);
                out += hex;
            }
        }
        return out;
    };

    std::printf("FAIL: %s\n", what);
    std::printf("      expected '%s'\n", escape(expected).c_str());
    std::printf("      got      '%s'\n", escape(actual).c_str());
    ++s_Failures;
}
static void ExpectTrue(bool value, const char* what)
{
    std::printf("%s: %s\n", value ? "ok  " : "FAIL", what);
    if (!value) ++s_Failures;
}

static std::string Paste(const char* text, bool bracketed)
{
    return Clipboard::PreparePaste(text, bracketed);
}
static std::string Decode(const char* text, bool* ok = nullptr)
{
    std::string out;
    bool        valid = Clipboard::Base64Decode(text, out);
    if (ok) *ok = valid;
    return out;
}

static void TestHelpers()
{
    ExpectEqual(Decode(""), "", "base64: empty input");
    ExpectEqual(Decode("aGVsbG8="), "hello", "base64: single padding");
    ExpectEqual(Decode("aGk="), "hi", "base64: padded");
    ExpectEqual(Decode("aGk"), "hi", "base64: unpadded");
    ExpectEqual(Decode("4pSA4pSC"), "\xe2\x94\x80\xe2\x94\x82",
                "base64: binary data");

    bool ok = true;
    Decode("a!b=", &ok);
    ExpectTrue(!ok, "base64: invalid characters are rejected");
    Decode("aG=k", &ok);
    ExpectTrue(!ok, "base64: data after padding is rejected");

    ExpectEqual(Paste("ls -l", false), "ls -l", "paste: plain text");
    ExpectEqual(Paste("a\nb\r\nc\r", false), "a\rb\rc\r",
                "paste: line breaks become carriage returns");
    ExpectEqual(Paste("echo hi\n", true), "\e[200~echo hi\r\e[201~",
                "paste: bracketed");
    ExpectEqual(Paste("x\e[201~rm -rf ~\n", true),
                "\e[200~xrm -rf ~\r\e[201~",
                "paste: end marker inside of the text is removed");
    ExpectEqual(Paste("\e[A", false), "\e[A",
                "paste: escapes are untouched without bracketed mode");

    std::string utf8;
    for (u8 glyph : {u8('A'), u8(0xc4), u8(0xb3), u8(0x82), u8(0x01)})
        Clipboard::AppendUtf8(utf8, Clipboard::Cp437ToUnicode(glyph));
    ExpectEqual(utf8, "A\xe2\x94\x80\xe2\x94\x82\xc3\xa9\xe2\x98\xba",
                "cp437 glyphs are converted to UTF-8");
}

static void TestSelection(FramebufferConsole* console, Recorder& recorder)
{
    auto& size = console->GetSize();
    using Cell = FramebufferConsole::Cell;

    console->PrintString("\e[2J\e[H");
    console->PrintString("hello world  \r\n");
    console->PrintString("second line\r\n");
    console->PrintString("/usr/bin/env -i\r\n");
    console->PrintString("\xe2\x94\x8c\xe2\x94\x80\xe2\x94\x90 box\r\n");

    ExpectTrue(!console->HasSelection(), "nothing is selected initially");
    ExpectEqual(console->GetSelectedText(), "", "empty selection is empty");

    console->SetSelection({0, 0}, {4, 0});
    ExpectEqual(console->GetSelectedText(), "hello", "select a range");
    ExpectTrue(console->IsSelected(4, 0) && !console->IsSelected(5, 0),
               "selection bounds are inclusive");

    console->SetSelection({4, 0}, {0, 0});
    ExpectEqual(console->GetSelectedText(), "hello",
                "selecting backwards gives the same text");

    console->SetSelection({6, 0}, {5, 1});
    ExpectEqual(console->GetSelectedText(), "world\nsecond",
                "multi line selections drop trailing blanks");

    console->SetSelection({0, 0}, {size.ws_col - 1u, 1});
    ExpectEqual(console->GetSelectedText(), "hello world\nsecond line",
                "whole lines");

    console->SelectWord({8, 0});
    ExpectEqual(console->GetSelectedText(), "world", "double click selects a word");
    console->SelectWord({3, 2});
    ExpectEqual(console->GetSelectedText(), "/usr/bin/env",
                "paths are selected as a single word");
    console->SelectWord({13, 2});
    ExpectEqual(console->GetSelectedText(), "-i",
                "command line options are words");

    console->SelectLine(1);
    ExpectEqual(console->GetSelectedText(), "second line",
                "triple click selects the line");

    console->SetSelection({0, 3}, {6, 3});
    ExpectEqual(console->GetSelectedText(),
                "\xe2\x94\x8c\xe2\x94\x80\xe2\x94\x90 box",
                "box drawing characters are copied as UTF-8");

    // A blank line inside of the selection is kept
    console->PrintString("\e[6;1Hafter blank\e[H");
    console->SetSelection({0, 3}, {4, 5});
    ExpectEqual(console->GetSelectedText(),
                "\xe2\x94\x8c\xe2\x94\x80\xe2\x94\x90 box\n\nafter",
                "blank lines are preserved");

    // Pixel to cell mapping
    auto origin = console->CellAt(0, 0);
    auto far    = console->CellAt(100000, 100000);
    ExpectTrue(origin == Cell{0, 0}, "pixels in the margin clamp to 0, 0");
    ExpectTrue(far == Cell{size.ws_col - 1u, size.ws_row - 1u},
               "pixels past the grid clamp to the last cell");

    // Output that doesn't touch the selection keeps it
    console->SetSelection({0, 0}, {4, 0});
    console->PrintString("\e[10;1Hunrelated");
    ExpectTrue(console->HasSelection(),
               "output outside of the selection keeps it");
    // Overwriting with the same characters doesn't count as a change
    console->PrintString("\e[1;1Hhello");
    ExpectTrue(console->HasSelection(),
               "rewriting identical characters keeps the selection");
    console->PrintString("\e[1;1Hjello");
    ExpectTrue(!console->HasSelection(),
               "changing a selected cell drops the selection");

    console->SetSelection({0, 1}, {5, 1});
    console->PrintString("\e[2J");
    ExpectTrue(!console->HasSelection(), "clearing the screen drops it");

    console->SetSelection({0, 0}, {1, 0});
    console->ClearSelection();
    ExpectTrue(!console->HasSelection(), "ClearSelection");

    recorder.Data.clear();
}

static void TestOsc52(FramebufferConsole* console, Recorder& recorder)
{
    // "copied" in base64
    console->PrintString("\e]52;c;Y29waWVk\a");
    ExpectEqual(recorder.Clipboard, "copied", "OSC 52 sets the clipboard");

    console->PrintString("\e]52;p;cHJpbWFyeQ==\e\\");
    ExpectEqual(recorder.Primary, "primary",
                "OSC 52 terminated by ST sets the primary selection");

    console->PrintString("\e]52;;Ym90aA==\a");
    ExpectEqual(recorder.Clipboard, "both",
                "OSC 52 without selections defaults to the clipboard");

    usize writes = recorder.ClipboardWrites;
    console->PrintString("\e]52;c;?\a");
    console->PrintString("\e]52;c;not base64!\a");
    console->PrintString("\e]0;Y29waWVk\a");
    console->PrintString("\ePY29waWVk\e\\");
    ExpectTrue(recorder.ClipboardWrites == writes,
               "queries, invalid data and other OSCs are ignored");
    ExpectEqual(recorder.Data, "", "nothing is sent back to the client");

    // The sequence must not leave anything on the screen
    console->PrintString("\e[H\e[2J\e]52;c;eA==\ay");
    ExpectTrue(console->GetCharacter(0, 0) == 'y',
               "OSC 52 is not printed");
}

static void TestBracketedPaste(FramebufferConsole* console)
{
    ExpectTrue(!console->GetBracketedPasteMode(), "bracketed paste is off");
    console->PrintString("\e[?2004h");
    ExpectTrue(console->GetBracketedPasteMode(), "ESC[?2004h enables it");
    console->PrintString("\e[?2004l");
    ExpectTrue(!console->GetBracketedPasteMode(), "ESC[?2004l disables it");
    console->PrintString("\e[?2004h\ec");
    ExpectTrue(!console->GetBracketedPasteMode(), "reset disables it");
}

int main()
{
    TestHelpers();

    MemoryFramebuffer framebuffer(800, 600);
    Recorder          recorder;

    auto console = FramebufferConsole::Create(framebuffer, {});
    if (!console)
    {
        std::printf("FAIL: could not create the console\n");
        return EXIT_FAILURE;
    }
    console->SetClient(&recorder);
    console->SetActive(true);

    TestSelection(console, recorder);
    TestOsc52(console, recorder);
    TestBracketedPaste(console);

    // The in memory clipboard used by backends without a system clipboard
    framebuffer.SetClipboard(ClipboardKind::eClipboard, "clip");
    framebuffer.SetClipboard(ClipboardKind::ePrimary, "sel");
    ExpectEqual(framebuffer.GetClipboard(ClipboardKind::eClipboard), "clip",
                "framebuffer keeps the clipboard");
    ExpectEqual(framebuffer.GetClipboard(ClipboardKind::ePrimary), "sel",
                "framebuffer keeps the primary selection separately");

    delete console;

    if (s_Failures)
    {
        std::printf("%d check(s) failed\n", s_Failures);
        return EXIT_FAILURE;
    }

    std::printf("Success!\n");
    return EXIT_SUCCESS;
}
