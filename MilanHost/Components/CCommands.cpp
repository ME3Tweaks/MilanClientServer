#include <algorithm>
#include <cwctype>

#include "Components/CCommands.hpp"


void StringToLower(::game::wstring& String)
{
    std::transform(String.begin(), String.end(), String.begin(),
        [](wchar_t const ch) { return std::towlower(ch); });
}

// ! CCommands implementation.
// ========================================

bool CCommands::Initialize(::LESDK::Initializer& Context)
{
    MILAN_UNUSED(Context);

    m_commands.reserve(16);

    MILAN_DEBUGW(L"component '%s' initialized", k_typeName);
    return true;
}

void CCommands::Terminate(::LESDK::Initializer& Context)
{
    MILAN_UNUSED(Context);
    MILAN_DEBUGW(L"component '%s' terminating", k_typeName);
}

void CCommands::RegisterCommand(wchar_t const* const Namespace, wchar_t const* const Command,
    std::function<ConsoleCommand_t>&& Callback)
{
    MILAN_CHECKW(Command != nullptr, L"");
    MILAN_CHECKW(std::wcslen(Command) > 0, L"");

    ::game::wstring NSCommand{};

    if (Namespace != nullptr && std::wcslen(Namespace) > 0)
    {
        NSCommand.append(Namespace).append(L".").append(Command);
    }
    else
    {
        NSCommand.assign(Command);
    }

    ::StringToLower(NSCommand);
    m_commands.push_back({ NSCommand, Callback });

    MILAN_TRACEW(L"registered command %s", NSCommand.c_str());
}

bool CCommands::On_UGameEngineExec(UGameEngine* const Context, WCHAR const* const Command, void* const Output)
{
    MILAN_UNUSED_2(Context, Output);

    ::game::wstring ScopedCommand{ Command };
    ::StringToLower(ScopedCommand);

    while (!ScopedCommand.empty() && ScopedCommand.back() == L' ')
        ScopedCommand.erase(ScopedCommand.end() - 1);

    for (auto const& [NSCommand, Callback] : m_commands)
    {
        if (ScopedCommand.starts_with(NSCommand))
        {
            wchar_t const* Params = ScopedCommand.c_str() + NSCommand.length();
            wchar_t const* const ParamsEnd = ScopedCommand.c_str() + ScopedCommand.size();

            while (*Params != L' ' && Params != ParamsEnd)
                ++Params;

            while (*Params == L' ' && Params != ParamsEnd)
                ++Params;

            if (Callback(Params))
                return true;

            break;
        }
    }

    return false;
}
