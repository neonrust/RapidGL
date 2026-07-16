#include "postprocess.h"
#include <string_view>
using namespace std::literals;

#include <assert.h>

namespace RGL::PP
{

std::string_view qualityName(Quality q)
{
	static const std::string_view names[] = {
		"PossPoor"sv,
		"Low"sv,
		"Medium"sv,
		"High"sv,
		"Super"sv,
		"Insane"sv,
	};
	assert(q >= Quality::PissPoor and q <= Quality::Insane);
	return names[int_fast8_t(q) + 2];
}


void PostProcess::resize(uint32_t width, uint32_t height)
{
	_width = width;
	_height = height;
}

} // RGL
