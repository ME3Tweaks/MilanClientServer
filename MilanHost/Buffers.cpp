#include "Buffers.hpp"


// ! MemoryBuffer implementation.
// ========================================

MemoryBuffer::MemoryBuffer(std::size_t const Capacity)
{
    Buffer.reserve(Capacity);
}

MemoryBuffer::MemoryBuffer(std::span<BYTE> const Init)
{
    Buffer.assign(Init.begin(), Init.end());
}

INT64 MemoryBuffer::Seek(SeekOrigin const InOrigin, INT64 const InOffset)
{
    std::size_t Reference = 0;

    switch (InOrigin)
    {
    case SeekOrigin::Current:
        Reference = Offset;
        break;
    case SeekOrigin::End:
        Reference = Buffer.size();
        break;
    }

    MILAN_VERIFYW(Reference + InOffset < Buffer.size(), L"can't seek beyond buffer bounds");
    Offset = static_cast<std::size_t>(Reference + InOffset);

    return static_cast<INT64>(Offset);
}

INT64 MemoryBuffer::Advance(INT64 const Forward)
{
    MILAN_VERIFYW(Offset + Forward <= Buffer.size(), L"can't seek beyond buffer bounds");
    Offset += Forward;
    return Forward;
}

std::span<BYTE> MemoryBuffer::Remaining()
{
    MILAN_VERIFYW(Offset <= Buffer.size(), L"");
    return std::span<BYTE>{ Buffer }.subspan(Offset);
}


// ! WeakBuffer implementation.
// ========================================

WeakBuffer::WeakBuffer(std::span<BYTE const> const Data)
    : Buffer{ Data }
{
    MILAN_VERIFYW(Buffer.data() != nullptr, L"");
}

INT64 WeakBuffer::Seek(SeekOrigin const InOrigin, INT64 const InOffset)
{
    std::size_t Reference = 0;

    switch (InOrigin)
    {
    case SeekOrigin::Current:
        Reference = Offset;
        break;
    case SeekOrigin::End:
        Reference = Buffer.size();
        break;
    }

    MILAN_VERIFYW(Reference + InOffset < Buffer.size(), L"can't seek beyond buffer bounds");
    Offset = static_cast<std::size_t>(Reference + InOffset);

    return static_cast<INT64>(Offset);
}

INT64 WeakBuffer::Advance(INT64 const Forward)
{
    MILAN_VERIFYW(Offset + Forward <= Buffer.size(), L"can't seek beyond buffer bounds");
    Offset += Forward;
    return Forward;
}

std::span<BYTE const> WeakBuffer::Remaining()
{
    MILAN_VERIFYW(Offset <= Buffer.size(), L"");
    return std::span<BYTE const>{ Buffer }.subspan(Offset);
}
