/**
 * @file        MilanHost/Containers/string.hpp
 * @header      Wrapper for @c std::string using @c ::game::allocator
 */

#pragma once

#include <string>
#include "Milan/Base.hpp"

namespace game
{
    using string = std::basic_string<char, std::char_traits<char>, ::game::allocator<char>>;
    using wstring = std::basic_string<wchar_t, std::char_traits<wchar_t>, ::game::allocator<wchar_t>>;
}
