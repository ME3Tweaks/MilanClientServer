#include <algorithm>
#include <cwctype>

#include <LESDK/Init.hpp>
#include <LESDK/Headers.hpp>

#include "Base.hpp"
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

    t_UGameEngine_Exec* UGameEngine_Exec_orig = nullptr;
    CommandVectorType* gp_commands = nullptr;


    // ! Global subsystem functions.
    // ========================================

    void Initialize(::LESDK::Initializer& Context)
    {
        t_UGameEngine_Exec* UGameEngine_Exec_target = Context.ResolveTyped<t_UGameEngine_Exec>(GLOBAL_UGAMEENGINE_EXEC_PHOOK);
        UGameEngine_Exec_orig = (t_UGameEngine_Exec*)Context.InstallHook("t_UGameEngine_Exec", UGameEngine_Exec_target, UGameEngine_Exec_hook);

        gp_commands = new CommandVectorType;
        gp_commands->reserve(8);

        MILAN_INFOW(L"console system initialized");
    }

    void Terminate()
    {
        MILAN_INFOW(L"console system terminating");
        delete gp_commands;
    }


    // ! Subsystem hooks.
    // ========================================

    UBOOL UGameEngine_Exec_hook(UGameEngine* const Context, WCHAR const* const Command, void* const Output)
    {
        //MILAN_TRACEW_FMT(L"UGameEngine::Exec: %s", Command);

        ::game::wstring ScopedCommand{ Command };
        ::StringToLower(ScopedCommand);

        while (!ScopedCommand.empty() && ScopedCommand.back() == L' ')
            ScopedCommand.erase(ScopedCommand.end() - 1);

        for (auto const& [NSCommand, Callback] : *gp_commands)
        {
            if (ScopedCommand.starts_with(NSCommand))
            {
                wchar_t const* Params = ScopedCommand.c_str() + NSCommand.length();

                if (*Params == L' ')
                    ++Params;

                if (Callback(Params))
                    return TRUE;

                break;
            }
        }

        return (*UGameEngine_Exec_orig)(Context, Command, Output);
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

        MILAN_DEBUGW_FMT(L"registered command %s", NSCommand.c_str());
    }
}
