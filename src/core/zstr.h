#if !defined(zstr_h_Included)
#define zstr_h_Included

#include <string>
#include <vector>
#include <map>
#include <sstream>

#include <limits.h>

#ifdef ZSTR_USE_RE
class RE;
#endif

namespace zstr
{

using namespace std::literals;

//! strip characters (e.g. whitespace) from a string (leading & trailing)
std::string_view strip(std::string_view s, std::string_view delimiters = " \t\n\r"sv);

//! split a string on a delimiter into a list
std::vector<std::string_view> split(std::string_view input, std::string_view delimiters = " \t"sv);

std::vector<std::string_view> lexsplit(std::string_view input);

#ifdef ZSTR_USE_RE
//! split a string on a regular expression into a list
std::vector<std::string> split(const std::string &input, const RE &delimiter);
#endif

//! join a list into a string, with specified delimiter inbetween all elements
//  TODO add support for any range of any string type
std::string join(const std::vector<std::string> &list, const std::string_view &delimiter = ", "sv);

//! extract a python-like slice of a string list.
std::vector<std::string> slice(const std::vector<std::string> &v, int start=INT_MAX, int end=INT_MAX, int skip=1);

//! true if 'find' is present in 'list'
bool in(const std::vector<std::string> &list, const std::string &find);

//! parses a string of key-value pairs, e.g.: 'key1=value1 key2=value2 key3=value3' into a map
std::map<std::string, std::string> parseArgs(const std::string &argStr, const std::string &delimiters = " \t");

//! returns 0 if the globbing pattern 'p' matches 's'
int match(const char *p, const char *s);

inline int match(const std::string &p, const std::string &s)
{
    return match(p.c_str(), s.c_str());
}
inline int match(const char *p, const std::string &s)
{
    return match(p, s.c_str());
}
inline int match(const std::string &p, const char &s)
{
    return match(p.c_str(), s);
}

//! returns number as hexadecimal - 32bit
std::string asHex(const int &num);

//! returns number as hexadecimal - 64bit
std::string asHex(const long long &num);

//! convert a string into anything (provided iostream can convert it)
template<typename T>
T as(const std::string &str)
{
  std::stringstream strm(str);
  T value;
  strm >> value;
  return value;
}

template<typename S>
S::size_type find_nth(const S &s, typename S::value_type ch, typename S::size_type n)
{
	typename S::size_type pos { 0 };
	while(n-- > 0 and pos != S::npos)
		pos = s.find(ch, pos + 1);
	return pos;
}

//! "0001001011" -> number (max 32 bits)
unsigned int bitstringValue(const std::string &s);

//! same as python's os.path.basename
std::string baseName(const std::string &argv0);

//! convert string to lower case
// TODO: doesn't work!
//std::string lower(const std::string &s);

//! convert string to upper case
// TODO: doesn't work!
//std::string upper(const std::string &s);

//! return true if the string contains wildcards
bool hasWildcards(const std::string &input);

std::string fileExtension(const std::string &filePath);

std::string makeNameSerial(const char *name);

bool boolValue(const std::string &val);

}

#endif
