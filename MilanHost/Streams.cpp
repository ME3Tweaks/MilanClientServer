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
