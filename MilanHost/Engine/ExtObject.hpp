/**
 * @file        MilanHost/Engine/ExtObject.hpp
 * @brief       Utilities for UObject and children which don't make it into diffs.
 */

#pragma once
#include "Base/Base.hpp"


/**
 * @brief   Unreal-style iterator filtering objects by a specific type.
 * @tparam  T - A type derived from @c UObject of which all filtered objects must be.
 * @tparam  bAllowDerived - Whether filtered objects are allowed to have types derived from the given type.
 * @tparam  bIncludeDefaults - Whether filtered objects are allowed to be Class Default Objects.
 */
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


/**
 * @brief   Collects keys of a @c TMap into a @c TArray.
 * @tparam  TKey - Map key type.
 * @tparam  TValue - Map value type.
 * @param   Map - Input map.
 * @return  An array of map keys.
 *
 * @remark  This only sits in @c ExtObject.hpp because I don't have a better place for it!!!!
 */
template<typename TKey, typename TValue>
inline TArray<TKey> CollectMapKeys(TMap<TKey, TValue>& Map)
{
    TArray<TKey> Out{};
    for (auto const& [Key, Value] : Map)
    {
        Out.Add(Key);
        MILAN_UNUSED(Value);
    }
    return Out;
}


/** Some utilities for AActor and children. */
namespace ActorUtils
{
    /** Retrieves the global world info instance the same way Unreal does. */
    AWorldInfo* GlobalWorldInfo() noexcept;
    /** Checks if the global world info say it has begun gameplay. */
    bool GlobalBegunPlay() noexcept;
}
