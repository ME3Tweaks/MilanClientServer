/**
 * @file        MilanHost/Containers/vector.hpp
 * @header      Wrapper for @c std::vector using @c ::game::allocator
 */

#pragma once

#include <vector>
#include "Base/Base.hpp"

namespace game
{
    template<class T>
    using vector = std::vector<T, ::game::allocator<T>>;
}
