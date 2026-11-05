/*
 * Created by v1tr10l7 on 05.11.2026.
 * Copyright (c) 2024-2026, Szymon Zemke <v1tr10l7@proton.me>
 *
 * SPDX-License-Identifier: GPL-3
 */
#include <Core/Log.hpp>
#include <Platform/PseudoTerminal.hpp>

#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <pty.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

PseudoTerminal::~PseudoTerminal() { Close(); }

bool PseudoTerminal::Spawn(char* const* argv, const winsize& size)
{
    i32     masterFd = -1;
    i32     slaveFd  = -1;
    winsize ws       = size;

    if (openpty(&masterFd, &slaveFd, nullptr, nullptr, &ws) < 0)
    {
        LogError("PTY: Failed to open a pseudo terminal -> {}",
                 strerror(errno));
        return false;
    }

    pid_t pid = fork();
    if (pid < 0)
    {
        LogError("PTY: Failed to fork -> {}", strerror(errno));
        close(masterFd);
        close(slaveFd);
        return false;
    }

    if (pid == 0)
    {
        close(masterFd);

        setsid();
        ioctl(slaveFd, TIOCSCTTY, 0);

        dup2(slaveFd, STDIN_FILENO);
        dup2(slaveFd, STDOUT_FILENO);
        dup2(slaveFd, STDERR_FILENO);
        if (slaveFd > STDERR_FILENO) close(slaveFd);

        // We emulate the Linux console
        setenv("TERM", "linux", 1);
        unsetenv("COLUMNS");
        unsetenv("LINES");

        signal(SIGCHLD, SIG_DFL);
        signal(SIGPIPE, SIG_DFL);

        execvp(argv[0], argv);

        std::fprintf(stderr, "quantum-term: failed to execute '%s': %s\n",
                     argv[0], strerror(errno));
        _exit(127);
    }

    close(slaveFd);
    fcntl(masterFd, F_SETFD, FD_CLOEXEC);
    fcntl(masterFd, F_SETFL, fcntl(masterFd, F_GETFL) | O_NONBLOCK);

    m_MasterFd = masterFd;
    m_ChildPid = pid;

    LogInfo("PTY: Spawned '{}' (pid: {})", argv[0], pid);
    return true;
}

isize PseudoTerminal::Read(u8* buffer, usize size)
{
    if (m_MasterFd < 0) return 0;

    isize nread = read(m_MasterFd, buffer, size);
    if (nread < 0 && (errno == EAGAIN || errno == EINTR)) return -1;

    // EIO is reported once the slave side gets closed
    return nread < 0 ? 0 : nread;
}
void PseudoTerminal::Write(StringView data)
{
    if (m_MasterFd < 0) return;

    usize offset = 0;
    while (offset < data.Size())
    {
        isize nwritten
            = write(m_MasterFd, data.Raw() + offset, data.Size() - offset);
        if (nwritten < 0)
        {
            if (errno == EINTR) continue;
            if (errno == EAGAIN)
            {
                // The client is not keeping up, wait for it
                fd_set set;
                FD_ZERO(&set);
                FD_SET(m_MasterFd, &set);
                select(m_MasterFd + 1, nullptr, &set, nullptr, nullptr);
                continue;
            }

            break;
        }

        offset += nwritten;
    }
}

void PseudoTerminal::Resize(const winsize& size)
{
    if (m_MasterFd < 0) return;
    ioctl(m_MasterFd, TIOCSWINSZ, &size);
}
void PseudoTerminal::Close()
{
    if (m_MasterFd >= 0) close(m_MasterFd);
    m_MasterFd = -1;

    if (m_ChildPid > 0)
    {
        kill(m_ChildPid, SIGHUP);
        waitpid(m_ChildPid, nullptr, WNOHANG);
    }
    m_ChildPid = -1;
}
