#include "../Engine/InputPromptPolicy.hpp"

#include <array>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

using namespace InputPromptFix::Policy;
using namespace std::literals;

static void Check(bool Condition, char const* Description)
{
    if (!Condition)
    {
        std::cerr << "FAIL: " << Description << '\n';
        std::exit(1);
    }
}

int main()
{
    Check(ShouldAdjust(k_controlTokenReturnRva, false, L"Shared_Melee"), "token caller");
    Check(ShouldAdjust(k_getBoundKeyReturnRva, false, L"sHaReD_CustomAction"), "all shared aliases, case insensitive");
    Check(!ShouldAdjust(k_getBoundKeyReturnRva, false, L"PC_Melee"), "PC aliases untouched");
    Check(!ShouldAdjust(k_getBoundKeyReturnRva, false, L"Console_Melee"), "console aliases untouched");
    Check(!ShouldAdjust(k_getBoundKeyReturnRva, false, L"Shared_"), "empty alias suffix");
    Check(!ShouldAdjust(k_getBoundKeyReturnRva, true, L"Shared_Melee"), "defaults enumeration untouched");
    Check(!ShouldAdjust(0xA7BDA6, false, L"Shared_Melee"), "controls-menu enumeration untouched");
    Check(!ShouldAdjust(0xBB5439, false, L"Shared_Melee"), "binding-management caller untouched");
    Check(ClassifyKey("XboxTypeS_B"sv) == Device::Gamepad, "physical gamepad");
    Check(ClassifyKey(L"gamepad_LeftStick_Up"sv) == Device::Gamepad, "emulated gamepad");
    Check(ClassifyKey(L"LeftMouseButton"sv) == Device::KeyboardMouse, "mouse icon is not gamepad");
    Check(ClassifyKey(L"MouseScrollUp"sv) == Device::KeyboardMouse, "wheel binding");
    Check(ClassifyKey(L"unbound"sv) == Device::Unbound, "unbound sentinel");
    Check(ClassifyKey("None"sv) == Device::Unbound, "none sentinel");
    Check(ClassifyKey(L""sv) == Device::Unbound, "empty sentinel");

    std::array Keys{ L"XboxTypeS_B"sv, L"None"sv, L"G"sv, L"F"sv, L"XboxTypeS_X"sv };
    auto Select = [&](bool Gamepad) { return SelectBinding(Keys.size(), Gamepad,
        [&](std::size_t Index) { return ClassifyKey(Keys[Index]); }); };
    Check(Select(false) == 2, "remapped primary keyboard key wins over controller and secondary");
    Check(Select(true) == 0, "gamepad selection");
    Check(Select(false) == 2, "mode switch reevaluates without cache");
    std::array KeyboardFirst{ L"LeftMouseButton"sv, L"XboxTypeS_RightTrigger"sv, L"XboxTypeS_A"sv };
    Check(SelectBinding(KeyboardFirst.size(), true,
        [&](std::size_t Index) { return ClassifyKey(KeyboardFirst[Index]); }) == 1,
        "gamepad primary wins when a mouse binding is first");
    Check(SelectBinding(0, false, [](std::size_t) { return Device::Gamepad; }) == 0, "empty result");
    Check(SelectBinding(1, false, [](std::size_t) { return Device::Gamepad; }) == 1, "no wrong-device fallback");
    Check(SelectBinding(2, true, [](std::size_t) { return Device::KeyboardMouse; }) == 2, "no keyboard fallback for pad");

    // Move-only ownership exercises promotion without copying or double-destruction,
    // while a separate label array verifies key/modifier/display-name association.
    struct Binding { std::unique_ptr<int> Owned; int Modifiers; };
    std::array<Binding, 3> Bindings{{ {std::make_unique<int>(10), 0},
        {std::make_unique<int>(20), 3}, {std::make_unique<int>(30), 4} }};
    std::array<std::wstring, 3> Labels{L"B", L"Ctrl+Shift+G", L"Alt+F"};
    Check(PromoteBinding(std::span<Binding>(Bindings), std::span<std::wstring>(Labels), 1), "paired promotion");
    Check(*Bindings[0].Owned == 20 && Bindings[0].Modifiers == 3 && Labels[0] == L"Ctrl+Shift+G", "selected key keeps modifiers and label");
    Check(*Bindings[1].Owned == 10 && Labels[1] == L"B" && *Bindings[2].Owned == 30, "remaining priority preserved");
    Check(!PromoteBinding(std::span<Binding>(Bindings), std::span<std::wstring>(Labels).first(2), 1), "mismatched arrays rejected");
    Check(!PromoteBinding(std::span<Binding>(Bindings), std::span<std::wstring>(Labels), 3), "invalid index rejected");
    std::cout << "Input prompt policy tests passed\n";
}
