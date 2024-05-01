#include <LESDK/Init.hpp>
#include <LESDK/Headers.hpp>

#include "Base.hpp"
#include "Systems/Output.hpp"
#include "Systems/State.hpp"


class FunctionInterceptLegacy
{
    UStruct* TargetNode{ nullptr };
    wchar_t const* TargetFullName{ nullptr };

public:

    FunctionInterceptLegacy() = default;

    bool Applies(UStruct* const Node, wchar_t const* const FullName)
    {
        MILAN_CHECKW(Node != nullptr, L"");

        if (TargetFullName == nullptr)
        {
            MILAN_CHECKW(FullName != nullptr, L"");
            MILAN_CHECKW(::wcslen(FullName) > 0, L"");
            TargetFullName = FullName;
        }

        if (TargetNode == nullptr)
        {
            MILAN_CHECKW(TargetFullName != nullptr, L"");
            TargetNode = UObject::FindObject<UStruct>(TargetFullName);
        }

        return TargetNode == Node;
    }

};


namespace Milan::State
{
    void Initialize(::LESDK::Initializer& Context)
    {
        MILAN_UNUSED(Context);
        MILAN_INFOW(L"state system initialized");
    }

    void Terminate()
    {
        MILAN_INFOW(L"state system terminating");
    }

    bool On_UObject_ProcessInternal(UObject* const Context, FFrame* const Stack, void* const Result)
    {
        MILAN_UNUSED_3(Context, Stack, Result);

        static FunctionInterceptLegacy SFXSaveManagerMP_Initialize{};
        if (SFXSaveManagerMP_Initialize.Applies(Stack->Node, L"Function SFXGame.SFXSaveManagerMP.Initialize"))
        {
            MILAN_WARNW(L"SFXGame.SFXSaveManagerMP.Initialize");
        }

        static FunctionInterceptLegacy SFXSaveManagerMP_SaveRecordsImpl{};
        if (SFXSaveManagerMP_SaveRecordsImpl.Applies(Stack->Node, L"Function SFXGame.SFXSaveManagerMP.SaveRecordsImpl"))
        {
            MILAN_WARNW(L"SFXGame.SFXSaveManagerMP.SaveRecordsImpl");
        }

        return true;
    }

    void On_UWorld_Tick(UWorld* const Context, int const Unknown, float const Delta)
    {
        MILAN_UNUSED_3(Context, Unknown, Delta);
    }
}
