#include "Shell/CmdArgs.hpp"


wchar_t const* CmdArgs::Get() noexcept
{
    static wchar_t const* String = nullptr;

    if (String == nullptr)  [[unlikely]]
    {
        // Algorithm originates from Wine's implementation.
        // https://stackoverflow.com/a/36876057

        wchar_t* Args = ::GetCommandLineW();
        if (*Args == L'"')
        {
            ++Args;
            while (*Args)
            {
                if (*Args++ == L'"')
                    break;
            }
        }
        else
        {
            while (*Args && *Args != L' ' && *Args != L'\t')
                ++Args;
        }

        String = Args;
    }

    return String;
}

wchar_t const* CmdArgs::GetWithExec() noexcept
{
    static wchar_t const* String = nullptr;
    if (String == nullptr) String = ::GetCommandLineW();
    return String;
}

bool CmdArgs::Contain(wchar_t const* const Needle) noexcept
{
    return std::wcsstr(::CmdArgs::Get(), Needle) != nullptr;
}
