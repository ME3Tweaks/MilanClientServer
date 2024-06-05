/**
 * @file        MilanHost/Milan/Buffers.hpp
 * @brief       Reusable memory buffers.
 */

#pragma once

#include <span>

#include "Milan/Base.hpp"
#include "Containers/vector.hpp"


/** Seekable binary buffer that owns its bytes. */
struct MemoryBuffer
{
    game::vector<BYTE>      Buffer{};
    std::size_t             Offset{};

    MemoryBuffer() = default;
    explicit MemoryBuffer(std::size_t Capacity);
    explicit MemoryBuffer(std::span<BYTE> Init);

    INT64 Length() const { return static_cast<INT64>(Buffer.size()); }
    INT64 Position() const { return static_cast<INT64>(Offset); };

    INT64 Seek(SeekOrigin InOrigin, INT64 InOffset);

    INT64 Advance(INT64 Forward);
    std::span<BYTE> Remaining();
};

/** Seekable binary buffer that views bytes owned externally. */
struct WeakBuffer
{
    std::span<BYTE const>   Buffer{};
    std::size_t             Offset{};

    explicit WeakBuffer(std::span<BYTE const> Data);

    INT64 Length() const { return static_cast<INT64>(Buffer.size()); }
    INT64 Position() const { return static_cast<INT64>(Offset); }

    INT64 Seek(SeekOrigin InOrigin, INT64 InOffset);

    INT64 Advance(INT64 Forward);
    std::span<BYTE const> Remaining();
};
