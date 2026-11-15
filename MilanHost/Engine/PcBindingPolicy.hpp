#pragma once

#include "InputPromptPolicy.hpp"
#include <array>
#include <utility>
#include <vector>
#include <type_traits>

namespace InputPromptFix::Policy
{
    inline constexpr std::uintptr_t k_pcMenuReturnRva = 0xA7BDA6;
    inline constexpr std::uintptr_t k_pcSaveReturnRva = 0xBB5439;

    // Is the given alias a PC specific binding?
    constexpr bool IsPcBindingLookup(std::uintptr_t CallerRva, std::wstring_view Alias)
    {
        return (CallerRva == k_pcMenuReturnRva || CallerRva == k_pcSaveReturnRva) &&
            Alias.size() > 7 && StartsWithAsciiNoCase(Alias, "Shared_");
    }

    // Map PC keys to shared versions.
    // Sometimes we need to bind to PC_ cause other code doesn't work with shared,
    // but this translates it back
    constexpr std::wstring_view MigratePcAlias(std::wstring_view Alias)
    {
        constexpr std::array Names{
            std::pair{"PC_Action", L"Shared_Action"},
            std::pair{"PC_Shoot", L"Shared_Shoot"},
            std::pair{"PC_Aim", L"Shared_Aim"},
            std::pair{"PC_Melee", L"Shared_Melee"},
            std::pair{"PC_QuickSave", L"Shared_QuickSave"},
            std::pair{"PC_ExitAtlas", L"Shared_ExitAtlas"},
            std::pair{"PC_ExitMountedGun", L"Shared_ExitMountedGun"}
        };
        for (auto const& [Old, Current] : Names)
        {
            if (Alias.size() == std::string_view(Old).size() && StartsWithAsciiNoCase(Alias, Old))
            {
                return Current;
            }
        }
        return {};
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

    template<typename Range, typename Classifier>
    bool RetainDefaultsForLegacyRow(bool LegacyRow, Range const& Keys, Classifier Classify)
    {
        return LegacyRow && std::none_of(Keys.begin(), Keys.end(), [&](auto const& Key)
            { return Classify(Key) == Device::KeyboardMouse; });
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

    // Native replacement owns key conflicts and alias counts. Hide the controller
    // records for that call, then restore their complete values and original order.
    template<typename Array, typename Classifier, typename Apply>
    void WithGamepadBindingsPreserved(Array& Bindings, Classifier Classify, Apply ReplacePc)
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
            if (Classify(Bindings(Index)) == Device::Gamepad)
            {
                Guard.Saved.emplace_back(Index, Bindings(Index));
            }
        }
        for (auto Iter = Guard.Saved.rbegin(); Iter != Guard.Saved.rend(); ++Iter)
        {
            EraseOwnedBinding(Bindings, Iter->first);
        }
        ReplacePc();
    }
}
