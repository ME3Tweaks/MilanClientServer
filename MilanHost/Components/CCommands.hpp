/**
 * @file        MilanHost/Components/CCommands.hpp
 * @header      Component for registering and handling custom console commands.
 */

#pragma once

#include <functional>

#include "Base/Base.hpp"
#include "Containers/string.hpp"
#include "Containers/vector.hpp"


class CCommands final : public NonCopyable
{
public:

    static constexpr wchar_t const* k_typeName = L"commands";

    bool Initialize(::LESDK::Initializer& Context);
    void Terminate(::LESDK::Initializer& Context);

    using ConsoleCommand_t = bool(wchar_t const* Params);
    using ConsoleCommandPair_t = std::pair<game::wstring, std::function<ConsoleCommand_t>>;

    /** Registers a command with text '<namespace>.<command>' implemented by the given callback. */
    void RegisterCommand(wchar_t const* Namespace, wchar_t const* Command, std::function<ConsoleCommand_t>&& Callback);

    /** Executes some code when UGameEngine::Exec hook is triggered. */
    bool On_UGameEngineExec(UGameEngine* Context, WCHAR const* Command, void* Output);

private:

    game::vector<ConsoleCommandPair_t> m_commands{};
};

static_assert(ComponentType<CCommands>);
