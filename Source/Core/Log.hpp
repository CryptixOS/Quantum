/*
 * Created by v1tr10l7 on 05.11.2026.
 * Copyright (c) 2024-2026, Szymon Zemke <v1tr10l7@proton.me>
 *
 * SPDX-License-Identifier: GPL-3
 */
#pragma once

#include <Prism/Core/Types.hpp>

#include <cstdio>
#include <cstdlib>
#include <fmt/format.h>

namespace Quantum::Log
{
    enum class Level
    {
        eTrace,
        eInfo,
        eWarn,
        eError,
    };

    inline Level& MinimumLevel()
    {
        static Level s_Level = Level::eInfo;
        return s_Level;
    }

    template <typename... Args>
    inline void Print(Level level, fmt::format_string<Args...> format,
                      Args&&... args)
    {
        if (level < MinimumLevel()) return;

        const char* prefix = "";
        switch (level)
        {
            case Level::eTrace: prefix = "\e[35mTrace\e[0m"; break;
            case Level::eInfo: prefix = "\e[36mInfo\e[0m"; break;
            case Level::eWarn: prefix = "\e[33mWarn\e[0m"; break;
            case Level::eError: prefix = "\e[31mError\e[0m"; break;
        }

        auto message = fmt::format(format, static_cast<Args&&>(args)...);
        std::fprintf(stderr, "quantum-term: [%s]: %s\n", prefix,
                     message.c_str());
    }
} // namespace Quantum::Log

#define LogTrace(...) ::Quantum::Log::Print(::Quantum::Log::Level::eTrace, __VA_ARGS__)
#define LogInfo(...)  ::Quantum::Log::Print(::Quantum::Log::Level::eInfo, __VA_ARGS__)
#define LogWarn(...)  ::Quantum::Log::Print(::Quantum::Log::Level::eWarn, __VA_ARGS__)
#define LogError(...) ::Quantum::Log::Print(::Quantum::Log::Level::eError, __VA_ARGS__)

#ifndef Assert
    #define Assert(expr)                                                       \
        do {                                                                   \
            if (!(expr))                                                       \
            {                                                                  \
                std::fprintf(stderr, "quantum-term: assertion failed: %s (%s:%d)\n", \
                             #expr, __FILE__, __LINE__);                       \
                std::abort();                                                  \
            }                                                                  \
        } while (0)
#endif
