#pragma once

#include <ankerl/unordered_dense.h>

namespace hash
{

struct stringv
{
	using is_transparent = std::true_type;

	inline auto operator () (std::string_view str) const noexcept
	{
		return std::hash<std::string_view>()(str);
	}

	inline bool operator () (std::string_view l, std::string_view r) const noexcept
	{
		return l == r;
	}
};

} // NS: hash

template<typename KeyT, typename ValueT>
using dense_map = ankerl::unordered_dense::map<KeyT, ValueT>;

template<typename ValueT>
using string_map = ankerl::unordered_dense::map<std::string, ValueT, hash::stringv, hash::stringv>;

