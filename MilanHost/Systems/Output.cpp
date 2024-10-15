#include <LESDK/Headers.hpp>

#include "Milan/Hooks.hpp"
#include "Milan/Intercept.hpp"
#include "Milan/Logger.hpp"
#include "Milan/Misc.hpp"
#include "Systems/Output.hpp"
#include "Systems/Globals.hpp"


namespace
{
    bool gb_enableLogInternal = false;
    bool gb_enableWarnInternal = false;
}


#ifndef _FINAL_RELEASE
BOOL WINAPI MilanConsoleHandler(DWORD const dwCtrlType)
{
    if (dwCtrlType <= CTRL_CLOSE_EVENT)
        ::SingleDetachImpl();

    return false;
}
#endif

UFunction* FindRequiredFunction(short const iNative)
{
    for (TypedObjectIterator<UFunction, false, false> Iter; Iter; ++Iter)
    {
        if ((*Iter)->iNative == iNative)
        {
            return *Iter;
        }
    }

    MILAN_FATALW(L"failed to find required function with iNative = %d", iNative);
    //return nullptr;
}

void UObject_LogInternal(UObject* const Context, FFrame* const Stack, [[maybe_unused]] void* const Result)
{
    NATIVE_GET_STRING(Str);
    NATIVE_GET_NAME_OPTIONAL(Tag, SFXName{});
    NATIVE_FINISH();

    static FString ObjectName{};
    static FString FunctionName{};

    if (!gb_enableLogInternal)
        return;

    ObjectName.Clear();
    Context->AppendName(ObjectName, SFXName::k_formatInstanced);

    FunctionName.Clear();
    Stack->Node->AppendName(FunctionName, SFXName::k_formatInstanced);

    MILAN_DEBUGW(L"LogInternal (%s.%s) {%s}: %s", *ObjectName, *FunctionName, *Tag.ToString(), *Str);
}

void UObject_WarnInternal(UObject* const Context, FFrame* const Stack, [[maybe_unused]] void* const Result)
{
    NATIVE_GET_STRING(Str);
    NATIVE_FINISH();

    static FString ObjectName{};
    static FString FunctionName{};

    if (!gb_enableWarnInternal)
        return;

    ObjectName.Clear();
    Context->AppendName(ObjectName, SFXName::k_formatInstanced);

    FunctionName.Clear();
    Stack->Node->AppendName(FunctionName, SFXName::k_formatInstanced);

    MILAN_WARNW(L"WarnInternal (%s.%s): %s", *ObjectName, *FunctionName, *Str);
}


namespace Milan::Output
{
    ::MilanLogger*      gp_mainLogger = nullptr;
    ::ILogger*          gp_activeLogger = nullptr;

    bool                gb_initialized = false;


    // ! Global subsystem functions.
    // ========================================

