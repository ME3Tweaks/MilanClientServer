#include "Streams.hpp"


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
