#include <LESDK/Init.hpp>
#include <LESDK/Headers.hpp>

#include "Systems/Output.hpp"
#include "Logger.hpp"


namespace Milan::Output
{
    ::MilanLogger*      gp_mainLogger = nullptr;
    ::ILogger*          gp_activeLogger = nullptr;


    void Initialize(::LESDK::Initializer& Context)
    {
        (void)Context;

#ifndef _FINAL_RELEASE
        ::LESDK::InitializeConsole();
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
    }

    void Terminate()
    {
        if (gp_activeLogger != gp_mainLogger)
            delete gp_activeLogger;

        if (gp_mainLogger != nullptr)
            delete gp_mainLogger;

#ifndef _FINAL_RELEASE
        ::LESDK::TerminateConsole();
#endif
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
