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
