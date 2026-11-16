#include "Online/TeamChat.hpp"

#ifdef WITH_IMGUI

#include <Windows.h>
#include <cstdio>
#include <cstring>

#include <imgui.h>
#include <imgui_internal.h>

#include "Components/CImgui.hpp"


namespace
{
    game::string ToUtf8(FString const& Text)
    {
        UINT const Length = Text.Length();
        if (Length == 0)
        {
            return {};
        }

        UINT const Utf8Length = LESDK::GetUtf8LengthWide(Text.Chars(), Length);
        game::string Output(Utf8Length, '\0');

        DWORD WinError = 0;
        if (!LESDK::EncodeUtf8FromWide(Text.Chars(), Length, Output.data(), Utf8Length, &WinError))
        {
            MILAN_WARNW(L"TeamChat: failed to encode a %u-character string as UTF-8 (Win32 error %lu)", Length, WinError);
            return {};
        }

        return Output;
    }
}


// ! TeamChat implementation.
// ========================================

bool TeamChat::IsOpen() const
{
    return bActive;
}

bool TeamChat::IsFocused() const
{
    return bActive && bFocused.load();
}

void TeamChat::OnShowChat()
{
    bActive = true;
    bFocused.store(true);
    bScrollToBottom = true;
    bFocusInput = true;
    bResetInput.store(true);
}

void TeamChat::OnDrainQueue()
{
    std::unique_lock _{ QueuedSentMutex };
    DrainQueueInner();
    QueuedSentMessages.clear();
}

void TeamChat::OnReceiveNotification()
{
    if (!bActive)
    {
        bActive = true;
        bFocused.store(false);
        bFocusInput = false;
        bScrollToBottom = true;
        return;
    }

    if (!IsFocused())
    {
        bScrollToBottom = true;
    }
}

void TeamChat::OnReceiveMessage(bool const bLocal, FString const& PlayerName, FString const& Text)
{
    SYSTEMTIME LocalTime{};
    ::GetLocalTime(&LocalTime);
    FString const TimeStr = FString::Printf(L"%02d:%02d:%02d", LocalTime.wHour, LocalTime.wMinute, LocalTime.wSecond);

    MILAN_TRACEW(L"TeamChat::OnReceiveMessage - %s : %s (at %s)", *PlayerName, *Text, *TimeStr);

    std::unique_lock _{ MessagesMutex };
    MessageRecord& Message = Messages.emplace_back();
    Message.bLocal = bLocal;
    Message.TimeStr = ToUtf8(TimeStr);
    Message.PlayerName = ToUtf8(PlayerName);
    Message.Text = ToUtf8(Text);
}

void TeamChat::OnRender()
{
    bool const bOpen = bActive;
    bool const bFocusedNow = bOpen && bFocused.load();

    if (bResetInput.exchange(false) || (bWasActive && !bOpen) || (bWasFocused && !bFocusedNow))
    {
        ImGui::GetIO().ClearInputKeys();
        ImGui::ClearActiveID();
    }

    bWasActive = bOpen;
    bWasFocused = bFocusedNow;

    bOpen ? RenderActive() : RenderPassive();
}

void TeamChat::DrainQueueInner()
{
    for (FString const& Message : QueuedSentMessages)
    {
        if (Message.Equals(L"/clear", true))
        {
            std::unique_lock _{ MessagesMutex };
            Messages.clear();
            return;
        }

        if (Message.Equals(L"/help", true))
        {
            OnReceiveMessage(true, L"(system)", L"type /clear to clear chat history");
            OnReceiveMessage(true, L"(system)", L"type /help to show this message");
            OnReceiveMessage(true, L"(system)", L"type /hide to close chat window");
            return;
        }

        if (Message.Equals(L"/hide", true))
        {
            bActive = false;
            return;
        }

        if (Message.StartsWith(L"/", true))
        {
            OnReceiveMessage(true, L"(system)", L"unrecognized slash command");
            return;
        }

        (*GEngine)->Cast<USFXEngine>()->GetRealWorldInfo()->GetLocalPlayerController()->Say(Message);
    }
}

