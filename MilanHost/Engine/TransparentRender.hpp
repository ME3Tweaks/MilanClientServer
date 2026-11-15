#pragma once
#include <vector>
#include "Base/Base.hpp"

class CCommands;

namespace TransparentRender
{
    bool IsTransparentRenderEnabled();
    void RegisterCommands(CCommands& Commands);
    void OnProcessEvent(UFunction* Function);
    bool HandleCapture(wchar_t const* inputBaseName, int width, int height, std::vector<unsigned char> const& rgb);
}
