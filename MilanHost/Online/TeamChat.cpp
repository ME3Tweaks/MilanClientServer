#include "Online/TeamChat.hpp"

#include <Windows.h>
#include <cstdio>
#include <cstring>

#include <imgui.h>

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

bool TeamChat::HasActiveInput() const
{
    return bActive;
}

void TeamChat::OnShowChat()
{
    bActive = true;
    bScrollToBottom = true;
    bFocusInput = true;
}

void TeamChat::OnReceiveMessage(bool const bLocal, FString const& PlayerName, FString const& Text)
{
    SYSTEMTIME LocalTime{};
    ::GetLocalTime(&LocalTime);
    FString const TimeStr = FString::Printf(L"%02d:%02d:%02d", LocalTime.wHour, LocalTime.wMinute, LocalTime.wSecond);

    MILAN_TRACEW(L"TeamChat::OnReceiveMessage - %s : %s (at %s)", *PlayerName, *Text, *TimeStr);

    MessageRecord& Message = Messages.emplace_back();
    Message.bLocal = bLocal;
    Message.TimeStr = ToUtf8(TimeStr);
    Message.PlayerName = ToUtf8(PlayerName);
    Message.Text = ToUtf8(Text);
}

void TeamChat::OnRender()
{
    bActive ? RenderActive() : RenderPassive();
}

void TeamChat::RenderActive()
{
    ImGui::SetNextWindowSize(ImVec2(450, 350), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("Team chat", &bActive, ImGuiWindowFlags_NoScrollbar)) {
        ImGui::End();
        return;
    }

    float const FooterHeightReserved = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();
    if (ImGui::BeginChild("ScrollingRegion", ImVec2(0, -FooterHeightReserved), false, ImGuiWindowFlags_HorizontalScrollbar))
    {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 4));

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

        if (std::exchange(bScrollToBottom, false) ||
            (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()))
        {
            ImGui::SetScrollHereY(1.0f);
        }

        ImGui::PopStyleVar();
    }

    ImGui::EndChild();
    ImGui::Separator();

    bool bReclaimFocus = std::exchange(bFocusInput, false);

    ImGui::PushItemWidth(-60.0f);
    if (ImGui::InputText("##ChatInput", InputBuffer.data(), InputBuffer.size(), ImGuiInputTextFlags_EnterReturnsTrue))
    {
        SendInputMessage();
        bReclaimFocus = true;
    }
    ImGui::PopItemWidth();

    ImGui::SetItemDefaultFocus();
    if (bReclaimFocus)
    {
        ImGui::SetKeyboardFocusHere(-1);
    }

    ImGui::SameLine();
    if (ImGui::Button("Send", ImVec2(50.0f, 0.0f)))
    {
        SendInputMessage();
        ImGui::SetKeyboardFocusHere(-1);
    }

    ImGui::End();
}

void TeamChat::RenderPassive()
{
    // ...
}

void TeamChat::SendInputMessage()
{
    if (InputBuffer.front() != '\0')
    {
        FString Message{};
        Message.AppendUtf8(InputBuffer.data());
        InputBuffer.front() = '\0';

        if (Message.Equals(L"/clear", true))
        {
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
