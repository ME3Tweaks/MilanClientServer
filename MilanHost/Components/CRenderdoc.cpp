#include "Components/CCommands.hpp"
#include "Components/CRenderdoc.hpp"
#include "Milan/CmdArgs.hpp"


// ! CRenderdoc implementation.
// ========================================

bool CRenderdoc::Initialize(::LESDK::Initializer& Context)
{
    LoadAPI();
    RegisterCommands();

    MILAN_UNUSED(Context);
    MILAN_DEBUGW(ELOG_Startup, L"component '%s' initialized", k_typeName);
    return true;
}

void CRenderdoc::Terminate(::LESDK::Initializer& Context)
{
    MILAN_UNUSED(Context);
    MILAN_DEBUGW(ELOG_Teardown, L"component '%s' terminating", k_typeName);
}

void CRenderdoc::On_VectoredException(void* const Address, DWORD const Code, _EXCEPTION_POINTERS* const Exceptions)
{
    MILAN_UNUSED_3(Address, Code, Exceptions);

    if (mp_apiHandle && mp_apiHandle->IsFrameCapturing() == 1)
    {
        MILAN_DEBUGW(L"terminating renderdoc capture");
        if (mp_apiHandle->EndFrameCapture(nullptr, nullptr) != 1)
            MILAN_WARNW(L"failed to terminate renderdoc capture");
    }
}

void CRenderdoc::On_UObject_ProcessEvent(UObject* const Context, UFunction* const Function, void* const Parms, void* const Result)
{
    MILAN_UNUSED_4(Context, Function, Parms, Result);
}

void CRenderdoc::LoadAPI()
{
    auto const ModuleHandle = ::GetModuleHandleA("renderdoc.dll");
    if (ModuleHandle == nullptr)
    {
        // This is not a hard error because RenderDoc is likely not to be attached.
        MILAN_WARNW(ELOG_StartupRenderdoc, L"failed to load renderdoc.dll, multi-frame capture will be disabled");
        MILAN_INFOW(ELOG_StartupRenderdoc, L"if you expect multi-frame capture to work, launch the game from under renderdoc");
        mb_disableRenderdoc = true;
        return;
    }

    auto const pGetApi = reinterpret_cast<pRENDERDOC_GetAPI>(GetProcAddress(ModuleHandle, "RENDERDOC_GetAPI"));
    if (pGetApi == nullptr)
    {
        MILAN_ERRORW(ELOG_StartupRenderdoc, L"failed to get RENDERDOC_GetAPI pointer");
        mb_disableRenderdoc = true;
        return;
    }

    if (pGetApi(eRENDERDOC_API_Version_1_3_0, (void**)&mp_apiHandle) != 1)
    {
        MILAN_ERRORW(ELOG_StartupRenderdoc, L"failed to get initialize gp_apiHandle");
        mb_disableRenderdoc = true;
        return;
    }

    if (!CmdArgs::Contain(L"-ONETHREAD"))
    {
        MILAN_ERRORW(ELOG_StartupRenderdoc, L"renderdoc is attached with a multithreaded renderer, commands will not be enabled");
        MILAN_ERRORW(ELOG_StartupRenderdoc, L"launch game with -ONETHREAD to fix this problem");
        mb_disableRenderdoc = true;
        return;
    }

    // ...
}

void CRenderdoc::RegisterCommands()
{
    COMPONENT(CCommands).RegisterCommand(L"rdoc", L"status",
        [&](wchar_t const*)
        {
            if (mp_apiHandle == nullptr)
            {
                MILAN_WARNW(L"renderdoc: not initialized");
                return true;
            }

            MILAN_INFOW(L"renderdoc: attached");

            int Major = 0, Minor = 0, Patch = 0;
            mp_apiHandle->GetAPIVersion(&Major, &Minor, &Patch);

            int const CaptureCount = mp_apiHandle->GetNumCaptures();
            MILAN_UNUSED(CaptureCount);

            MILAN_INFOW(L"renderdoc: version = %d.%d.%d", Major, Minor, Patch);
            MILAN_INFOW(L"renderdoc: capture count = %u", CaptureCount);

            return true;
        });


    if (mb_disableRenderdoc)
        return;


    COMPONENT(CCommands).RegisterCommand(L"rdoc", L"start",
        [&](wchar_t const*)
        {
            if (mp_apiHandle == nullptr)
            {
                MILAN_WARNW(L"renderdoc: not initialized");
                return true;
            }

            if (mp_apiHandle->IsFrameCapturing() == 1)
            {
                MILAN_WARNW(L"renderdoc: already capturing");
                return true;
            }

            MILAN_INFOW(L"renderdoc: starting capture");

            mp_apiHandle->StartFrameCapture(nullptr, nullptr);
            if (mp_apiHandle->IsFrameCapturing() != 1)
            {
                MILAN_WARNW(L"renderdoc: failed to start capture");
                return true;
            }

            return true;
        });

    COMPONENT(CCommands).RegisterCommand(L"rdoc", L"end",
        [&](wchar_t const*)
        {
            if (mp_apiHandle == nullptr)
            {
                MILAN_WARNW(L"renderdoc: not initialized");
                return true;
            }

            if (mp_apiHandle->IsFrameCapturing() != 1)
            {
                MILAN_WARNW(L"renderdoc: not capturing");
                return true;
            }

            MILAN_INFOW(L"renderdoc: terminating capture");

            if (mp_apiHandle->EndFrameCapture(nullptr, nullptr) != 1)
            {
                MILAN_WARNW(L"renderdoc: failed to terminate capture");
                return true;
            }

            return true;
        });
}
