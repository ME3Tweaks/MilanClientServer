/**
 * @file        MilanHost/Components/CScreenshots.hpp
 * @header      Component for PNG screenshots, ported from the original ASI.
 */

#pragma once
#include "Base/Base.hpp"
#include "Engine/Hooks.hpp"
#include <filesystem>


#define GLOBAL_APPCREATEBITMAP_RVA  ::LESDK::Address::FromOffset(0x119ED0)

using t_appCreateBitmap = void(wchar_t* pattern, int width, int height, FColor* data, void* fileManager);
extern t_appCreateBitmap* appCreateBitmap_orig;

void appCreateBitmap_hook(wchar_t* inputBaseName, int width, int height, FColor* data, void* fileManager);


class CScreenshots final : public NonCopyable
{
public:

    static constexpr wchar_t const* k_typeName = L"screenshots";

    bool Initialize(::LESDK::Initializer& Context);
    void Terminate(::LESDK::Initializer& Context);

};

static_assert(ComponentType<CScreenshots>);
