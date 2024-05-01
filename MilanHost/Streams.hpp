/**
 * @file        MilanHost/Streams.hpp
 * @header      Synchronous streaming input/output for binary primitives.
 */

#pragma once

#include <span>
#include "Base.hpp"


// ! Common definitions.
// ========================================

enum struct IoMode : int {
    Read = 1,
    ReadWrite = 2,
};

static constexpr auto k_ioRead = IoMode::Read;
static constexpr auto k_ioReadWrite = IoMode::ReadWrite;

enum struct SeekOrigin : int
{
    Begin = 1,
    Current = 2,
    End = 3,
};

static constexpr auto k_seekBegin = SeekOrigin::Begin;
static constexpr auto k_seekCurrent = SeekOrigin::Current;
static constexpr auto k_seekEnd = SeekOrigin::End;


// ! Stream interfaces.
// ========================================

/** Stream interface for reading primitives. */
class IInputStream
{
public:

    virtual             ~IInputStream() noexcept = default;

    virtual INT64       Length() const = 0;
    virtual INT64       Position() const = 0;
    virtual INT64       Seek(SeekOrigin Origin, INT64 Offset) = 0;

    virtual INT64       Read(BYTE* Output, INT64 Length) = 0;

    // Shared implementation.

    virtual INT64       Rewind();
    virtual INT64       Skip(INT64 Forward);

    virtual INT64       Read(std::span<BYTE> Output);
    virtual void        ReadExact(std::span<BYTE> Output);

    template<std::endian Endian, NumericRegularType Type>
    void ReadNumeric(Type& OutValue);

    template<std::endian Endian, NumericRegularType Type>
    void ReadMultiple(Type* OutArray, int Count);

    template<std::endian Endian, NumericRegularType Type>
    void ReadMultiple(std::span<Type> Output);

    template<PlainOldType Type>
    void ReadPod(Type& OutValue);

};

template<std::endian Endian, NumericRegularType Type>
void IInputStream::ReadNumeric(Type& OutValue)
{
    Type Value = {};

    auto const ValueSize = static_cast<INT64>(sizeof Value);
    auto const ReadBytes = this->Read((BYTE*)&Value, ValueSize);

    MILAN_VERIFYW(ReadBytes == ValueSize, L"read %lld byte(s) of %lld expected", ReadBytes, ValueSize);

    if constexpr (Endian != NATIVE)
        Value = Byteswap(Value);

    OutValue = Value;
}

template<std::endian Endian, NumericRegularType Type>
void IInputStream::ReadMultiple(Type* const OutArray, int const Count)
{
    MILAN_CHECKW(OutArray != nullptr, L"");
    MILAN_CHECKW(Count != 0, L"");

    auto const ArraySize = static_cast<INT64>(sizeof(Type)) * Count;
    auto const ReadBytes = this->Read((BYTE*)OutArray, ArraySize);

    MILAN_VERIFYW(ReadBytes == ArraySize, L"read %lld byte(s) of %lld expected", ReadBytes, ArraySize);

    if constexpr (Endian != NATIVE)
    {
        for (int i = 0; i < Count; ++i)
        {
            Type& Value = OutArray[i];
            Value = Byteswap(Value);
        }
    }
}

template<std::endian Endian, NumericRegularType Type>
void IInputStream::ReadMultiple(std::span<Type> const Output)
{
    return this->ReadMultiple<Endian, Type>(Output.data(), static_cast<int>(Output.size()));
}

template<PlainOldType Type>
void IInputStream::ReadPod(Type& OutValue)
{
    auto const ValueSize = static_cast<INT64>(sizeof(Type));
    auto const ReadBytes = this->Read(reinterpret_cast<byte_t*>(&OutValue), ValueSize);
    MILAN_VERIFYW(ReadBytes == ValueSize, L"read %lld byte(s) of %lld expected", ReadBytes, ValueSize);
}
