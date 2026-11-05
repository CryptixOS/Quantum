/*
 * Created by v1tr10l7 on 02.09.2025.
 * Copyright (c) 2025-2026, Szymon Zemke <v1tr10l7@proton.me>
 *
 * SPDX-License-Identifier: GPL-3
 */
#include <Common/MemoryFramebuffer.hpp>
#include <Terminal/FramebufferConsole.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

struct Recorder final : public ConsoleClient
{
    std::string Data;
    void        SendToClient(StringView data) override
    {
        Data.append(data.Raw(), data.Size());
    }
};

static int s_Failures = 0;

static void Expect(FramebufferConsole* console, Recorder& recorder,
                   const char* input, const char* expectedResponse)
{
    recorder.Data.clear();
    console->PrintString(input);
    console->PrintString("\e[6n");

    if (recorder.Data != expectedResponse)
    {
        std::printf("FAIL: input '%s'\n", input);
        std::printf("      expected '%s', got '%s'\n", expectedResponse + 1,
                    recorder.Data.empty() ? "" : recorder.Data.c_str() + 1);
        ++s_Failures;
    }
}

int main()
{
    printf("Running tests...\n");

    MemoryFramebuffer framebuffer(800, 600);
    Recorder          recorder;

    auto console = FramebufferConsole::Create(framebuffer, {});
    if (!console)
    {
        printf("FAIL: could not create the console\n");
        return EXIT_FAILURE;
    }
    console->SetClient(&recorder);
    console->SetActive(true);

    auto& size = console->GetSize();
    printf("console size: %ux%u\n", size.ws_col, size.ws_row);

    Expect(console, recorder, "", "\e[1;1R");
    Expect(console, recorder, "abc", "\e[1;4R");
    Expect(console, recorder, "\r\n", "\e[2;1R");
    Expect(console, recorder, "\e[5;10H", "\e[5;10R");
    Expect(console, recorder, "\e[2A", "\e[3;10R");
    Expect(console, recorder, "\e[3B", "\e[6;10R");
    Expect(console, recorder, "\e[4C", "\e[6;14R");
    Expect(console, recorder, "\e[20D", "\e[6;1R");
    Expect(console, recorder, "\e[H", "\e[1;1R");
    Expect(console, recorder, "\e[7G", "\e[1;7R");
    Expect(console, recorder, "\e[3d", "\e[3;7R");
    Expect(console, recorder, "\t", "\e[3;9R");
    Expect(console, recorder, "\b\b", "\e[3;7R");
    // Line feed doesn't return the carriage
    Expect(console, recorder, "\n", "\e[4;7R");
    Expect(console, recorder, "\e7\e[10;10H\e8", "\e[4;7R");
    Expect(console, recorder, "\e[K\e[2J", "\e[4;7R");
    // OSC sequences are swallowed
    Expect(console, recorder, "\e]0;title\ax", "\e[4;8R");
    // UTF-8 sequences take a single cell
    Expect(console, recorder, "\xe2\x94\x80\xe2\x94\x82", "\e[4;10R");
    Expect(console, recorder, "\e[1;31;42mX\e[0m\e[38;2;1;2;3mY\e[m", "\e[4;12R");

    // Scrolling at the bottom keeps the cursor on the last row
    std::string bottom = "\e[" + std::to_string(size.ws_row) + ";1H\n\n";
    std::string expect = "\e[" + std::to_string(size.ws_row) + ";1R";
    Expect(console, recorder, bottom.c_str(), expect.c_str());

    // Writing past the last column wraps
    std::string line(size.ws_col + 2, 'x');
    std::string wrap = "\e[1;1H" + line;
    Expect(console, recorder, wrap.c_str(), "\e[2;3R");

    // Scroll regions
    Expect(console, recorder, "\e[2;5r", "\e[1;1R");
    Expect(console, recorder, "\e[5;1H\n\n", "\e[5;1R");
    Expect(console, recorder, "\e[r\e[L\e[M\e[P\e[X\e[@", "\e[1;1R");

    Expect(console, recorder, "\e[5n", "\e[0n\e[1;1R");
    Expect(console, recorder, "\e[c", "\e[?6c\e[1;1R");

    delete console;

    if (s_Failures)
    {
        printf("%d test(s) failed\n", s_Failures);
        return EXIT_FAILURE;
    }

    printf("Success!\n");
    return EXIT_SUCCESS;
}
