/**
 * @file        MilanHost/Components/CDirectories.hpp
 * @header      Component for managing data storage directories.
 */

#pragma once
#include "Base/Base.hpp"
#include "Engine/Hooks.hpp"


class CDirectories final : public NonCopyable
{
public:
    static constexpr wchar_t const* k_typeName = L"directories";

    bool Initialize(::LESDK::Initializer& Context);
    void Terminate(::LESDK::Initializer& Context);

private:
    void PatchLicenseeVersion(::LESDK::Initializer& Context);
    void PatchDocumentsDirectory(::LESDK::Initializer& Context);
};

static_assert(ComponentType<CDirectories>);
