/*
 * Created by v1tr10l7 on 05.11.2026.
 * Copyright (c) 2025-2026, Szymon Zemke <v1tr10l7@proton.me>
 *
 * SPDX-License-Identifier: GPL-3
 */
#pragma once

#include <Graphics/Framebuffer.hpp>

// Headless framebuffer, so the console can be exercised without a display
class MemoryFramebuffer final : public Framebuffer
{
  public:
    MemoryFramebuffer(usize width, usize height)
    {
        m_Width   = width;
        m_Height  = height;
        m_Pitch   = width * sizeof(u32);
        m_Pixels  = new u32[width * height]{};
        m_Address = m_Pixels;
    }
    ~MemoryFramebuffer() override { delete[] m_Pixels; }

    bool       Initialize() override { return true; }
    StringView Name() const override { return "memory"; }
    void       Present() override {}
    void       PollEvents(InputListener&) override {}

  private:
    u32* m_Pixels = nullptr;
};
