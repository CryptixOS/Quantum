/*
 * Created by v1tr10l7 on 02.09.2025.
 * Copyright (c) 2024-2026, Szymon Zemke <v1tr10l7@proton.me>
 *
 * SPDX-License-Identifier: GPL-3
 */
#include <Core/Log.hpp>
#include <Graphics/FbDevFramebuffer.hpp>
#include <Platform/PseudoTerminal.hpp>
#include <Terminal/FramebufferConsole.hpp>

#if QUANTUM_HAVE_SDL
    #include <Graphics/SDLFramebuffer.hpp>
#endif

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef QUANTUM_DATA_DIR
    #define QUANTUM_DATA_DIR "/usr/share/quantum-term"
#endif

namespace
{
    constexpr usize MAX_CONSOLE_COUNT = 8;

    struct Arguments
    {
        const char*  Backend    = nullptr;
        const char*  Device     = "/dev/fb0";
        const char*  Background = nullptr;
        usize        FontScale  = 0;
        usize        Width      = 1280;
        usize        Height     = 800;
        char* const* Command    = nullptr;
    };

    void PrintUsage(const char* program)
    {
        std::fprintf(
            stderr,
            "Usage: %s [options] [-e command [args...]]\n"
            "\n"
            "Options:\n"
            "  -b, --backend <sdl|fbdev>  display backend to use\n"
            "  -d, --device <path>        fbdev device (default: /dev/fb0)\n"
            "  -g, --background <path>    background image (PNG)\n"
            "      --no-background        don't draw a background image\n"
            "  -s, --scale <n>            font scale (default: automatic)\n"
            "      --size <WxH>           window size of the sdl backend\n"
            "  -v, --verbose              enable trace logging\n"
            "  -h, --help                 show this message\n",
            program);
    }

    bool ParseArguments(int argc, char** argv, Arguments& args)
    {
        static char  noBackground[] = "";
        auto         next = [&](int& i) -> const char*
        {
            if (i + 1 >= argc)
            {
                std::fprintf(stderr, "quantum-term: '%s' expects a value\n",
                             argv[i]);
                std::exit(EXIT_FAILURE);
            }
            return argv[++i];
        };

        for (int i = 1; i < argc; i++)
        {
            StringView arg = argv[i];
            if (arg == "-h" || arg == "--help")
            {
                PrintUsage(argv[0]);
                std::exit(EXIT_SUCCESS);
            }
            else if (arg == "-b" || arg == "--backend") args.Backend = next(i);
            else if (arg == "-d" || arg == "--device") args.Device = next(i);
            else if (arg == "-g" || arg == "--background")
                args.Background = next(i);
            else if (arg == "--no-background") args.Background = noBackground;
            else if (arg == "-s" || arg == "--scale")
                args.FontScale = std::strtoul(next(i), nullptr, 10);
            else if (arg == "--size")
            {
                const char* size = next(i);
                char*       end  = nullptr;
                args.Width       = std::strtoul(size, &end, 10);
                if (!end || *end != 'x') return false;
                args.Height = std::strtoul(end + 1, nullptr, 10);
                if (args.Width < 320 || args.Height < 240) return false;
            }
            else if (arg == "-v" || arg == "--verbose")
                Quantum::Log::MinimumLevel() = Quantum::Log::Level::eTrace;
            else if (arg == "-e")
            {
                if (i + 1 >= argc) return false;
                args.Command = argv + i + 1;
                break;
            }
            else
            {
                std::fprintf(stderr, "quantum-term: unknown option '%s'\n",
                             argv[i]);
                return false;
            }
        }

        return true;
    }

    Vector<u8> ReadFile(const char* path)
    {
        Vector<u8> contents;

        i32        fd = open(path, O_RDONLY | O_CLOEXEC);
        if (fd < 0) return contents;

        struct stat st;
        if (fstat(fd, &st) == 0 && st.st_size > 0)
        {
            contents.Resize(st.st_size);

            usize offset = 0;
            while (offset < contents.Size())
            {
                isize nread
                    = read(fd, contents.Raw() + offset, contents.Size() - offset);
                if (nread <= 0) break;
                offset += nread;
            }
            contents.Resize(offset);
        }

        close(fd);
        return contents;
    }

