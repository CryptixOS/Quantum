/*
 * Created by v1tr10l7 on 05.11.2026.
 * Copyright (c) 2024-2026, Szymon Zemke <v1tr10l7@proton.me>
 *
 * SPDX-License-Identifier: GPL-3
 */
#pragma once

#include <Graphics/Framebuffer.hpp>

#include <termios.h>

// Native backend, draws directly into the memory of a Linux compatible fbdev
// device (/dev/fbN), keyboard input is read from the controlling terminal
// (the kernel's virtual console) which is switched into raw mode
class FbDevFramebuffer final : public Framebuffer
{
  public:
    explicit FbDevFramebuffer(StringView devicePath = "/dev/fb0",
                              i32        inputFd    = 0);
    ~FbDevFramebuffer() override;

    bool       Initialize() override;
    StringView Name() const override { return "fbdev"; }

    void       Present() override;

    i32        EventFd() const override { return m_InputFd; }
    void       PollEvents(InputListener& listener) override;

  private:
    StringView m_DevicePath;
    i32        m_DeviceFd      = -1;
    usize      m_MappingSize   = 0;

    i32        m_InputFd       = 0;
    bool       m_TermiosSaved  = false;
    termios    m_SavedTermios  = {};
    bool       m_GraphicsMode  = false;
};
