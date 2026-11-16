#pragma once

#ifdef WITH_IMGUI

#include <array>
#include <atomic>
#include <mutex>
#include "Base/Base.hpp"
#include "Containers/string.hpp"
#include "Containers/vector.hpp"
#include "Engine/Hooks.hpp"


class TeamChat final : public NonCopyable
{
public:

    /** Window is open: it renders the chat and replaces the game's HUD chat. */
    bool IsOpen() const;
    /** Window is open and owns keyboard input (the only state where input is intercepted). */
    bool IsFocused() const;

    void OnShowChat();
    void OnDrainQueue();
    void OnReceiveNotification();
    void OnReceiveMessage(bool bLocal, FString const& PlayerName, FString const& Text);
    void OnRender();

private:

    void DrainQueueInner();
    void RenderActive();
    void RenderPassive();
    bool SendInputMessage();

    struct MessageRecord final
    {
        bool            bLocal{};
        game::string    TimeStr{};
        game::string    PlayerName{};
        game::string    Text{};
    };

    game::vector<MessageRecord>     Messages{};
    std::array<char, 256>           InputBuffer{};
    game::vector<FString>           QueuedSentMessages{};
    std::mutex                      QueuedSentMutex{};
    std::mutex                      MessagesMutex{};
    bool                            bScrollToBottom{ true };
    bool                            bActive{ false };
    bool                            bFocusInput{ false };
    bool                            bLastInputActive{ false };
    bool                            bWasActive{ false };
    bool                            bWasFocused{ false };
    std::atomic<bool>               bFocused{ false };
    std::atomic<bool>               bResetInput{ false };
};

#endif
