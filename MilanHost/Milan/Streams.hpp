/**
 * @file        MilanHost/Milan/Streams.hpp
 * @header      Synchronous streaming input/output for binary primitives.
 */

#pragma once

#include <cstdio>
#include <span>

#include "Containers/string.hpp"
#include "Containers/vector.hpp"
#include "Milan/Base.hpp"
#include "Milan/Buffers.hpp"


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

    template<bool bWithRaii>
    void ReadString(FStringBase<bWithRaii>& OutValue);

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
    MILAN_CHECKW(Count > 0, L"");

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
    this->ReadMultiple<Endian, Type>(Output.data(), static_cast<int>(Output.size()));
}

template<PlainOldType Type>
void IInputStream::ReadPod(Type& OutValue)
{
    auto const ValueSize = static_cast<INT64>(sizeof(Type));
    auto const ReadBytes = this->Read(reinterpret_cast<byte_t*>(&OutValue), ValueSize);
    MILAN_VERIFYW(ReadBytes == ValueSize, L"read %lld byte(s) of %lld expected", ReadBytes, ValueSize);
}

template<bool bWithRaii>
void IInputStream::ReadString(FStringBase<bWithRaii>& OutValue)
{
    // Holds the number of *characters*.
    // Includes the null-terminator if present.
    INT32 Length = 0;
    this->ReadNumeric<BIG>(Length);

    if (Length == 0)
    {
        OutValue.Reset();
    }
    else if (Length < 0)
    {
        Length = -Length;

        ::game::wstring Temporary(static_cast<size_t>(Length), L'\0');
        this->ReadExact(std::span((BYTE*)Temporary.data(), Temporary.size() * 2));

        MILAN_VERIFYW(Temporary.back() == L'\0',
            L"expected last char to be null-terminator, found %d",
            static_cast<int>(Temporary.back()));

        OutValue.Reset();
        OutValue.Append(Temporary.begin(), Temporary.end() - 1);
    }
    else
    {
        MILAN_FATALW(L"only unicode strings are supported for serialization");
    }
}


/** Stream interface for writing primitives. */
class IOutputStream
{
public:

    virtual             ~IOutputStream() noexcept = default;

    virtual INT64       Length() const = 0;
    virtual INT64       Position() const = 0;
    virtual INT64       Seek(SeekOrigin Origin, INT64 Offset) = 0;

    virtual INT64       Write(BYTE const* Input, INT64 Length) = 0;

    // Shared implementation.

    virtual void        Flush();

    virtual INT64       Rewind();
    virtual INT64       Skip(INT64 Forward);

    virtual INT64       Write(std::span<BYTE const> Input);
    virtual void        WriteExact(std::span<BYTE const> Input);

    template<std::endian Endian, NumericRegularType Type>
    void WriteNumeric(Type InValue);

    template<std::endian Endian, NumericRegularType Type>
    void WriteMultiple(Type const* InArray, int Count);

    template<std::endian Endian, NumericRegularType Type>
    void WriteMultiple(std::span<Type const> Input);

    template<PlainOldType Type>
    void WritePod(Type& Value);

    template<bool bWithRaii>
    void WriteString(FStringBase<bWithRaii> const& InValue);

};

template<std::endian Endian, NumericRegularType Type>
void IOutputStream::WriteNumeric(Type InValue)
{
    if constexpr (Endian != NATIVE)
        InValue = Byteswap(InValue);

    auto const ValueSize = static_cast<INT64>(sizeof InValue);
    auto const WrittenBytes = this->Write((BYTE const*)&InValue, ValueSize);
    MILAN_VERIFYW(WrittenBytes == ValueSize, L"wrote %lld byte(s) of %lld expected", WrittenBytes, ValueSize);
}

template<std::endian Endian, NumericRegularType Type>
void IOutputStream::WriteMultiple(Type const* const InArray, int const Count)
{
    MILAN_CHECKW(InArray != nullptr, L"");
    MILAN_CHECKW(Count > 0, L"");

    auto const ValueSize = static_cast<INT64>(sizeof(Type));
    auto const ArraySize = ValueSize * Count;

    // Since manipulating InArray in-place is undesirable,
    // all of its elements will need to be byteswapped one by one.

    INT64 WrittenBytes = 0;

    for (int i = 0; i < Count; ++i)
    {
        Type Value = InArray[i];
        if constexpr (Endian != NATIVE)
            Value = Byteswap(Value);

        auto const IterWrittenBytes = this->Write((BYTE const*)&Value, ValueSize);
        WrittenBytes += IterWrittenBytes;

        if (IterWrittenBytes != ValueSize)
            break;
    }

    MILAN_VERIFYW(WrittenBytes == ArraySize, L"wrote %lld byte(s) of %lld expected", WrittenBytes, ArraySize);
}

