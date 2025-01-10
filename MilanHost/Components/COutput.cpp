#include "Base/Logger.hpp"
#include "Components/COutput.hpp"
#include "Engine/Hooks.hpp"
#include "Engine/Intercept.hpp"
#include "Milan/Misc.hpp"
#include "Shell/ClientConfig.hpp"
#include "Shell/CmdArgs.hpp"


#ifndef _FINAL_RELEASE

BOOL WINAPI MilanConsoleHandler(DWORD const dwCtrlType)
{
    if (dwCtrlType <= CTRL_CLOSE_EVENT)
        ::SingleDetachImpl();

    return false;
}

UFunction* FindRequiredFunction(short const iNative)
{
    for (TypedObjectIterator<UFunction, false, false> Iter; Iter; ++Iter)
    {
        if ((*Iter)->iNative == iNative) return *Iter;
    }

    MILAN_FATALW(L"failed to find required function with iNative = %d", iNative);
    //return nullptr;
}

#if defined(MILAN_BUILD_LOGINTERNAL) && MILAN_BUILD_LOGINTERNAL

void UObject_LogInternal(UObject* const Context, FFrame* const Stack, [[maybe_unused]] void* const Result)
{
    NATIVE_GET_STRING(Str);
    NATIVE_GET_NAME_OPTIONAL(Tag, SFXName{});
    NATIVE_FINISH();

    static FString ObjectName{};
    static FString FunctionName{};

    if (!COMPONENT(COutput).bEnableLogInternal) return;

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

    if (!COMPONENT(COutput).bEnableWarnInternal) return;

    ObjectName.Clear();
    Context->AppendName(ObjectName, SFXName::k_formatInstanced);

    FunctionName.Clear();
    Stack->Node->AppendName(FunctionName, SFXName::k_formatInstanced);

    MILAN_WARNW(L"WarnInternal (%s.%s): %s", *ObjectName, *FunctionName, *Str);
}

#endif
#endif


// ! COutput implementation.
// ========================================

bool COutput::Initialize(::LESDK::Initializer& Context)
{
    MILAN_UNUSED(Context);

    int NameInitCount = 0;
    int NameSuppressCount = 0;

    InitLoggingCategories(NameInitCount);
    InitSuppressedCategories(NameSuppressCount);

    InitConsole();
    InitMainLogger();

    bUseAsyncLogger
        ? MILAN_INFOW(ELOG_Startup, L"using asynchronous logger")
        : MILAN_WARNW(ELOG_Startup, L"forced synchronous logger via -synclogger");

    // These numbers have no less-than-or-equal / greater-than relation.
    // It is valid (but inadvisable) to suppress SFXNames which are not logging categories.
    MILAN_DEBUGW(ELOG_Startup, L"registered %d logging category(ies)", NameInitCount);
    MILAN_DEBUGW(ELOG_Startup, L"suppressed %d logging category name(s)", NameSuppressCount);

    MILAN_DEBUGW(ELOG_Startup, L"component '%s' initialized", k_typeName);
    return true;
}

void COutput::Terminate(::LESDK::Initializer& Context)
{
    MILAN_UNUSED(Context);
    MILAN_DEBUGW(ELOG_Teardown, L"component '%s' terminating", k_typeName);

    // This is not very safe, but it shouldn't change much...
    reinterpret_cast<MilanLogger*>(gp_mainLogger)->Cleanup();

    if (gp_mainLogger != nullptr)
    {
        delete gp_mainLogger;
        gp_mainLogger = nullptr;
    }

#ifndef _FINAL_RELEASE
    ::LESDK::TerminateConsole();
#endif
}

void COutput::PostInitialize(::LESDK::Initializer& Context)
{
    MILAN_UNUSED(Context);

#ifndef _FINAL_RELEASE
#if defined(MILAN_BUILD_LOGINTERNAL) && MILAN_BUILD_LOGINTERNAL

    bEnableLogInternal = CmdArgs::Contain(L"-startloginternal");
    bEnableWarnInternal = CmdArgs::Contain(L"-startwarninternal");

    // Replace LogInternal() and WarnInternal() to output to our logger.
    // TODO: Disable this behavior when DebugLogger is installed.

    GNatives[0xE7] = UObject_LogInternal;
    FindRequiredFunction(0xE7)->Func = UObject_LogInternal;
    MILAN_INFOW(ELOG_Startup, L"replaced LogInternal through iNative");

    GNatives[0xE8] = UObject_WarnInternal;
    FindRequiredFunction(0xE8)->Func = UObject_WarnInternal;
    MILAN_INFOW(ELOG_Startup, L"replaced WarnInternal through iNative");

#endif
#endif
}

void COutput::FlushEverything()
{
    if (gp_mainLogger != nullptr)
        gp_mainLogger->Flush();
}

