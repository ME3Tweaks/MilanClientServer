/**
 * @file        MilanHost/Engine/PcBindingPolicy.hpp
 * @brief       Pure logic for PC binding menu, reset and profile handling.
 */

#pragma once

#include "InputPromptPolicy.hpp"
#include <array>
#include <utility>
#include <vector>
#include <type_traits>

#define GLOBAL_COLLECTBINDINGS_PCMENU_RETURN_RVA  0xA7BDA6
#define GLOBAL_COLLECTBINDINGS_PCSAVE_RETURN_RVA  0xBB5439

namespace InputPromptFix::Policy
{
    // Is the given alias a PC specific binding?
    constexpr bool IsPcBindingLookup(std::uintptr_t CallerRva, std::wstring_view Alias)
    {
        return (CallerRva == GLOBAL_COLLECTBINDINGS_PCMENU_RETURN_RVA || CallerRva == GLOBAL_COLLECTBINDINGS_PCSAVE_RETURN_RVA) &&
            Alias.size() > 7 && StartsWithAsciiNoCase(Alias, "Shared_");
    }

    template<typename Array, typename IndexType>
    void EraseOwnedBinding(Array& Values, IndexType Index)
    {
        auto const Count = Values.Count();
        for (auto Next = Index + 1; Next < Count; ++Next)
        {
            Values(Next - 1) = std::move(Values(Next));
        }
        Values.Resize(Count - 1);
    }

    // Keep sentinels: two Unbound slots are an intentional clear in a current profile
    template<typename Array, typename Labels, typename Classifier>
    void KeepPcBindings(Array& Bindings, Labels& Names, Classifier Classify)
    {
        for (auto Index = Bindings.Count(); Index != 0;)
        {
            --Index;
            if (Classify(Bindings(Index)) == Device::Gamepad)
            {
                EraseOwnedBinding(Bindings, Index);
                EraseOwnedBinding(Names, Index);
            }
        }
    }

    // Hide matching records for a single native call (allowing save to complete properly), then restore the values
    template<typename Array, typename Predicate, typename Apply>
    void WithBindingsHidden(Array& Bindings, Predicate Hide, Apply Call)
    {
        using Binding = std::remove_reference_t<decltype(Bindings(0))>;
        using IndexType = decltype(Bindings.Count());
        struct Restore
        {
            Array& Target;
            std::vector<std::pair<IndexType, Binding>> Saved;
            ~Restore()
            {
                for (auto& [Index, Value] : Saved)
                {
                    Target.Insert(std::min(Index, Target.Count()), std::move(Value));
                }
            }
        } Guard{Bindings, {}};

        for (IndexType Index = 0; Index < Bindings.Count(); ++Index)
        {
            if (Hide(Bindings(Index)))
            {
                Guard.Saved.emplace_back(Index, Bindings(Index));
            }
        }
        for (auto Iter = Guard.Saved.rbegin(); Iter != Guard.Saved.rend(); ++Iter)
        {
            EraseOwnedBinding(Bindings, Iter->first);
        }
        Call();
    }

    template<typename Array, typename Classifier, typename Apply>
    void WithGamepadBindingsPreserved(Array& Bindings, Classifier Classify, Apply ReplacePc)
    {
        WithBindingsHidden(Bindings,
            [&](auto const& Binding) { return Classify(Binding) == Device::Gamepad; }, ReplacePc);
    }
}