void TeamChat::RenderActive()
{
    bool bInputFocused = bActive && bFocused.load();

    bool const bFaded = bActive && !bInputFocused;
    if (bFaded)
    {
        ImVec4 const WindowBackground = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
        ImGui::PushStyleColor(ImGuiCol_WindowBg,
            ImVec4(WindowBackground.x, WindowBackground.y, WindowBackground.z, WindowBackground.w * 0.45f));

        ImVec4 const ChildBackground = ImGui::GetStyle().Colors[ImGuiCol_ChildBg];
        ImGui::PushStyleColor(ImGuiCol_ChildBg,
            ImVec4(ChildBackground.x, ChildBackground.y, ChildBackground.z, ChildBackground.w * 0.45f));

        ImVec4 const TitleBackground = ImGui::GetStyle().Colors[ImGuiCol_TitleBgActive];
        ImGui::PushStyleColor(ImGuiCol_TitleBgActive,
            ImVec4(TitleBackground.x, TitleBackground.y, TitleBackground.z, TitleBackground.w * 0.45f));
    }

    ImGui::SetNextWindowPos(ImVec2(30, 300), ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2(600, 240), ImGuiCond_Once);

    ImGuiWindowFlags WindowFlags =
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoCollapse;

    if (!bInputFocused)
    {
        WindowFlags |= ImGuiWindowFlags_NoResize;
    }

    bool* const pOpen = bInputFocused ? &bActive : nullptr;

    if (!ImGui::Begin("##Team chat", pOpen, WindowFlags))
    {
        bFocusInput = false;
        bLastInputActive = false;
        bFocused.store(false);
        InputBuffer.front() = '\0';
        ImGui::End();
        if (bFaded)
        {
            ImGui::PopStyleColor(3);
        }
        return;
    }

    if (!bActive)
    {
        bFocusInput = false;
        bLastInputActive = false;
        bFocused.store(false);
        InputBuffer.front() = '\0';
    }

    if (bInputFocused && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        ImVec2 const WindowPosition = ImGui::GetWindowPos();
        ImVec2 const WindowSize = ImGui::GetWindowSize();
        if (!ImGui::IsMouseHoveringRect(WindowPosition,
                ImVec2(WindowPosition.x + WindowSize.x, WindowPosition.y + WindowSize.y), false))
        {
            bFocused.store(false);
            bInputFocused = false;
            bFocusInput = false;
        }
    }

    if (bInputFocused && ImGui::IsKeyPressed(ImGuiKey_Escape))
    {
        bFocused.store(false);
        bInputFocused = false;
        bFocusInput = false;
        ImGui::ClearActiveID();
    }

    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarPadding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 5.0f);
    float const FooterHeightReserved = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();
    if (ImGui::BeginChild("ScrollingRegion", ImVec2(0, -FooterHeightReserved), false, ImGuiWindowFlags_HorizontalScrollbar))
    {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 4));

        {
            std::unique_lock _{ MessagesMutex };
            for (MessageRecord const& Message : Messages)
            {
                if (Message.bLocal)
                {
                    static const ImVec4 s_localColor = ImColor(255, 219, 88);
                    ImGui::PushStyleColor(ImGuiCol_Text, s_localColor);
                }

                ImGui::TextWrapped("[%s] %s: %s", Message.TimeStr.c_str(), Message.PlayerName.c_str(), Message.Text.c_str());

                if (Message.bLocal)
                {
                    ImGui::PopStyleColor();
                }
            }
        }

        if (std::exchange(bScrollToBottom, false) ||
            (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()))
        {
            ImGui::SetScrollHereY(1.0f);
        }

        ImGui::PopStyleVar();
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleVar();

    ImGui::PushStyleColor(ImGuiCol_NavHighlight, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushItemFlag(ImGuiItemFlags_NoTabStop, true);

    if (bFaded)
    {
        ImVec4 const FrameBackground = ImGui::GetStyle().Colors[ImGuiCol_FrameBg];
        ImGui::PushStyleColor(ImGuiCol_FrameBg,
            ImVec4(FrameBackground.x, FrameBackground.y, FrameBackground.z, FrameBackground.w * 0.45f));

        ImVec4 const FrameHovered = ImGui::GetStyle().Colors[ImGuiCol_FrameBgHovered];
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,
            ImVec4(FrameHovered.x, FrameHovered.y, FrameHovered.z, FrameHovered.w * 0.45f));

        ImVec4 const FrameActive = ImGui::GetStyle().Colors[ImGuiCol_FrameBgActive];
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive,
            ImVec4(FrameActive.x, FrameActive.y, FrameActive.z, FrameActive.w * 0.45f));

        ImVec4 const TextColor = ImGui::GetStyle().Colors[ImGuiCol_Text];
        ImGui::PushStyleColor(ImGuiCol_Text,
            ImVec4(TextColor.x, TextColor.y, TextColor.z, TextColor.w * 0.45f));
    }

    ImGui::PushItemWidth(-50.0f);
    if (bInputFocused && std::exchange(bFocusInput, false))
    {
        ImGui::SetKeyboardFocusHere();
    }
    bool const bEntered = ImGui::InputText("##ChatInput", InputBuffer.data(), InputBuffer.size(),
            ImGuiInputTextFlags_EnterReturnsTrue |
            ImGuiInputTextFlags_NoUndoRedo |
            ImGuiInputTextFlags_ElideLeft);
    bool const bInputActive = ImGui::IsItemActive();
    ImGui::PopItemWidth();

    if (bFaded)
    {
        ImGui::PopStyleColor(4);
    }

    if (bEntered && SendInputMessage())
    {
        bFocused.store(false);
        bFocusInput = false;
        bInputFocused = false;
    }

    bool const bLostFocus = std::exchange(bLastInputActive, bInputActive) && !bInputActive;

    if (bInputFocused && bLostFocus &&
        !ImGui::IsAnyItemActive() &&
        !ImGui::GetIO().WantTextInput &&
        ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
    {
        ImGui::SetKeyboardFocusHere(-1);
    }

    ImGui::SameLine();
    ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);

    if (bFaded)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_DisabledAlpha, 0.45f);
        ImGui::BeginDisabled();
    }

    if (ImGui::Button(">>", ImVec2(40.0f, 0.0f)) && SendInputMessage())
    {
        bFocused.store(false);
        bFocusInput = false;
        bInputFocused = false;
    }

    if (bFaded)
    {
        ImGui::EndDisabled();
        ImGui::PopStyleVar();
    }

    ImGui::PopItemFlag();

    ImGui::PopItemFlag();
    ImGui::PopStyleColor();
    ImGui::End();

    if (bFaded)
    {
        ImGui::PopStyleColor(3);
    }
}

void TeamChat::RenderPassive()
{
    // ...
}

bool TeamChat::SendInputMessage()
{
    if (InputBuffer.front() == '\0')
    {
        return false;
    }

    FString Message{};
    Message.AppendUtf8(InputBuffer.data());
    InputBuffer.front() = '\0';

    std::unique_lock _{ QueuedSentMutex };
    QueuedSentMessages.push_back(std::move(Message));
    return true;
}

#endif

