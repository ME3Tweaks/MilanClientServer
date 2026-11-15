#pragma once

#ifdef WITH_IMGUI

#include <array>
#include "Base/Base.hpp"
#include "Containers/string.hpp"
#include "Containers/vector.hpp"
#include "Engine/Hooks.hpp"


class TeamChat final : public NonCopyable
{
public:

    bool HasActiveInput() const;
    void OnShowChat();
    void OnReceiveMessage(bool bLocal, FString const& PlayerName, FString const& Text);
    void OnRender();

private:

    void RenderActive();
    void RenderPassive();
    void SendInputMessage();

    struct MessageRecord final
    {
        bool            bLocal{};
        game::string    TimeStr{};
        game::string    PlayerName{};
        game::string    Text{};
    };

    game::vector<MessageRecord>     Messages{};
    std::array<char, 256>           InputBuffer{};
    bool                            bScrollToBottom = true;
    bool                            bActive = false;
    bool                            bFocusInput = false;
};

#endif
