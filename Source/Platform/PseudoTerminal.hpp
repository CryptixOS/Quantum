/*
 * Created by v1tr10l7 on 05.11.2026.
 * Copyright (c) 2024-2026, Szymon Zemke <v1tr10l7@proton.me>
 *
 * SPDX-License-Identifier: GPL-3
 */
#pragma once

#include <Core/Prism.hpp>

#include <Prism/String/StringView.hpp>

#include <sys/ioctl.h>
#include <sys/types.h>

// Master side of a pseudo terminal, with a child process attached to the
// slave side
class PseudoTerminal
{
  public:
    PseudoTerminal() = default;
    ~PseudoTerminal();

    PseudoTerminal(const PseudoTerminal&)            = delete;
    PseudoTerminal& operator=(const PseudoTerminal&) = delete;

    // Opens a new pty pair, and executes @argv in a new session with the
    // slave side as its controlling terminal
    bool            Spawn(char* const* argv, const winsize& size);

    inline i32      MasterFd() const { return m_MasterFd; }
    inline pid_t    ChildPid() const { return m_ChildPid; }
    inline bool     IsAlive() const { return m_MasterFd >= 0; }

    // Returns the number of bytes read, 0 on hangup
    isize           Read(u8* buffer, usize size);
    void            Write(StringView data);

    void            Resize(const winsize& size);
    void            Close();

  private:
    i32   m_MasterFd = -1;
    pid_t m_ChildPid = -1;
};
