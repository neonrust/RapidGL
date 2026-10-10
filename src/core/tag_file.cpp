#include "tag_file.h"
#include "log.h"

#include <cstring>

namespace RGL
{

namespace fs = std::filesystem;

// const tag_file::entry nullentry { std::string_view(), std::string_view() };

tag_file::tag_file(const fs::path &file_path) :
	_line_num(0),
	_file_path(file_path)
{
	_fp = open();
	_value.reserve(255);
}

// ----------------------------------------------------------------------------

tag_file::~tag_file()
{
	close();
}

// ----------------------------------------------------------------------------

static int hex_digit(char digit)
{
	if(std::isdigit(digit))
		return digit - '0';
	else if(digit >= 'a' and digit <= 'f')
		return digit - 'a' + 10;
	else if(digit >= 'A' and digit <= 'F')
		return digit - 'A' + 10;
	return -1;
};

// ----------------------------------------------------------------------------

std::pair<std::string_view, std::string_view> tag_file::next()
{
	if(not _fp)
		return {};

	size_t tag_length;

	while(true)
	{
		++_line_num;

		const auto line_start_pos = std::ftell(_fp);
		if(line_start_pos == -1)
		{
			close();
			return {};
		}
		_tag.resize(TagLength);
		tag_length = std::fread(_tag.data(), 1, TagLength, _fp);
		if(tag_length != TagLength) // read was short, i.e. probably EOF
		{
			// only output error if we read something non-empty and non-comment
			if(tag_length > 0 and not is_comment() and _tag[0] != '\n')
			{
				Log::error("[{}:{}] failed to read tag (8 bytes)", _file_path, _line_num);
				break;
			}
			// nothing relevant was read; this is normal -> just exit
			close();
			return {};
		}
		// trim to first space
		if(auto pos = _tag.find(' '); pos != std::string::npos)
			_tag.resize(pos);

		if(is_comment())
		{
			// skip the whole line and try again
			const std::string_view tag{ _tag };
			const auto lf_at = tag.find('\n');
			if(lf_at != std::string_view::npos)
			{
				// what we already read contained a LF, rewind to character following that LF
				std::fseek(_fp, line_start_pos + long(lf_at) + 1, SEEK_SET);
			}
			else if(not skip_line())
			{
				close();
				return {};
			}
			continue;
		}
		if(_tag[0] == '\n')
		{
			// just skip the LF and try again
			std::fseek(_fp, line_start_pos + 1, SEEK_SET);
			continue;
		}
		break;
	}

	auto highc = std::getc(_fp);
	if(highc == EOF)
	{
		Log::error("[{}:{}]: EOF while reading value size", _file_path, _line_num);
		close();
		return {};
	}
	int lowc = std::getc(_fp);
	if(lowc == EOF)
	{
		Log::error("[{}:{}]: EOF while reading value size", _file_path, _line_num);
		close();
		return {};
	}

	auto high = hex_digit(char(highc));
	auto low = hex_digit(char(lowc));

	if(high == -1 or low == -1)
	{
		Log::error("[{}:{}]: bad value size: {}{} (expected hex)", _file_path, _line_num, highc, lowc);
		close();
		return {};
	}

	size_t value_length = size_t(high*16 + low);
	if(value_length > ValueLength)
	{
		Log::error("[{}:{}]: failed to read value; too large: ", _file_path, _line_num, value_length);
		close();
		return {};
	}

	{
		_value.resize(value_length);
		auto could_read = std::fread(_value.data(), 1, value_length, _fp);
		if(could_read != value_length)
		{
			Log::error("[{}:{}]: failed to read value of size {}: {}", _file_path, _line_num, value_length, std::strerror(errno));
			close();
			return {};
		}
	}

	// all the above should be immediately followed by a LF
	auto tail = std::getc(_fp);
	if(tail != '\n')
	{
		Log::error("[{}:{}]: expected LF, got {} {:02x}", _file_path, _line_num, char(tail), uint8_t(tail));
		Log::error(" ... after reading tag '{}' and value '{}'", _tag, _value);
		close();
		return {};
	}

	if(std::feof(_fp))
		close();

	// Log::debug("tag_file| '{}' :: '{}'", tag(), value());

	return { tag(), value() };
}

// ----------------------------------------------------------------------------

std::FILE *tag_file::open()
{
	auto *fp = std::fopen(_file_path.native().c_str(), "rb");
	if(not fp)
	{
		if(errno == ENOENT)
			Log::error("[{}]: file not found", _file_path.native());
		else
			Log::error("[{}]: failed to open file: {}", _file_path.native(), std::strerror(errno));
		return {};
	}

	return fp;
}

// ----------------------------------------------------------------------------

void tag_file::close()
{
	if(_fp)
		std::fclose(_fp);
	_fp = nullptr;
}

// ----------------------------------------------------------------------------

bool tag_file::skip_line()
{
	int c;
	do
	{
		c = std::getc(_fp);
	}
	while(c != '\n' and c != EOF);

	return c != EOF;
}

// ----------------------------------------------------------------------------

bool tag_file::is_comment() const
{
	return _tag[0] == ' ' or _tag[0] == '\t' or _tag[0] == '#';
}

} // RGL