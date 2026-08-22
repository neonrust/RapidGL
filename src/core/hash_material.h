#pragma once

#include "asset/material.h"

namespace std
{
template<>
struct hash<RGL::Material>
{
	[[nodiscard]] inline size_t operator()(const RGL::Material &m) const
	{
		return m.hash();
	}
};

} // std


