/*
 * Created by v1tr10l7 on 05.11.2026.
 * Copyright (c) 2024-2026, Szymon Zemke <v1tr10l7@proton.me>
 *
 * SPDX-License-Identifier: GPL-3
 */
#pragma once

#include <Graphics/Framebuffer.hpp>

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;

// Development backend, emulates the framebuffer inside of an SDL window, so
// the terminal can be tested on the host
class SDLFramebuffer final : public Framebuffer
{
  public:
    SDLFramebuffer(usize width = 1280, usize height = 800);
    ~SDLFramebuffer() override;

    bool       Initialize() override;
    StringView Name() const override { return "sdl"; }

    void       Present() override;
    void       PollEvents(InputListener& listener) override;

    void        SetClipboard(ClipboardKind kind, StringView text) override;
    std::string GetClipboard(ClipboardKind kind) override;

  private:
    SDL_Window*   m_Window   = nullptr;
    SDL_Renderer* m_Renderer = nullptr;
    SDL_Texture*  m_Texture  = nullptr;
    u32*          m_Pixels   = nullptr;
};