    void Initialize(::LESDK::Initializer& Context)
    {
        (void)Context;

#ifndef _FINAL_RELEASE
        ::LESDK::InitializeConsole();
        ::SetConsoleCtrlHandler(::MilanConsoleHandler, TRUE);

        HANDLE const    ConsoleHandle = GetStdHandle(STD_OUTPUT_HANDLE);
        BOOL            bEnableVirtualTerminal = TRUE;
        DWORD           dwOriginalMode = 0u;

        if (ConsoleHandle == INVALID_HANDLE_VALUE) [[unlikely]]
        {
            bEnableVirtualTerminal = FALSE;
            auto const ErrorCode = std::to_string(::GetLastError());
            auto const ErrorMessage = std::string("can't get stdout handle: ") + ErrorCode;
            ::OutputDebugStringA(ErrorMessage.c_str());
        }
        else if (0 == ::GetConsoleMode(ConsoleHandle, &dwOriginalMode) ||
            0 == ::SetConsoleMode(ConsoleHandle, dwOriginalMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING))
        {
            bEnableVirtualTerminal = FALSE;
            auto const ErrorCode = std::to_string(::GetLastError());
            auto const ErrorMessage = std::string("can't enable virtual terminal: ") + ErrorCode;
            ::OutputDebugStringA(ErrorMessage.c_str());
        }
#endif

        gp_mainLogger = new MilanLogger{};
        std::string LogFileName = "MilanHost.log";

        int const OrdinalInstance = ::GetGameInstanceOrdinal();
        if (OrdinalInstance > 0)
        {
            // This allows us to use different log files when running multiple local clients.
            LogFileName = std::string{ "MilanHost-" } + std::to_string(OrdinalInstance) + ".log";
        }

#if defined(_MLN_DEBUG) || !defined(_FINAL_RELEASE)
        // In DEBUG and RELEASE builds keep both sinks, and set file to overwrite previous log.
        gp_mainLogger->RegisterOutput(bEnableVirtualTerminal ? (ILoggerOutput*)(new ConsoleLoggerOutput) : new BasicConsoleLoggerOutput);
        gp_mainLogger->RegisterOutput(new FileLoggerOutput{ LogFileName, false });
#else
        // In FINAL_RELEASE builds only enable file sink, and set it to append.
        gp_mainLogger->RegisterOutput(new FileLoggerOutput{ LogFileName, true });
#endif

        if (gp_activeLogger == nullptr)
            gp_activeLogger = gp_mainLogger;

        // From now on, it should be possible to use normal logging.

        gb_initialized = true;
        MILAN_INFOW(L"output system initialized");
    }

    void DeferredInitialize(::LESDK::Initializer& Context)
    {
        (void)Context;

        FString const CmdArgs{ Globals::GetCmdArgs() };

        gb_enableLogInternal = CmdArgs.Contains(L"-startloginternal", true);
        gb_enableWarnInternal = CmdArgs.Contains(L"-startwarninternal", true);


#if defined(MILAN_BUILD_LOGINTERNAL) && MILAN_BUILD_LOGINTERNAL

        // Replace LogInternal() and WarnInternal() to output to our logger.
        // TODO: Disable this behavior when DebugLogger is installed.

        GNatives[0xE7] = UObject_LogInternal;
        FindRequiredFunction(0xE7)->Func = UObject_LogInternal;
        MILAN_INFOW(L"replaced LogInternal through iNative");

        GNatives[0xE8] = UObject_WarnInternal;
        FindRequiredFunction(0xE8)->Func = UObject_WarnInternal;
        MILAN_INFOW(L"replaced WarnInternal through iNative");

#endif
    }

    void Terminate()
    {
        MILAN_INFOW(L"output system terminating");
        gb_initialized = false;

        if (gp_activeLogger != gp_mainLogger)
            delete gp_activeLogger;

        if (gp_mainLogger != nullptr)
            delete gp_mainLogger;

        gp_activeLogger = nullptr;
        gp_mainLogger = nullptr;

#ifndef _FINAL_RELEASE
        ::LESDK::TerminateConsole();
#endif
    }


    bool HasInitialized() noexcept
    {
        return gb_initialized;
    }


    // ! Public interface.
    // ========================================

    void On_VectoredException(void* const Address, DWORD const Code, _EXCEPTION_POINTERS* const Exceptions)
    {
        MILAN_WARNW(L"output: handling vectored exception, address = %p, code = 0x%08X", Address, Code);

        if (gp_mainLogger != nullptr)
            gp_mainLogger->Flush();

        MILAN_UNUSED_3(Address, Code, Exceptions);
    }

    ILogger& GetMainLogger()
    {
        assert(gp_activeLogger != nullptr);
        return *gp_activeLogger;
    }

    void FlushEverything()
    {
        if (gp_mainLogger)
            gp_mainLogger->Flush();
    }

#if defined(MILAN_BUILD_LOGINTERNAL) && MILAN_BUILD_LOGINTERNAL

    bool ToggleLogInternal() noexcept
    {
        gb_enableLogInternal = !gb_enableLogInternal;
        return gb_enableLogInternal;
    }

    bool ToggleWarnInternal() noexcept
    {
        gb_enableWarnInternal = !gb_enableWarnInternal;
        return gb_enableWarnInternal;
    }

#endif

}
