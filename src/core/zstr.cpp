#include "zstr.h"

#include <algorithm>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <unordered_map>
#include <unordered_set>

#include <cctype>

namespace zstr
{

//---------------------------------------------------------------------------

std::string_view strip(std::string_view s, std::string_view chars)
{
	if(chars.empty())
		return s;

	std::string result;

	// leading chars
	auto start = s.find_first_not_of(chars);
	if(start == std::string::npos)
		return "";

	// trailing chars
	auto end = s.find_last_not_of(chars);

	return s.substr(start, end - start + 1);
}

//---------------------------------------------------------------------------

std::vector<std::string_view> split(std::string_view input, std::string_view delimiters)
{
	std::vector<std::string_view> values;
	if(zstr::strip(input, delimiters).empty())
		return values;

	if(delimiters.empty())
	{
		values.push_back(input);
		return values;
	}

	// find all delimiter-separated sub strings
	std::size_t end = 0;
	std::size_t start = input.find_first_not_of(delimiters);
	do
	{
		auto terminator = (input[start] == '"' ? "\"" : input[start] == '\'' ? "'" : delimiters);
		bool quoted = (terminator == "'" || terminator == "\"");
		if(quoted)
			++start;  // skip begin quote

		end = input.find_first_of(terminator, start); // find next terminator

		values.push_back(input.substr(start, end - start));

		if(quoted && end != std::string::npos)
			++end;  // skip end quote

		// first start of next string
		start = input.find_first_not_of(delimiters, end);
	}
	while (end != std::string::npos && start != std::string::npos);

	return values;
}

//---------------------------------------------------------------------------

// perform a split similar to python's shlex.split()
std::vector<std::string_view> lexsplit(std::string_view input)
{
	std::vector<std::string_view> values;
	if(zstr::strip(input).empty())
		return values;

	static const std::string delimiters = " \t\n";
	
	static const int TEXT = 0, DELIMITER = 1, QUOTE = 2;
	int state = 0;
	
	std::string value;
	char quoteChar = -1;
	
	for(auto iter = input.begin(); iter != input.end(); ++iter)
	{
		char c = *iter;

		switch(state)
		{
			case TEXT:
				if(delimiters.find(c) != std::string::npos)
				{
					if(! value.empty())
					{
						values.push_back(value);
						value.clear();
					}
					state = DELIMITER;
				}
				else if(c == '\'' || c == '"')
				{
					state = QUOTE;
					quoteChar = c;
				}
				else
					value += c;
				break;

			case DELIMITER:
				if(delimiters.find(c) != std::string::npos)
				{
				}
				else if(c == '\'' || c == '"')
				{
					state = QUOTE;
					quoteChar = c;
				}
				else 
				{
					value += c;
					state = TEXT;
				}
				break;

			case QUOTE:
				if(c == quoteChar)
				{
					auto next = iter + 1;
					if(delimiters.find(*next) != std::string::npos)
					{
						values.push_back(value);
						value.clear();
						state = DELIMITER;
					}
					else
						state = TEXT;
				}
				else
					value += c;
				break;
		}
	}
	if(! value.empty())
		values.push_back(value);

	return values;
}

//---------------------------------------------------------------------------

#ifdef ZSTR_USE_RE
std::vector<std::string> split(const std::string &input, const RE &delimiter)
{
	std::vector<std::string> values;
	if(! delimiter.ok())
		return values;

	auto in = strip(input);
	if(in.empty())
		return values;

	RE::Matches matches;
	bool found = delimiter.search(in, matches);

	while(found)
	{
		auto pos = matches[0].start;
		values.push_back(strip(in.substr(0, pos)));
		in = in.substr(matches[0].end+1);

		found = delimiter.search(in, matches);
	}

	values.push_back(strip(in));

	return values;
}
#endif

//---------------------------------------------------------------------------

std::string join(const std::vector<std::string> &list, const std::string &delimiter)
{
	std::string result;
	auto iter = list.begin();
	while(iter != list.end())
	{
		result += *iter;
		if(++iter != list.end())
			result += delimiter;
	}
	return result;
}

//---------------------------------------------------------------------------

std::vector<std::string> slice(const std::vector<std::string> &v, int start, int end, int skip)
{
	std::vector<std::string> result;

	if(v.empty())
		return result;
	if(skip == 0)
		skip = 1;

	if(start == INT_MAX)
		start = 0;
	if(end == INT_MAX)
		end = v.size();

	if(start < 0)
		start = v.size() + start;
	else if((unsigned)start > v.size())
		start = v.size();
	if(end < 0)
		end = v.size() + end;
	else if((unsigned)end > v.size())
		end = v.size();


	if(skip < 0)
		std::swap(start, end);

	for(int idx = start; idx < end; idx += skip)
		result.push_back(v[idx]);

	return result;
}

//---------------------------------------------------------------------------

bool in(const std::vector<std::string> &list, const std::string &find)
{
	return std::find(list.begin(), list.end(), find) != list.end();
}

//---------------------------------------------------------------------------

//! parses a string of key-value pairs, e.g.: 'key1=value1 key2=value2 key3=value3' into a map
//  e.g.: 'key1':'value1', 'key2':'value2', 'key3':'value3'
// the 'delimiters' are of course not allowed in the values
std::map<std::string, std::string> parseArgs(const std::string &argStr, const std::string &delimiters)
{
	std::map<std::string, std::string> result;

	const auto args = zstr::split(argStr, delimiters);
	
	for(const auto &a: args)
	{
		auto p = a.find("=");
		if(p == std::string::npos)
			p = a.size() + 1;

		auto key = a.substr(0, p);
		auto value = a.substr(p + 1);

		result[std::string(key)] = std::string(value);
	}
	return result;
}

//---------------------------------------------------------------------------

// 0: pattern matches string
// 2: "premature end of filter"
// 3: "premature end of string"
// 4: "failure on literal match"
#define STRMATCH_MATCHED 0
#define STRMATCH_PATTERN_EOF 2
#define STRMATCH_STRING_EOF 3
#define STRMATCH_LITERAL_MISMATCH 4

// TODO: replace with fnmatch() (on UNIX) / PathMatchSpec() (on windos) ?
// benchmark!

int match(const char *p, const char *s)
{
#if defined(_MSC_VER)
#pragma warning( disable : 4706 )
#endif

	int m = -1, n;
	while(*p)
	{
		if(! *s)
			return (*p == '*' && *++p == '\0') ? STRMATCH_MATCHED : STRMATCH_STRING_EOF;

		switch(*p)
		{
		case '?':
			break;

		case '*':
			while(*p == '?' || *p == '*')
				if(*p++ == '?' && ! *s++)
					return STRMATCH_STRING_EOF;
			n = *p;
			if(! n)
				return STRMATCH_MATCHED;	// we have a match!
			do
			{
				if(n == *s)
					m = match(p, s);
				if(! *s++)
					return STRMATCH_STRING_EOF;
			}
			while(m != STRMATCH_MATCHED && m != STRMATCH_STRING_EOF);

			return m;

		default:
			if(*p != *s)
				return STRMATCH_LITERAL_MISMATCH;
		}
		++p;
		++s;
	}
	if(*s)
		return STRMATCH_PATTERN_EOF;

	return STRMATCH_MATCHED;			// we have a match!
	
#if defined(_MSC_VER)
#pragma warning( default : 4706 )
#endif
}

//---------------------------------------------------------------------------

std::string asHex(const int &num)
{
	std::stringstream strm;
	strm << "0x" << std::setbase(16) << num;
	return strm.str();
}

//---------------------------------------------------------------------------

std::string asHex(const long long &num)
{
	std::stringstream strm;
	strm << "0x" << std::setbase(16) << num;
	return strm.str();
}

//---------------------------------------------------------------------------

unsigned int bitstringValue(const std::string &s)
{
	unsigned int result=0;
	int shift=0;

	auto iter = s.rbegin();
	for(; iter != s.rend(); ++iter)
	{
		int c = *iter;
		if(c != '0' && c != '1')
			return 0;
		result += (c - '0') << shift;
		shift++;
	}
	return result;
}

//---------------------------------------------------------------------------

std::string baseName(const std::string &argv0)
{
	auto slash = argv0.find_last_of("/\\");
	if(slash != std::string::npos)
		++slash;
	else
		slash = 0;

	return argv0.substr(slash);
	//base = base.substr(0, base.find_last_of("."));
	//return base;
}

//---------------------------------------------------------------------------
/* TODO: doesn't work!
std::string lower(const std::string &s)
{
	std::string r;
	r.reserve(s.size());
	std::transform(s.begin(), s.end(), r.begin(), ::tolower);
	return r;
}
*/
//---------------------------------------------------------------------------
/* TODO: doesn't work!
std::string upper(const std::string &s)
{
	std::string r;
	r.reserve(s.size());
	std::transform(s.begin(), s.end(), r.begin(), ::toupper);
	return r;
}
*/
//---------------------------------------------------------------------------

bool hasWildcards(const std::string &input)
{
	return input.find_first_of("*?") != std::string::npos;
}

//---------------------------------------------------------------------------

std::string fileExtension(const std::string &filePath)
{
	std::string path = filePath;
	auto lastSlash = path.rfind("/");
	if(lastSlash != std::string::npos)
		path.erase(0, lastSlash + 1);

	auto lastDot = path.rfind(".");
	if(lastDot != std::string::npos)
		return path.substr(lastDot + 1);
	else
		return "";
}

//---------------------------------------------------------------------------

std::string makeNameSerial(const char *name)
{
	static std::unordered_map<std::string, std::size_t> _serials;

	std::stringstream strm;
	strm << name << '-' << (_serials[name]++);
	return strm.str();
}

//---------------------------------------------------------------------------

bool boolValue(const std::string &val)
{
	static std::unordered_set<std::string> trueValues { "yes", "1", "on", "true" };
	return trueValues.find(val) != trueValues.end();
}

//---------------------------------------------------------------------------

} // namespace zstr
