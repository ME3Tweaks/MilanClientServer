/**
 * @file        MilanHost/Milan/Misc.hpp
 * @header      Miscellaneous utilities.
 */

#pragma once

#include "Containers/string.hpp"
#include "Containers/vector.hpp"


int GetGameInstanceOrdinal();


class RelativeTimer final
{
    long long Frequency{};
    long long CounterStart{};

public:

    RelativeTimer();
    float GetSeconds() const;
    float GetMilliseconds() const;
};


// ! Full file input / output utilities.
// ========================================

void ReadFileFull(std::wstring_view InPath, ::game::vector<byte>& OutBuffer);
void WriteFileFull(std::wstring_view InPath, std::span<byte const> InBuffer);


// ! Other output utilities.
// ========================================

inline FString FormatGuid(FGuid& In)
{
    return FString::Printf(L"%08X%08X%08X%08X", In.A, In.B, In.C, In.D);
}

inline FString FormatGuid(FGuid const& In)
{
    return FString::Printf(L"%08X%08X%08X%08X", In.A, In.B, In.C, In.D);
}

inline bool operator==(FGuid& A, FGuid& B) { return A.A == B.A && A.B == B.B && A.C == B.C && A.D == B.D; }
inline bool operator!=(FGuid& A, FGuid& B) { return !(A == B); }

inline bool operator==(FGuid const& A, FGuid const& B) { return A.A == B.A && A.B == B.B && A.C == B.C && A.D == B.D; }
inline bool operator!=(FGuid const& A, FGuid const& B) { return !(A == B); }


template<SIZE_T HEXDUMP_LINE_LEN = 16u>
    requires (HEXDUMP_LINE_LEN >= 4 && HEXDUMP_LINE_LEN <= 32)
void PrintHex(FILE* const OutStream, BYTE* const InBuffer, SIZE_T const InSize, char const* const InPrefix)
{
    SIZE_T Line = 0;
    for (auto i = 0u; i < InSize; i += HEXDUMP_LINE_LEN)
    {
        Line++;

        SIZE_T MaxLen = HEXDUMP_LINE_LEN;
        if (Line * HEXDUMP_LINE_LEN > InSize) {
            MaxLen = InSize - HEXDUMP_LINE_LEN * (Line - 1);
        }

        // Address
        fprintf_s(OutStream, "%s%08X  ", InPrefix, i);

        // Hex bytes
        for (auto j = 0u; j < MaxLen; j++)
        {
            auto const c = static_cast<BYTE>(InBuffer[i + j]);
            fprintf_s(OutStream, "%02hhX ", c);
        }

        // Padding
        for (int j = 0u; j < HEXDUMP_LINE_LEN - MaxLen + 1; j++)
        {
            fprintf_s(OutStream, "   ");
        }

        // Characters
        for (int j = 0u; j < MaxLen; j++)
        {
            auto const l = static_cast<BYTE>(InBuffer[i + j]);
            fprintf_s(OutStream, "%c", (l >= 0x20) ? l : '.');
        }

        // Newline
        fprintf_s(OutStream, "\n");
    }
}

__declspec(noinline) void WriteCallstackLog(CodePlaceW&& Place, ESeverity Severity, int Skip = 0);


// ! UObject & AActor utilities.
// ========================================

template<std::derived_from<UObject> T, bool bAllowDerived = true, bool bIncludeDefaults = true>
class TypedObjectIterator final : public NonCopyable
{
    UObject** Current{ nullptr };

    void SkipToNext()
    {
        UObject** const IterEnd = UObject::GObjObjects->GetData()
            + UObject::GObjObjects->Count();

        if constexpr (bAllowDerived)
        {
            while (++Current <= IterEnd)
            {
                if (*Current && (*Current)->Cast<T>())
                {
                    if (!bIncludeDefaults && (*Current)->StaticName().StartsWith(L"Default__"))
                    {
                        continue;
                    }

                    break;
                }
            }
        }
        else
        {
            while (++Current <= IterEnd)
            {
                if (*Current && (*Current)->CastDirect<T>())
                {
                    if (!bIncludeDefaults && (*Current)->StaticName().StartsWith(L"Default__"))
                    {
                        continue;
                    }

                    break;
                }
            }
        }
    }

public:

    TypedObjectIterator()
    {
        Current = UObject::GObjObjects->GetData();
        Current--;
        SkipToNext();
    }

    operator bool() const
    {
        UObject** const IterEnd = UObject::GObjObjects->GetData()
            + UObject::GObjObjects->Count();
        return Current < IterEnd;
    }

    T* operator*() const
    {
        LESDK_CHECK(*this, "invalid iterator");

        using BareType = std::remove_cvref_t<std::remove_pointer_t<T>>;
        return reinterpret_cast<BareType*>(*Current);
    }

    TypedObjectIterator& operator++()
    {
        SkipToNext();
        return *this;
    }
};
