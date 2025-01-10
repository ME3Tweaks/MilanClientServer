/**
 * @file        MilanHost/Containers/unordered_map.hpp
 * @header      Wrapper for @c std::unordered_map using @c ::game::allocator
 */

#pragma once

#include <unordered_map>
#include "Base/Base.hpp"

namespace game
{
    template<class Key, class T, class Hash = std::hash<Key>, class KeyEqual = std::equal_to<Key>>
    using unordered_map = std::unordered_map<Key, T, Hash, KeyEqual, ::game::allocator<std::pair<const Key, T>>>;
}
