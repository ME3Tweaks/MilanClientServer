#include <LESDK/Init.hpp>
#include <LESDK/Headers.hpp>

#include "Hooks.hpp"
#include "Logger.hpp"
#include "Systems/Output.hpp"


#ifndef _FINAL_RELEASE
BOOL WINAPI MilanConsoleHandler(DWORD const dwCtrlType)
{
    if (dwCtrlType <= CTRL_CLOSE_EVENT)
        ::SingleDetachImpl();

    return false;
}
#endif


namespace Milan::Output
{
    ::MilanLogger*      gp_mainLogger = nullptr;
    ::ILogger*          gp_activeLogger = nullptr;

    bool                gb_initialized = false;


    void Initialize(::LESDK::Initializer& Context)
    {
        (void)Context;

#ifndef _FINAL_RELEASE
        ::LESDK::InitializeConsole();
        ::SetConsoleCtrlHandler(::MilanConsoleHandler, TRUE);
#endif

        gp_mainLogger = new MilanLogger{};

        static constexpr int k_tagConsole = 200;
        static constexpr int k_tagFile = 201;

#if defined(_DEBUG) || !defined(_FINAL_RELEASE)
        // In DEBUG and RELEASE builds keep both sinks, and set file to overwrite previous log.
        gp_mainLogger->RegisterOutput(k_tagConsole, new ConsoleLoggerOutput{});
        gp_mainLogger->RegisterOutput(k_tagFile, new FileLoggerOutput{ "MilanHost.log", false });
#else
        // In FINAL_RELEASE builds only enable file sink, and set it to append.
        gp_mainLogger->RegisterOutput(k_tagFile, new FileLoggerOutput{ "MilanHost.log", true });
#endif

        if (gp_activeLogger == nullptr)
            gp_activeLogger = gp_mainLogger;

        // From now on, it should be possible to use logging.

        gb_initialized = true;
        MILAN_INFOW(L"output system initialized");
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


    ILogger& GetMainLogger()
    {
        assert(gp_activeLogger != nullptr);
        return *gp_activeLogger;
    }

    void FlushEverything()
    {
        assert(gp_mainLogger != nullptr);
        gp_mainLogger->ForceFlush();
    }
}
