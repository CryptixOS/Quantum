/*
 * Created by v1tr10l7 on 05.11.2026.
 * Copyright (c) 2025-2026, Szymon Zemke <v1tr10l7@proton.me>
 *
 * SPDX-License-Identifier: GPL-3
 */
#include <Common/MemoryFramebuffer.hpp>
#include <Platform/PseudoTerminal.hpp>
#include <Terminal/FramebufferConsole.hpp>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <poll.h>
#include <string>
#include <unistd.h>

// Launches a real shell on a pseudo terminal, attached to the console the
// same way main.cpp does, and checks what ends up on the screen

constexpr int EXIT_SKIP  = 77;
constexpr int TIMEOUT_MS = 5000;

struct Session final : public ConsoleClient
{
    FramebufferConsole* Console = nullptr;
    PseudoTerminal      Pty;
    bool                HungUp = false;

    void SendToClient(StringView data) override { Pty.Write(data); }

    std::string Row(usize y) const
    {
        std::string row;
        for (usize x = 0; x < Console->GetSize().ws_col; x++)
            row += static_cast<char>(Console->GetCharacter(x, y));

        while (!row.empty() && row.back() == ' ') row.pop_back();
        return row;
    }
    std::string Screen() const
    {
        std::string screen;
        for (usize y = 0; y < Console->GetSize().ws_row; y++)
        {
            auto row = Row(y);
            if (!row.empty()) screen += "  | " + row + "\n";
        }
        return screen;
    }

    // Feeds the shell's output to the console until @done returns true
    template <typename Predicate>
    bool Pump(Predicate done)
    {
        auto deadline = std::chrono::steady_clock::now()
                      + std::chrono::milliseconds(TIMEOUT_MS);
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (done()) return true;
            if (HungUp) return false;

            pollfd fd = {Pty.MasterFd(), POLLIN, 0};
            if (poll(&fd, 1, 50) <= 0) continue;

            u8 buffer[4096];
            for (;;)
            {
                isize nread = Pty.Read(buffer, sizeof(buffer));
                if (nread < 0) break;
                if (nread == 0)
                {
                    HungUp = true;
                    break;
                }

                Console->PrintString(
                    StringView(reinterpret_cast<char*>(buffer), nread));
            }
        }

        return done();
    }

    bool HasRow(const std::string& expected) const
    {
        for (usize y = 0; y < Console->GetSize().ws_row; y++)
            if (Row(y) == expected) return true;
        return false;
    }
    bool HasRowStartingWith(const std::string& prefix) const
    {
        for (usize y = 0; y < Console->GetSize().ws_row; y++)
            if (Row(y).starts_with(prefix)) return true;
        return false;
    }
};

static int s_Failures = 0;

static void Check(Session& session, bool ok, const char* what)
{
    std::printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
    if (ok) return;

    std::printf("  screen:\n%s", session.Screen().c_str());
    ++s_Failures;
}

int main()
{
    const char* shell = "/bin/sh";
    if (access(shell, X_OK) != 0)
    {
        std::printf("SKIP: %s is not available\n", shell);
        return EXIT_SKIP;
    }

    // Keep the environment of the shell predictable
    setenv("PS1", "quantum$ ", 1);
    setenv("HISTFILE", "/dev/null", 1);
    unsetenv("ENV");
    unsetenv("PROMPT_COMMAND");

    MemoryFramebuffer framebuffer(800, 600);
    Session           session;
    session.Console = FramebufferConsole::Create(framebuffer, {});
    if (!session.Console)
    {
        std::printf("FAIL: could not create the console\n");
        return EXIT_FAILURE;
    }
    session.Console->SetClient(&session);
    session.Console->SetActive(true);

    auto& size = session.Console->GetSize();
    std::printf("console size: %ux%u\n", size.ws_col, size.ws_row);

    char  interactive[] = "-i";
    char* argv[] = {const_cast<char*>(shell), interactive, nullptr};
    if (!session.Pty.Spawn(argv, size))
    {
        std::printf("FAIL: could not spawn %s\n", shell);
        return EXIT_FAILURE;
    }

    Check(session,
          session.Pump([&] { return session.HasRowStartingWith("quantum$"); }),
          "shell prints its prompt");

    // Keys are delivered the same way the backends do, Enter sends CR
    session.Pty.Write("echo hello-$((40+2))\r");
    Check(session, session.Pump([&] { return session.HasRow("hello-42"); }),
          "shell executes a command");

    session.Pty.Write("stty size\r");
    auto expectedSize
        = std::to_string(size.ws_row) + " " + std::to_string(size.ws_col);
    Check(session,
          session.Pump([&] { return session.HasRow(expectedSize); }),
          "shell sees the console size");

    session.Pty.Write("printf 'a\\tb\\033[31m colored\\033[0m\\n'\r");
    Check(session,
          session.Pump([&] { return session.HasRow("a       b colored"); }),
          "escape sequences from the shell are interpreted");

    session.Pty.Write("echo $TERM\r");
    Check(session, session.Pump([&] { return session.HasRow("linux"); }),
          "TERM is set to linux");

    session.Pty.Write("clear\r");
    Check(session,
          session.Pump(
              [&]
              {
                  return session.Row(0).starts_with("quantum$")
                      && !session.HasRow("hello-42");
              }),
          "clear wipes the screen");

    session.Pty.Write("exit\r");
    Check(session, session.Pump([&] { return session.HungUp; }),
          "shell exits and hangs up the pty");

    delete session.Console;

    if (s_Failures)
    {
        std::printf("%d check(s) failed\n", s_Failures);
        return EXIT_FAILURE;
    }

    std::printf("Success!\n");
    return EXIT_SUCCESS;
}
