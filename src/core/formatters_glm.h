#pragma once

#include <format>
#include <glm/glm.hpp>


template<glm::length_t L, typename T, glm::qualifier Q>
struct std::formatter<glm::vec<L, T, Q>>
{
	std::formatter<T> elem_fmt;

	constexpr auto parse(std::format_parse_context& ctx)
	{
		return elem_fmt.parse(ctx);
	}

	auto format(const glm::vec<L, T, Q> &v,
				std::format_context& ctx) const
	{
		auto out_iter = ctx.out();
		*out_iter++ = '{';

		for(auto idx = 0; idx < L; ++idx)
		{
			out_iter = elem_fmt.format(v[idx], ctx);
			if (idx + 1 < L)
				*out_iter++ = ';';
		}

		*out_iter++ = '}';
		return out_iter;
	}
};

template <glm::length_t C, glm::length_t R, typename T, glm::qualifier Q>
struct std::formatter<glm::mat<C, R, T, Q>> {
	std::formatter<glm::vec<R, T, Q>> col_fmt;  // row formatter, i.e. a vector

	constexpr auto parse(std::format_parse_context &ctx)
	{
		return col_fmt.parse(ctx);
	}

	auto format(const glm::mat<C, R, T, Q> &m, std::format_context &ctx) const
	{
		auto out_iter = ctx.out();
		for(auto col = 0; col < R; ++col)
		{
			out_iter = col_fmt.format(m[col], ctx);
			if(col + 1 < C)
				*out_iter++ = '\n';
		}
		return out_iter;
	}
};