void COutput::On_VectoredException(void* const Address, DWORD const Code, _EXCEPTION_POINTERS* const Exceptions)
{
    MILAN_UNUSED_3(Address, Code, Exceptions);
    FlushEverything();
}

#ifndef _FINAL_RELEASE
#if defined(MILAN_BUILD_LOGINTERNAL) && MILAN_BUILD_LOGINTERNAL

bool COutput::ToggleLogInternal() noexcept
{
    bEnableLogInternal = !bEnableLogInternal;
    return bEnableLogInternal;
}

bool COutput::ToggleWarnInternal() noexcept
{
    bEnableWarnInternal = !bEnableWarnInternal;
    return bEnableWarnInternal;
}

#endif
#endif

void COutput::InitConsole()
{
#ifndef _FINAL_RELEASE
    ::LESDK::InitializeConsole();
    ::SetConsoleCtrlHandler(::MilanConsoleHandler, TRUE);

    HANDLE const    ConsoleHandle = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD           dwOriginalMode = 0u;

    if (ConsoleHandle == INVALID_HANDLE_VALUE) [[unlikely]]
    {
        bUseVirtualTerminal = FALSE;
        auto const ErrorCode = std::to_string(::GetLastError());
        auto const ErrorMessage = std::string("can't get stdout handle: ") + ErrorCode;
        ::OutputDebugStringA(ErrorMessage.c_str());
    }
    else if (0 == ::GetConsoleMode(ConsoleHandle, &dwOriginalMode) ||
        0 == ::SetConsoleMode(ConsoleHandle, dwOriginalMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING))
    {
        bUseVirtualTerminal = FALSE;
        auto const ErrorCode = std::to_string(::GetLastError());
        auto const ErrorMessage = std::string("can't enable virtual terminal: ") + ErrorCode;
        ::OutputDebugStringA(ErrorMessage.c_str());
    }
#endif
}

void COutput::InitMainLogger()
{
    bUseAsyncLogger = !CmdArgs::Contain(L"-synclogger");

    MilanLogger* MainLogger = bUseAsyncLogger ? new AsyncMilanLogger{} : new MilanLogger{};
    std::string LogFileName = "MilanHost.log";

    int const OrdinalInstance = ::GetGameInstanceOrdinal();
    if (OrdinalInstance > 0)
    {
        // This allows us to use different log files when running multiple local clients.
        LogFileName = std::string{ "MilanHost-" } + std::to_string(OrdinalInstance) + ".log";
    }

#ifndef _FINAL_RELEASE
    // In DEBUG and RELEASE builds keep both sinks, and set file to overwrite previous log.
    MainLogger->AddSink(bUseVirtualTerminal ? (ILoggerOutput*)(new ConsoleLoggerOutput) : new BasicConsoleLoggerOutput);
    MainLogger->AddSink(new FileLoggerOutput{ LogFileName, false });
#else
    // In FINAL_RELEASE builds only enable file sink, and set it to append.
    MainLogger->AddSink(new FileLoggerOutput{ LogFileName, true });
#endif

    MainLogger->Initialize();
    gp_mainLogger = std::exchange(MainLogger, nullptr);
}

void COutput::InitLoggingCategories(int& InitCount)
{
#define _CG_LOGCAT(a) MILAN_CONCAT(ELOG_, a) = SFXName{ #a, 0, false }; InitCount++;
    #include "Base/LoggingCategories.inl"
#undef _CG_LOGCAT
}

void COutput::InitSuppressedCategories(int& InitCount)
{
    MILAN_UNUSED(InitCount);

#ifndef _FINAL_RELEASE
    if (gp_clientConfig != nullptr)
    {
        DWORD flOldProtect = 0u;

        for (auto const& Category : gp_clientConfig->LoggingSuppressed)
        {
            SFXNameEntry* const Entry = SFXName(Category.c_str(), 0, false).GetEntry();
            bool const bConstEntry = Entry->IsConst();

            if (bConstEntry)  [[unlikely]]
            {
                [[maybe_unused]] BOOL const Success = VirtualProtect(Entry, sizeof *Entry, PAGE_READWRITE, &flOldProtect);
                assert(Success && "failed to VirtualProtect a const SFXNameEntry (in)");
            }

            Entry->Index.Flags |= SFXPackedIndex::k_flagSuppressed;

            if (bConstEntry) [[unlikely]]
            {
                [[maybe_unused]] BOOL const Success = VirtualProtect(Entry, sizeof * Entry, flOldProtect, &flOldProtect);
                assert(Success && "failed to VirtualProtect a const SFXNameEntry (out)");
            }

            InitCount++;
        }
    }
#endif // ! _FINAL_RELEASE
}
