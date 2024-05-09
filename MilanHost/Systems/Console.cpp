#include <algorithm>
#include <cwctype>

#include <LESDK/Init.hpp>
#include <LESDK/Headers.hpp>

#include "Base.hpp"
#include "Hooks.hpp"
#include "Containers/string.hpp"
#include "Containers/vector.hpp"
#include "Systems/Console.hpp"
#include "Systems/Output.hpp"


void StringToLower(::game::wstring& String)
{
    std::transform(String.begin(), String.end(), String.begin(),
        [](wchar_t const ch) { return std::towlower(ch); });
}


namespace Milan::Console
{
    using CommandVectorType = ::game::vector<std::pair<::game::wstring, std::function<tOwnConsoleCommand>>>;
    CommandVectorType* gp_commands = nullptr;


    // ! Global subsystem functions.
    // ========================================

    void Initialize(::LESDK::Initializer& Context)
    {
        MILAN_UNUSED(Context);

        gp_commands = new CommandVectorType;
        gp_commands->reserve(8);

        MILAN_INFOW(L"console system initialized");
    }

    void Terminate()
    {
        MILAN_INFOW(L"console system terminating");
        delete gp_commands;
    }


    // ! Public interface.
    // ========================================

    void RegisterCommand(wchar_t const* const Namespace, wchar_t const* const Command,
        std::function<tOwnConsoleCommand>&& Callback)
    {
        assert(Namespace != nullptr);
        assert(Command != nullptr);
        assert(std::wcslen(Command) > 0);

        ::game::wstring NSCommand{};

        if (std::wcslen(Namespace) > 0)
        {
            NSCommand.append(Namespace).append(L".").append(Command);
        }
        else
        {
            NSCommand.assign(Command);
        }

        ::StringToLower(NSCommand);
        gp_commands->push_back({ NSCommand, Callback });

        MILAN_DEBUGW(L"registered command %s", NSCommand.c_str());
    }

    bool OnUGameEngineExec(UGameEngine* const Context, WCHAR const* const Command, void* const Output)
    {
        MILAN_UNUSED_2(Context, Output);

        ::game::wstring ScopedCommand{ Command };
        ::StringToLower(ScopedCommand);

        while (!ScopedCommand.empty() && ScopedCommand.back() == L' ')
            ScopedCommand.erase(ScopedCommand.end() - 1);

        for (auto const& [NSCommand, Callback] : *gp_commands)
        {
            if (ScopedCommand.starts_with(NSCommand))
            {
                wchar_t const* Params = ScopedCommand.c_str() + NSCommand.length();
                wchar_t const* const ParamsEnd = ScopedCommand.c_str() + ScopedCommand.size();

                while (*Params != L' ' && Params != ParamsEnd)
                    ++Params;

                if (Callback(Params))
                    return true;

                break;
            }
        }

        return false;
    }
}
