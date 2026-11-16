#ifdef WITH_IMGUI

#include "Components/CChat.hpp"
#include "Engine/Intercept.hpp"


// ! ChatInterface implementation.
// ========================================

ChatInterface::ChatInterface(std::shared_ptr<TeamChat> InTeamChat)
    : m_teamChat{ std::move(InTeamChat) }
{
    // ...
}

SFXName ChatInterface::GetName() const
{
    return SFXName(L"chat", 0);
}

bool ChatInterface::IsRendering() const
{
    return true;
}

bool ChatInterface::IsIntercepting() const
{
    return m_teamChat->IsFocused();
}

void ChatInterface::OnWndProc(HWND const hWnd, UINT const uMsg, WPARAM const wParam, LPARAM const lParam)
{
    MILAN_UNUSED_4(hWnd, uMsg, wParam, lParam);
}

void ChatInterface::OnRender()
{
    m_teamChat->OnRender();
}


// ! CChat implementation.
// ========================================

bool CChat::Initialize(::LESDK::Initializer& Context)
{
    MILAN_UNUSED(Context);
    m_teamChat = std::make_shared<TeamChat>();
    COMPONENT(CImgui).RegisterInterface(std::make_shared<ChatInterface>(m_teamChat));
    MILAN_DEBUGW(ELOG_Startup, L"component '%s' initialized", k_typeName);
    return true;
}

void CChat::Terminate(::LESDK::Initializer& Context)
{
    MILAN_UNUSED(Context);
    MILAN_DEBUGW(ELOG_Teardown, L"component '%s' terminating", k_typeName);
    m_teamChat = nullptr;
}

bool CChat::On_UObject_ProcessInternal_Pre(UObject* const Context, FFrame* const Stack, void* const Result)
{
    MILAN_UNUSED_3(Context, Stack, Result);

    static FunctionNamedIntercept SFXHUDMP_DisplayConsoleMessages{};
    if (SFXHUDMP_DisplayConsoleMessages.Applies(Stack->Node, L"SFXGameMPContent", L"SFXHUDMP", L"DisplayConsoleMessages"))
    {
        m_teamChat->OnDrainQueue();
        return false;
    }

    static FunctionNamedIntercept PlayerController_BioTalk{};
    if (PlayerController_BioTalk.Applies(Stack->Node, L"SFXGameMPContent", L"SFXPlayerControllerMP", L"BioTalk"))
    {
        // Open the ImGui chat instead of the engine console's 'Say' state.
        m_teamChat->OnShowChat();
        return false;
    }

    static FunctionNamedIntercept PlayerController_TeamMessage{};
    if (PlayerController_TeamMessage.Applies(Stack->Node, L"SFXGameMPContent", L"SFXPlayerControllerMP", L"TeamMessage"))
    {
        auto* const Parms = reinterpret_cast<APlayerController_eventTeamMessage_Parms*>(Stack->Locals);

        if (Parms->Type.ToString() == L"Say" && Parms->PRI != nullptr)
        {
            auto const Owner = Parms->PRI->Owner != nullptr ? Parms->PRI->Owner->Cast<APlayerController>() : nullptr;
            bool const bLocalMessage = Owner != nullptr && Owner->Player != nullptr && Owner->Player->Cast<ULocalPlayer>() != nullptr;
            m_teamChat->OnReceiveMessage(bLocalMessage, Parms->PRI->PlayerName, Parms->S);
            m_teamChat->OnReceiveNotification();
            return false;
        }
    }

    return true;
}

#endif
