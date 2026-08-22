#pragma once

#include <format>
#include <entt/fwd.hpp>

template<>
struct std::formatter<entt::entity>
{
	std::formatter<uint32_t> elem_fmt;

	constexpr auto parse(std::format_parse_context& ctx)
	{
		return elem_fmt.parse(ctx);
	}

	auto format(const entt::entity &e, std::format_context& ctx) const
	{
		auto out_iter = ctx.out();
		*out_iter++ = '[';
		elem_fmt.format(uint32_t(e), ctx);
		*out_iter++ = ']';

		return out_iter;
	}
};

