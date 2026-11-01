#include <chrono>
#include <deque>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "Base/Logger.hpp"
#include "Engine/ConsolePipe.hpp"
#include "Engine/ExtObject.hpp"
#include "Engine/Hooks.hpp"


#ifndef _FINAL_RELEASE
namespace ConsolePipe
{
    namespace
    {
        constexpr DWORD k_bufferSize = 64 * 1024;

        /** How long a client waits for the game thread before giving up on a command (it still runs later). */
        constexpr auto k_commandTimeout = std::chrono::seconds{ 15 };

        struct Request
        {
            std::wstring Command{};
            std::promise<std::wstring> Output{};
        };

        std::mutex s_lock{};
        std::deque<std::shared_ptr<Request>> s_pending{};

        /**
         * Collects what an engine Exec prints. Mirrors the game's FOutputDevice: its vtable is destructor, Serialize(Text,
         * Event), Flush, TearDown, followed by the bAllowSuppression, bSuppressEventTag and bAutoEmitLineTerminator flags.
         * The game only calls it through those virtuals and reads those flags.
         */
        class FCaptureOutputDevice
        {
        public:
            virtual ~FCaptureOutputDevice() = default;

            virtual void Serialize(wchar_t const* const Text, int const /* Event */)
            {
                if (Text != nullptr)
                {
                    m_text += Text;
                    m_text += L'\n';
                }
            }

            virtual void Flush() {}
            virtual void TearDown() {}

            std::wstring const& GetText() const { return m_text; }

        private:
            UBOOL m_allowSuppression = TRUE;
            UBOOL m_suppressEventTag = TRUE;
            UBOOL m_autoEmitLineTerminator = FALSE;
            std::wstring m_text{};
        };

        std::wstring Widen(std::string const& Text)
        {
            if (Text.empty())
            {
                return {};
            }
            int const Length = ::MultiByteToWideChar(CP_UTF8, 0, Text.data(), static_cast<int>(Text.size()), nullptr, 0);
            std::wstring Wide(static_cast<std::size_t>(Length), L'\0');
            ::MultiByteToWideChar(CP_UTF8, 0, Text.data(), static_cast<int>(Text.size()), Wide.data(), Length);
            return Wide;
        }

        std::string Narrow(std::wstring const& Text)
        {
            if (Text.empty())
            {
                return {};
            }
            int const Length = ::WideCharToMultiByte(CP_UTF8, 0, Text.data(), static_cast<int>(Text.size()), nullptr, 0, nullptr, nullptr);
            std::string Narrowed(static_cast<std::size_t>(Length), '\0');
            ::WideCharToMultiByte(CP_UTF8, 0, Text.data(), static_cast<int>(Text.size()), Narrowed.data(), Length, nullptr, nullptr);
            return Narrowed;
        }

        /**
         * Runs one command as if typed into the console: through the local player's controller, so its exec functions
         * and cheats are reachable, or straight through the engine when there's no controller (before login, or when a map
         * never spawns one). Game thread only.
         */
        std::wstring RunCommand(std::wstring const& Command)
        {
            UEngine* const Engine = (GEngine != nullptr) ? *GEngine : nullptr;
            if (Engine == nullptr)
            {
                return L"(no engine yet)";
            }

            ULocalPlayer* const Player = Engine->GamePlayers.Count() > 0 ? Engine->GamePlayers(0) : nullptr;
            if (Player != nullptr && Player->Actor != nullptr)
            {
                FString const Output = Player->Actor->ConsoleCommand(FString{ Command.c_str() }, TRUE);
                return Output.Length() > 0 ? std::wstring{ *Output } : std::wstring{};
            }

            FCaptureOutputDevice Output{};
            if (!UGameEngine_Exec_hook(static_cast<UGameEngine*>(Engine), Command.c_str(), &Output))
            {
                return L"(no player controller, and the engine didn't recognize the command)\n" + Output.GetText();
            }
            return Output.GetText();
        }

        /** Queues a command for the game thread and waits for what it returned. */
        std::wstring Submit(std::wstring const& Command)
        {
            auto const Pending = std::make_shared<Request>();
            Pending->Command = Command;
            auto Output = Pending->Output.get_future();
            {
                std::scoped_lock Guard{ s_lock };
                s_pending.push_back(Pending);
            }

            if (Output.wait_for(k_commandTimeout) != std::future_status::ready)
            {
                return L"(timed out waiting for the game thread; the command still runs when the game ticks)";
            }
            return Output.get();
        }

        /** Reads one client's commands (up to its first newline-terminated read) and answers each in turn. */
        void HandleClient(HANDLE const Pipe)
        {
            std::string Received{};
            char Buffer[4096];
            while (Received.empty() || Received.back() != '\n')
            {
                DWORD Read = 0;
                if (!::ReadFile(Pipe, Buffer, sizeof(Buffer), &Read, nullptr) || Read == 0)
                {
                    break;
                }
                Received.append(Buffer, Read);
            }

            std::wstring Reply{};
            std::wstring const Commands = Widen(Received);
            std::size_t Start = 0;
            while (Start < Commands.size())
            {
                std::size_t End = Commands.find(L'\n', Start);
                if (End == std::wstring::npos)
                {
                    End = Commands.size();
                }
                std::wstring Command = Commands.substr(Start, End - Start);
                Start = End + 1;
                while (!Command.empty() && (Command.back() == L'\r' || Command.back() == L' '))
                {
                    Command.pop_back();
                }
                if (Command.empty())
                {
                    continue;
                }

                MILAN_INFOW(ELOG_EngineHooks, L"console pipe: %s", Command.c_str());
                Reply += L"> " + Command + L"\n" + Submit(Command) + L"\n";
            }

            std::string const Bytes = Narrow(Reply);
            DWORD Written = 0;
            ::WriteFile(Pipe, Bytes.data(), static_cast<DWORD>(Bytes.size()), &Written, nullptr);
            ::FlushFileBuffers(Pipe);
        }

        void Serve(std::wstring const Name)
        {
            for (;;)
            {
                HANDLE const Pipe = ::CreateNamedPipeW(Name.c_str(), PIPE_ACCESS_DUPLEX,
                    PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                    1, k_bufferSize, k_bufferSize, 0, nullptr);
                if (Pipe == INVALID_HANDLE_VALUE)
                {
                    MILAN_ERRORW(ELOG_EngineHooks, L"console pipe: couldn't create %s (error %lu)", Name.c_str(), ::GetLastError());
                    return;
                }

                if (::ConnectNamedPipe(Pipe, nullptr) || ::GetLastError() == ERROR_PIPE_CONNECTED)
                {
                    HandleClient(Pipe);
                }
                ::DisconnectNamedPipe(Pipe);
                ::CloseHandle(Pipe);
            }
        }
    }

    void Start()
    {
        std::wstring Name = L"\\\\.\\pipe\\MilanHost-" + std::to_wstring(::GetCurrentProcessId());
        MILAN_INFOW(ELOG_EngineHooks, L"console pipe: %s", Name.c_str());
        std::thread{ Serve, std::move(Name) }.detach();
    }

    void Pump()
    {
        std::deque<std::shared_ptr<Request>> Ready{};
        {
            std::scoped_lock Guard{ s_lock };
            Ready.swap(s_pending);
        }

        for (auto const& Pending : Ready)
        {
            Pending->Output.set_value(RunCommand(Pending->Command));
        }
    }
}
#endif