template<std::endian Endian, NumericRegularType Type>
void IOutputStream::WriteMultiple(std::span<Type const> const Input)
{
    this->WriteMultiple(Input.data(), static_cast<int>(Input.size()));
}

template<PlainOldType Type>
void IOutputStream::WritePod(Type& Value)
{
    auto const ValueSize = static_cast<INT64>(sizeof(Type));
    auto const WrittenBytes = this->Write(reinterpret_cast<BYTE*>(&Value), ValueSize);
    MILAN_VERIFYW(WrittenBytes == ValueSize, L"wrote %lld byte(s) of %lld expected", WrittenBytes, ValueSize);
}

template<bool bWithRaii>
void IOutputStream::WriteString(FStringBase<bWithRaii> const& InValue)
{
    if (InValue.Empty())
    {
        this->WriteNumeric<BIG, INT32>(0);
    }
    else
    {
        // Always serialize the null-terminator.
        INT32 const Length = static_cast<INT32>(InValue.Length() + 1);
        std::span<BYTE> const Bytes{ (BYTE*)InValue.Chars(), static_cast<size_t>(Length * 2) };

        // Always serialize as unicode.
        this->WriteNumeric<BIG, INT32>(Length * -1);
        this->WriteExact(Bytes);
    }
}


// ! File-backed streams.
// ========================================

class BufferedFileOutputStream final
    : public IOutputStream, public NonCopyable
{
    size_t          m_size;
    MemoryBuffer    m_buffer;
    std::FILE*      mp_file;

public:

    // IOutputStream implementation.

    ~BufferedFileOutputStream() noexcept override;

    INT64 Length() const override;
    INT64 Position() const override;
    INT64 Seek(SeekOrigin Origin, INT64 Offset) override;
    INT64 Write(BYTE const* Input, INT64 Length) override;
    void  Flush() override;

    // Own interface.

    static constexpr size_t k_defaultBufferSize = 64 * 1024;  // 64 kb
    explicit BufferedFileOutputStream(std::string_view Path, size_t Buffer = k_defaultBufferSize);

};


// ! Memory-backed streams.
// ========================================

/** An @ref IInputStream backed by an internal fixed-size buffer. */
class MemoryInputStream final
    : public IInputStream, public NonCopyable
{
    MemoryBuffer m_buffer;

public:

    // IInputStream implementation.

    ~MemoryInputStream() noexcept override = default;

    INT64 Length() const override;
    INT64 Position() const override;
    INT64 Seek(SeekOrigin Origin, INT64 Offset) override;
    INT64 Read(BYTE* Output, INT64 Length) override;

    // Own interface.

    explicit MemoryInputStream(std::span<BYTE> Data);
    explicit MemoryInputStream(game::vector<BYTE>&& Buffer);
    explicit MemoryInputStream(IInputStream& Source);

    MemoryInputStream(MemoryInputStream&& Other) noexcept;
    MemoryInputStream& operator=(MemoryInputStream&& Other) noexcept;

    std::span<BYTE const> View() const noexcept
    {
        return std::span{ m_buffer.Buffer };
    }
};

/** An @ref IOutputStream backed by an internal fixed-size buffer. */
class MemoryOutputStream final
    : public IOutputStream, public NonCopyable
{
    MemoryBuffer m_buffer;

public:

    // IOutputStream implementation.

    ~MemoryOutputStream() noexcept override = default;

    INT64 Length() const override;
    INT64 Position() const override;
    INT64 Seek(SeekOrigin Origin, INT64 Offset) override;
    INT64 Write(BYTE const* Input, INT64 Length) override;

    // Own interface.

    MemoryOutputStream() = default;
    explicit MemoryOutputStream(std::size_t Capacity);

    void Reserve(std::size_t Capacity);

    MemoryOutputStream(MemoryOutputStream&& Other) noexcept;
    MemoryOutputStream& operator=(MemoryOutputStream&& Other) noexcept;

    std::span<BYTE const> View() const noexcept
    {
        return std::span{ m_buffer.Buffer };
    }
};

/** An @ref IInputStream backed by an external fixed-size buffer. */
class WeakInputStream final : public IInputStream
{
    WeakBuffer m_buffer;

public:

    // IInputStream implementation.

    ~WeakInputStream() noexcept override = default;

    INT64 Length() const override;
    INT64 Position() const override;
    INT64 Seek(SeekOrigin Origin, INT64 Offset) override;
    INT64 Read(BYTE* Output, INT64 Length) override;

    // Own interface.

    explicit WeakInputStream(std::span<BYTE const> Data);

    std::span<BYTE const> View() const noexcept {
        return m_buffer.Buffer;
    }
};
