#pragma once

#include <filesystem>
#include <string_view>
#include <cstdio>
#include <cstdint>

// Extremely simple tag-value file format:
//
// <tag><size><value>LF
//   <tag> is always 8 bytes
//   <size> is a 2-digit hexadecimal value denoting the length of <value> (so, max 255)
//   <length> is <size> bytes
//   LF
// exceptions:
//   starts with space, tab or '#' -> a comment; line skipped


namespace RGL
{


class tag_file
{
public:
	static constexpr size_t TagLength = 8;
	static constexpr size_t ValueLength = 256;

public:
	tag_file(const std::filesystem::path &file_path);
	~tag_file();

	inline operator bool () const { return bool(_fp); }

	// returned key & value are the same buffers every time (valid until next call)
	std::pair<std::string_view, std::string_view> next();
	inline std::pair<std::string_view, std::string_view> current() const { return { tag(), value() }; }

	inline std::string_view tag() const { return _tag; }
	inline std::string_view value() const { return _value; }

	inline uint32_t line_num() const { return _line_num; }

private:
	static std::FILE *open(std::string_view filename);
	void close();
	bool skip_line();
	bool is_comment() const;
	
private:
	std::FILE *_fp;
	uint32_t _line_num { 0 };
	std::string_view _filename;

	std::string _tag;
	std::string _value;
};

} // RGL