    Framebuffer* CreateFramebuffer(const Arguments& args)
    {
        StringView backend = args.Backend ? args.Backend : "";
#if QUANTUM_HAVE_SDL
        // Prefer SDL, unless we are running directly on a console
        if (backend.Empty())
            backend = getenv("DISPLAY") || getenv("WAYLAND_DISPLAY") ? "sdl"
                                                                     : "fbdev";
        if (backend == "sdl") return new SDLFramebuffer(args.Width, args.Height);
#else
        if (backend.Empty()) backend = "fbdev";
#endif
        if (backend == "fbdev") return new FbDevFramebuffer(args.Device);

        LogError("Unknown or unsupported backend '{}'",
                 fmt::string_view(backend.Raw(), backend.Size()));
        return nullptr;
    }

    struct Session final : public ConsoleClient
    {
        FramebufferConsole* Console = nullptr;
        PseudoTerminal      Pty;

        ~Session() override { delete Console; }

        void SendToClient(StringView data) override { Pty.Write(data); }
    };

    class Application final : public InputListener
    {
      public:
        Application(Framebuffer& framebuffer, const Arguments& args)
            : m_Framebuffer(framebuffer)
            , m_Arguments(args)
        {
        }
        ~Application() override
        {
            for (auto session : m_Sessions) delete session;
        }

        bool Initialize()
        {
            const char* background = m_Arguments.Background;
            if (!background) background = getenv("QUANTUM_BACKGROUND");
            if (!background) background = QUANTUM_DATA_DIR "/background.png";

            if (*background)
            {
                m_Background = ReadFile(background);
                if (m_Background.Empty())
                    LogWarn("Failed to read the background image '{}'",
                            background);
            }

            for (usize i = 0; i < MAX_CONSOLE_COUNT; i++)
                m_Sessions.PushBack(nullptr);

            return SwitchTo(0);
        }

        i32 Run()
        {
            while (m_Running)
            {
                Vector<pollfd>   fds;
                Vector<Session*> owners;
                for (auto session : m_Sessions)
                {
                    if (!session || !session->Pty.IsAlive()) continue;

                    fds.PushBack({session->Pty.MasterFd(), POLLIN, 0});
                    owners.PushBack(session);
                }

                i32 eventFd = m_Framebuffer.EventFd();
                if (eventFd >= 0) fds.PushBack({eventFd, POLLIN, 0});

                // Backends without a pollable fd are polled at ~60 Hz
                i32 timeout = eventFd >= 0 ? -1 : 16;
                if (poll(fds.Raw(), fds.Size(), timeout) < 0 && errno != EINTR)
                {
                    LogError("poll failed -> {}", strerror(errno));
                    return EXIT_FAILURE;
                }

                for (usize i = 0; i < owners.Size(); i++)
                    if (fds[i].revents) ProcessOutput(owners[i]);

                if (eventFd < 0 || fds.Back().revents)
                    m_Framebuffer.PollEvents(*this);

                ReapChildren();
                m_Framebuffer.Present();
            }

            return EXIT_SUCCESS;
        }

        // InputListener
        void OnText(StringView text) override
        {
            if (auto session = Active()) session->Pty.Write(text);
        }
        void OnKey(Key key, u32 modifiers) override
        {
            auto session = Active();
            if (!session) return;

            bool       applicationMode = session->Console->GetCursorKeyMode();
            StringView sequence        = "";
            switch (key)
            {
                case Key::eEnter: sequence = "\r"; break;
                case Key::eBackspace: sequence = "\x7f"; break;
                case Key::eTab:
                    sequence = modifiers & KeyModifier::eShift ? "\e[Z" : "\t";
                    break;
                case Key::eEscape: sequence = "\e"; break;
                case Key::eUp: sequence = applicationMode ? "\eOA" : "\e[A"; break;
                case Key::eDown:
                    sequence = applicationMode ? "\eOB" : "\e[B";
                    break;
                case Key::eRight:
                    sequence = applicationMode ? "\eOC" : "\e[C";
                    break;
                case Key::eLeft:
                    sequence = applicationMode ? "\eOD" : "\e[D";
                    break;
                // Same sequences as sent by the Linux console
                case Key::eHome: sequence = "\e[1~"; break;
                case Key::eInsert: sequence = "\e[2~"; break;
                case Key::eDelete: sequence = "\e[3~"; break;
                case Key::eEnd: sequence = "\e[4~"; break;
                case Key::ePageUp: sequence = "\e[5~"; break;
                case Key::ePageDown: sequence = "\e[6~"; break;
                case Key::eF1: sequence = "\e[[A"; break;
                case Key::eF2: sequence = "\e[[B"; break;
                case Key::eF3: sequence = "\e[[C"; break;
                case Key::eF4: sequence = "\e[[D"; break;
                case Key::eF5: sequence = "\e[[E"; break;
                case Key::eF6: sequence = "\e[17~"; break;
                case Key::eF7: sequence = "\e[18~"; break;
                case Key::eF8: sequence = "\e[19~"; break;
                case Key::eF9: sequence = "\e[20~"; break;
                case Key::eF10: sequence = "\e[21~"; break;
                case Key::eF11: sequence = "\e[23~"; break;
                case Key::eF12: sequence = "\e[24~"; break;

                default: return;
            }

            if (modifiers & KeyModifier::eAlt) session->Pty.Write("\e");
            session->Pty.Write(sequence);
        }
        void OnSwitchConsole(usize index) override { SwitchTo(index); }
        void OnQuit() override { m_Running = false; }

