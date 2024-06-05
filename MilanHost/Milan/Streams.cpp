#include "Milan/Streams.hpp"


// ! IInputStream implementation.
// ========================================

INT64 IInputStream::Rewind()
{
    return this->Seek(SeekOrigin::Begin, 0);
}

INT64 IInputStream::Skip(INT64 const Forward)
{
    MILAN_VERIFYW(Forward >= 0, L"skipping backwards is unsupported");
    return this->Seek(SeekOrigin::Current, Forward);
}

INT64 IInputStream::Read(std::span<BYTE> const Output)
{
    return this->Read(Output.data(), static_cast<INT64>(Output.size()));
}

void IInputStream::ReadExact(std::span<BYTE> const Output)
{
    auto const ReadBytes = this->Read(Output);
    auto const ExactSize = static_cast<INT64>(Output.size());
    MILAN_VERIFYW(ReadBytes == ExactSize, L"read %lld byte(s) of %lld expected", ReadBytes, ExactSize);
}


// ! IOutputStream implementation.
// ========================================

INT64 IOutputStream::Rewind()
{
    return this->Seek(SeekOrigin::Begin, 0);
}

INT64 IOutputStream::Skip(INT64 const Forward)
{
    MILAN_VERIFYW(Forward >= 0, L"skipping backwards is unsupported");
    return this->Seek(SeekOrigin::Current, Forward);
}

INT64 IOutputStream::Write(std::span<BYTE const> const Input)
{
    return this->Write(Input.data(), static_cast<INT64>(Input.size()));
}

void IOutputStream::WriteExact(std::span<BYTE const> const Input)
{
    auto const WrittenBytes = this->Write(Input);
    auto const InputBytes = static_cast<INT64>(Input.size());
    MILAN_VERIFYW(WrittenBytes == InputBytes, L"wrote %lld byte(s) of %lld expected", WrittenBytes, InputBytes);
}


// ! MemoryInputStream implementation.
// ========================================

INT64 MemoryInputStream::Length() const
{
    return m_buffer.Length();
}

INT64 MemoryInputStream::Position() const
{
    return m_buffer.Position();
}

INT64 MemoryInputStream::Seek(SeekOrigin const Origin, INT64 const Offset)
{
    return m_buffer.Seek(Origin, Offset);
}

INT64 MemoryInputStream::Read(BYTE* const Output, INT64 const Length)
{
    MILAN_CHECKW(Output != nullptr, L"");
    MILAN_CHECKW(Length > 0, L"");

    std::span<BYTE> const Remaining = m_buffer.Remaining();
    INT64 const Copied = std::min(Length, static_cast<INT64>(Remaining.size()));

    std::copy_n(Remaining.begin(), Copied, Output);
    return m_buffer.Advance(Copied);
}

MemoryInputStream::MemoryInputStream(std::span<BYTE> const Data)
    : m_buffer{ Data } {}

MemoryInputStream::MemoryInputStream(game::vector<BYTE>&& Buffer)
    : m_buffer{ }
{
    MILAN_CHECKW(!Buffer.empty(), L"");
    m_buffer.Buffer = std::move(Buffer);
}

MemoryInputStream::MemoryInputStream(IInputStream& Source)
    : m_buffer{}
{
    auto const SourceLength = static_cast<std::size_t>(Source.Length());
    m_buffer.Buffer.resize(SourceLength, 0u);
    Source.ReadExact({ m_buffer.Buffer });
    MILAN_CHECKW(Length() == Source.Length(), L"");
}

MemoryInputStream::MemoryInputStream(MemoryInputStream&& Other) noexcept
    : m_buffer{ std::move(Other.m_buffer) } {}

MemoryInputStream& MemoryInputStream::operator=(MemoryInputStream&& Other) noexcept
{
    if (this != &Other)
        m_buffer = std::move(Other.m_buffer);

    return *this;
}


// ! MemoryOutputStream implementation.
// ========================================

INT64 MemoryOutputStream::Length() const
{
    return m_buffer.Length();
}

INT64 MemoryOutputStream::Position() const
{
    return m_buffer.Position();
}

INT64 MemoryOutputStream::Seek(SeekOrigin const Origin, INT64 const Offset)
{
    return m_buffer.Seek(Origin, Offset);
}

INT64 MemoryOutputStream::Write(BYTE const* const Input, INT64 const Length)
{
    MILAN_CHECKW(Input != nullptr, L"");
    MILAN_CHECKW(Length > 0, L"");
    MILAN_CHECKW(m_buffer.Position() <= m_buffer.Length(), L"");

    // Ensure the buffer is big enough for our overwrite.
    auto const WriteEndPosition = m_buffer.Position() + Length;
    if (WriteEndPosition > m_buffer.Length())
        m_buffer.Buffer.resize(static_cast<size_t>(WriteEndPosition));

    // Insert, or possibly overwrite the buffer.
    auto const WriteBegin = m_buffer.Remaining().begin();
    std::copy(Input, Input + Length, WriteBegin);

    return m_buffer.Advance(Length);
}

MemoryOutputStream::MemoryOutputStream(std::size_t const Capacity)
    : m_buffer{ Capacity } {}

MemoryOutputStream::MemoryOutputStream(MemoryOutputStream&& Other) noexcept
    : m_buffer{ std::move(Other.m_buffer) } {}

MemoryOutputStream& MemoryOutputStream::operator=(MemoryOutputStream&& Other) noexcept
{
    if (this != &Other)
        m_buffer = std::move(Other.m_buffer);

    return *this;
}

void MemoryOutputStream::Reserve(std::size_t const Capacity)
{
    m_buffer.Buffer.reserve(Capacity);
}


// ! WeakInputStream implementation.
// ========================================

INT64 WeakInputStream::Length() const
{
    return m_buffer.Length();
}

INT64 WeakInputStream::Position() const
{
    return m_buffer.Position();
}

INT64 WeakInputStream::Seek(SeekOrigin const Origin, INT64 const Offset)
{
    return m_buffer.Seek(Origin, Offset);
}

INT64 WeakInputStream::Read(BYTE* const Output, INT64 const Length) {
    MILAN_CHECKW(Output != nullptr, L"");
    MILAN_CHECKW(Length > 0, L"");

    auto const Remaining = m_buffer.Remaining();
    auto const Copied = std::min(Length, static_cast<INT64>(Remaining.size()));

    std::copy_n(Remaining.begin(), Copied, Output);
    return m_buffer.Advance(Copied);
}

WeakInputStream::WeakInputStream(std::span<BYTE const> const Data)
    : m_buffer{ Data }
{
    MILAN_CHECKW(!Data.empty(), L"");
}






void _Test_IInputStream_Builds_(IInputStream& Stream)
{
    FString EngineString{};
    Stream.ReadString(EngineString);
}

void _Test_IOutputStream_Builds_(IOutputStream& Stream)
{
    FString EngineString{};
    Stream.WriteString(EngineString);
}
