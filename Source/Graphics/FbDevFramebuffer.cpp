/*
 * Created by v1tr10l7 on 05.11.2026.
 * Copyright (c) 2024-2026, Szymon Zemke <v1tr10l7@proton.me>
 *
 * SPDX-License-Identifier: GPL-3
 */
#include <Core/Log.hpp>
#include <Graphics/FbDevFramebuffer.hpp>

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/kd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

FbDevFramebuffer::FbDevFramebuffer(StringView devicePath, i32 inputFd)
    : m_DevicePath(devicePath)
    , m_InputFd(inputFd)
{
}
FbDevFramebuffer::~FbDevFramebuffer()
{
    if (m_GraphicsMode) ioctl(m_InputFd, KDSETMODE, KD_TEXT);
    if (m_TermiosSaved) tcsetattr(m_InputFd, TCSAFLUSH, &m_SavedTermios);

    if (m_Address) munmap(m_Address.As<void>(), m_MappingSize);
    if (m_DeviceFd >= 0) close(m_DeviceFd);
}

bool FbDevFramebuffer::Initialize()
{
    m_DeviceFd = open(m_DevicePath.Raw(), O_RDWR | O_CLOEXEC);
    if (m_DeviceFd < 0)
    {
        LogError("FbDev: Failed to open '{}' -> {}", m_DevicePath.Raw(),
                 strerror(errno));
        return false;
    }

    fb_var_screeninfo variableInfo{};
    fb_fix_screeninfo fixedInfo{};
    if (ioctl(m_DeviceFd, FBIOGET_VSCREENINFO, &variableInfo) < 0
        || ioctl(m_DeviceFd, FBIOGET_FSCREENINFO, &fixedInfo) < 0)
    {
        LogError("FbDev: Failed to query screen info -> {}", strerror(errno));
        return false;
    }

    if (variableInfo.bits_per_pixel != 32)
    {
        LogError("FbDev: Unsupported pixel depth -> {}",
                 variableInfo.bits_per_pixel);
        return false;
    }

    m_Width          = variableInfo.xres;
    m_Height         = variableInfo.yres;
    m_Pitch          = fixedInfo.line_length;
    m_BitsPerPixel   = variableInfo.bits_per_pixel;

    RedMaskSize      = variableInfo.red.length;
    RedMaskShift     = variableInfo.red.offset;
    GreenMaskSize    = variableInfo.green.length;
    GreenMaskShift   = variableInfo.green.offset;
    BlueMaskSize     = variableInfo.blue.length;
    BlueMaskShift    = variableInfo.blue.offset;

    m_MappingSize    = fixedInfo.smem_len;
    if (!m_MappingSize) m_MappingSize = m_Pitch * m_Height;

    void* address = mmap(nullptr, m_MappingSize, PROT_READ | PROT_WRITE,
                         MAP_SHARED, m_DeviceFd, 0);
    if (address == MAP_FAILED)
    {
        LogError("FbDev: Failed to map the framebuffer -> {}",
                 strerror(errno));
        return false;
    }
    m_Address = address;

    // Switch the controlling terminal into raw mode, so that every key press
    // is delivered to us immediately and nothing gets echoed by the kernel
    if (isatty(m_InputFd) && tcgetattr(m_InputFd, &m_SavedTermios) == 0)
    {
        m_TermiosSaved = true;

        termios raw    = m_SavedTermios;
        cfmakeraw(&raw);
        raw.c_cc[VMIN]  = 1;
        raw.c_cc[VTIME] = 0;
        tcsetattr(m_InputFd, TCSAFLUSH, &raw);

        // Ask the kernel console to stop drawing, it's fine if it's not
        // supported
        if (ioctl(m_InputFd, KDSETMODE, KD_GRAPHICS) == 0)
            m_GraphicsMode = true;
    }
    else LogWarn("FbDev: Input fd {} is not a terminal", m_InputFd);

    LogInfo("FbDev: Mapped '{}' ({}x{}, pitch: {})", m_DevicePath.Raw(),
            m_Width, m_Height, m_Pitch);
    return true;
}

// Everything is drawn directly into the mapped video memory
void FbDevFramebuffer::Present() { m_Dirty = false; }

void FbDevFramebuffer::PollEvents(InputListener& listener)
{
    char  buffer[256];
    isize nread = read(m_InputFd, buffer, sizeof(buffer));
    if (nread == 0)
    {
        listener.OnQuit();
        return;
    }
    if (nread < 0) return;

    // The kernel already translates key presses into byte sequences
    listener.OnText(StringView(buffer, nread));
}
