#include <renderdoc/renderdoc_app.h>

#include "Milan/Base.hpp"
#include "Systems/Console.hpp"
#include "Systems/Imgui.hpp"
#include "Systems/Output.hpp"
#include "Systems/Renderdoc.hpp"

namespace Milan::Renderdoc
{
    RENDERDOC_API_1_3_0* gp_apiHandle = nullptr;


    // ! Global subsystem functions.
    // ========================================

    void Initialize(::LESDK::Initializer& Context)
    {
        MILAN_UNUSED(Context);

        Console::RegisterCommand(L"rdoc", L"status",
            [](wchar_t const*)
            {
                if (gp_apiHandle == nullptr)
                {
                    MILAN_WARNW(L"renderdoc: not initialized");
                    return true;
                }

                MILAN_INFOW(L"renderdoc: attached");

                int Major = 0, Minor = 0, Patch = 0;
                gp_apiHandle->GetAPIVersion(&Major, &Minor, &Patch);
                int CaptureCount = gp_apiHandle->GetNumCaptures();

                MILAN_INFOW(L"renderdoc: version = %d.%d.%d", Major, Minor, Patch);
                MILAN_INFOW(L"renderdoc: capture count = %u", CaptureCount);

                return true;
            });

        auto const ModuleHandle = ::GetModuleHandleA("renderdoc.dll");
        if (ModuleHandle == nullptr)
        {
            // This is not an error because RenderDoc is likely not to be attached.
            MILAN_INFOW(L"failed to load renderdoc.dll");
            return;
        }

        auto const pGetApi = reinterpret_cast<pRENDERDOC_GetAPI>(GetProcAddress(ModuleHandle, "RENDERDOC_GetAPI"));
        if (pGetApi == nullptr)
        {
            MILAN_ERRORW(L"failed to get RENDERDOC_GetAPI pointer");
            return;
        }

        if (pGetApi(eRENDERDOC_API_Version_1_3_0, (void**)&gp_apiHandle) != 1)
        {
            MILAN_ERRORW(L"failed to get initialize gp_apiHandle");
            return;
        }

        gp_apiHandle->UnloadCrashHandler();

        Console::RegisterCommand(L"rdoc", L"start",
            [](wchar_t const*)
            {
                if (gp_apiHandle == nullptr)
                {
                    MILAN_WARNW(L"renderdoc: not initialized");
                    return true;
                }

                if (gp_apiHandle->IsFrameCapturing() == 1)
                {
                    MILAN_WARNW(L"renderdoc: already capturing");
                    return true;
                }

                //auto const [Device, Window] = Imgui::GetRenderingGlobals();
                //MILAN_DEBUGW(L"renderdoc: starting capture on device 0x%p, window 0x%p", (void*)Device, (void*)Window);
                gp_apiHandle->StartFrameCapture(nullptr, nullptr);

                if (gp_apiHandle->IsFrameCapturing() != 1)
                {
                    MILAN_WARNW(L"renderdoc: failed to start capture");
                    return true;
                }

                return true;
            });

        Console::RegisterCommand(L"rdoc", L"end",
            [](wchar_t const*)
            {
                if (gp_apiHandle == nullptr)
                {
                    MILAN_WARNW(L"renderdoc: not initialized");
                    return true;
                }

                if (gp_apiHandle->IsFrameCapturing() != 1)
                {
                    MILAN_WARNW(L"renderdoc: not capturing");
                    return true;
                }

                //auto const [Device, Window] = Imgui::GetRenderingGlobals();
                //MILAN_DEBUGW(L"renderdoc: ending capture on device 0x%p, window 0x%p", (void*)Device, (void*)Window);
                if (gp_apiHandle->EndFrameCapture(nullptr, nullptr) != 1)
                {
                    MILAN_WARNW(L"renderdoc: failed to end capture");
                    return true;
                }

                return true;
            });

        MILAN_INFOW(L"renderdoc system initialized");
    }

    void Terminate()
    {
        MILAN_INFOW(L"renderdoc system terminating");
    }


    // ! Public interface.
    // ========================================

    void On_VectoredException(void* const Address, DWORD const Code, _EXCEPTION_POINTERS* const Exceptions)
    {
        MILAN_UNUSED_3(Address, Code, Exceptions);

#ifndef _FINAL_RELEASE
        // ...
#endif
    }

    void On_UObject_ProcessEvent(UObject* const Context, UFunction* const Function, void* const Parms, void* const Result)
    {
        MILAN_UNUSED_4(Context, Function, Parms, Result);

#ifndef _FINAL_RELEASE
        // ...
#endif
    }
}