      private:
        Framebuffer&      m_Framebuffer;
        const Arguments&  m_Arguments;
        Vector<u8>        m_Background;

        Vector<Session*>  m_Sessions;
        usize             m_ActiveIndex = 0;
        bool              m_Running     = true;

        Session*          Active()
        {
            auto session = m_Sessions[m_ActiveIndex];
            return session && session->Pty.IsAlive() ? session : nullptr;
        }

        Session* CreateSession()
        {
            FramebufferConsole::Options options;
            options.Background = Span<u8>(m_Background.Raw(), m_Background.Size());
            options.FontScale  = m_Arguments.FontScale;

            auto session       = new Session;
            session->Console   = FramebufferConsole::Create(m_Framebuffer, options);
            if (!session->Console)
            {
                LogError("Failed to create the framebuffer console");
                delete session;
                return nullptr;
            }
            session->Console->SetClient(session);

            const char* shell = getenv("QUANTUM_SHELL");
            if (!shell || !*shell) shell = "/bin/sh";

            char* defaultCommand[] = {const_cast<char*>(shell), nullptr};
            char* const* command
                = m_Arguments.Command ? m_Arguments.Command : defaultCommand;

            if (!session->Pty.Spawn(command, session->Console->GetSize()))
            {
                delete session;
                return nullptr;
            }

            auto& size = session->Console->GetSize();
            LogInfo("Console: {}x{} cells", size.ws_col, size.ws_row);
            return session;
        }

        bool SwitchTo(usize index)
        {
            if (index >= m_Sessions.Size()) return false;

            auto& session = m_Sessions[index];
            if (session && !session->Pty.IsAlive())
            {
                delete session;
                session = nullptr;
            }
            if (!session)
            {
                session = CreateSession();
                if (!session) return false;
            }

            if (auto previous = m_Sessions[m_ActiveIndex];
                previous && previous != session)
                previous->Console->SetActive(false);

            m_ActiveIndex = index;
            session->Console->SetActive(true);
            return true;
        }

        void ProcessOutput(Session* session)
        {
            u8 buffer[4096];
            for (;;)
            {
                isize nread = session->Pty.Read(buffer, sizeof(buffer));
                if (nread < 0) break;
                if (nread == 0)
                {
                    OnSessionClosed(session);
                    break;
                }

                session->Console->PrintString(
                    StringView(reinterpret_cast<char*>(buffer), nread));
            }
        }

        void OnSessionClosed(Session* session)
        {
            session->Pty.Close();

            // Switch to any other console which is still alive
            if (session == m_Sessions[m_ActiveIndex])
            {
                for (usize i = 0; i < m_Sessions.Size(); i++)
                {
                    auto other = m_Sessions[i];
                    if (other && other->Pty.IsAlive())
                    {
                        SwitchTo(i);
                        return;
                    }
                }

                // That was the last one
                m_Running = false;
            }
        }

        void ReapChildren()
        {
            while (waitpid(-1, nullptr, WNOHANG) > 0);
        }
    };
} // namespace

int main(int argc, char** argv)
{
    Arguments args;
    if (!ParseArguments(argc, argv, args))
    {
        PrintUsage(argv[0]);
        return EXIT_FAILURE;
    }

    signal(SIGPIPE, SIG_IGN);

    Framebuffer* framebuffer = CreateFramebuffer(args);
    if (!framebuffer || !framebuffer->Initialize())
    {
        delete framebuffer;
        return EXIT_FAILURE;
    }

    i32 status = EXIT_FAILURE;
    {
        Application app(*framebuffer, args);
        if (app.Initialize()) status = app.Run();
    }

    delete framebuffer;
    return status;
}
