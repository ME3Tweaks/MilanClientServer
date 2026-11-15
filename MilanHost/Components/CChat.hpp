#pragma once

#ifdef WITH_IMGUI

#include "Base/Base.hpp"
#include "Components/CImgui.hpp"
#include "Engine/Hooks.hpp"
#include "Online/TeamChat.hpp"


// ! ImGui interface.
// ========================================

class ChatInterface final : public CImgui::IInterface
{
public:

    explicit ChatInterface(std::shared_ptr<TeamChat> InTeamChat);
    virtual ~ChatInterface() override = default;

    virtual SFXName GetName() const override;
    virtual bool IsRendering() const override;
    virtual bool IsIntercepting() const override;

    virtual void OnWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) override;
    virtual void OnRender() override;

private:

    std::shared_ptr<TeamChat> m_teamChat{};
};


// ! Component.
// ========================================

class CChat final : public NonCopyable
{
public:

    static constexpr wchar_t const* k_typeName = L"chat";

    bool Initialize(::LESDK::Initializer& Context);
    void Terminate(::LESDK::Initializer& Context);

    bool On_UObject_ProcessInternal_Pre(UObject* Context, FFrame* Stack, void* Result);

private:

    std::shared_ptr<TeamChat> m_teamChat{};
};

static_assert(ComponentType<CChat>);

#endif
