#include "Components/CCommands.hpp"
#include "Components/CNetworkProfiler.hpp"
#include "NetProf/NetProfStream.hpp"


// ! CNetworkProfiler implementation.
// ========================================

bool CNetworkProfiler::Initialize(::LESDK::Initializer& Context)
{
    COMPONENT(CCommands).RegisterCommand(L"repl", L"capture",
        [](wchar_t const*)
        {
            if (NetworkCapture::Global() != nullptr)
            {
                MILAN_WARNW(L"global instance already started");
                return true;
            }

            NetworkCapture::GlobalInitialize();
            return true;
        });

    COMPONENT(CCommands).RegisterCommand(L"repl", L"endcapture",
        [](wchar_t const*)
        {
            if (NetworkCapture::Global() == nullptr)
            {
                MILAN_WARNW(L"global instance not started");
                return true;
            }

            NetworkCapture::GlobalClose();
            return true;
        });

    MILAN_UNUSED(Context);
    MILAN_DEBUGW(ELOG_Startup, L"component '%s' initialized", k_typeName);
    return true;
}

void CNetworkProfiler::Terminate(::LESDK::Initializer& Context)
{
    MILAN_UNUSED(Context);
    MILAN_DEBUGW(ELOG_Teardown, L"component '%s' terminating", k_typeName);

    if (NetworkCapture::Global() != nullptr)
        NetworkCapture::GlobalClose();
}

void CNetworkProfiler::On_VectoredException(void* const Address, DWORD const Code, _EXCEPTION_POINTERS* const Exceptions)
{
    MILAN_UNUSED_3(Address, Code, Exceptions);

    if (NetworkCapture::Global() != nullptr)
        NetworkCapture::Global()->FlushRecords();
}
