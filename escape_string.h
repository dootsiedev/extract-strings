#pragma once

//#include "../core/global.h"

#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <cstdio>

#ifndef serr
#define serr(msg) llvm::errs() << msg;
#endif
#ifndef serrf
#define serrf(fmt, ...)                                             \
	do                                                              \
	{                                                               \
		char serrf_buff[1000];                                      \
		snprintf(serrf_buff, sizeof(serrf_buff), fmt, __VA_ARGS__); \
		llvm::errs() << serrf_buff;                                 \
	} while(0)
#endif
#ifndef ASSERT
#define ASSERT assert
#endif

#include <string>
#include <string_view>
#include <cstring>

// checks if any characters are escape codes.
// so you can skip the allocation.
inline bool escape_string_check_contains(std::string_view input_string)
{
	for(char c : input_string)
	{
		switch(c)
		{
		case '\n':
		case '\t':
		case '\"':
		case '\\': return true;
		}
	}
	return false;
}

// a string conversion so '\n' turns into "\\\n"
inline void escape_string(std::string &output_string, std::string_view input_string)
{
	for(char c : input_string)
	{
		switch(c)
		{
		case '\n':
			output_string.push_back('\\');
			output_string.push_back('n');
			break;
		case '\t':
			output_string.push_back('\\');
			output_string.push_back('t');
			break;
		case '\"':
			output_string.push_back('\\');
			output_string.push_back('\"');
			break;
		case '\\':
			output_string.push_back('\\');
			output_string.push_back('\\');
			break;
		default:
			// if not an escape code.
			if(!(c > 0 && c < 0x001fu))
			{
				output_string.push_back(c);
			}
		}
	}
}

inline bool rem_escape_string(char* input_string)
{
	ASSERT(input_string != NULL);

	int i = 0;
	int j = 0;
	while(input_string[i] != '\0')
	{
		if(input_string[i] == '\\')
		{
			++i;
			switch(input_string[i])
			{
			case 'n': input_string[j] = '\n'; break;
			case 't': input_string[j] = '\t'; break;
			case '\"': input_string[j] = '\"'; break;
			case '\\': input_string[j] = '\\'; break;
			case '\0': serr("expected escape code, got null\n"); return false;
			default:
				if(isalnum(input_string[i]) != 0)
				{
					serrf("expected escape code, got: %c, offset: %d\n", input_string[i], i);
					return false;
				}
				serrf("expected escape code, got: #%d, offset: %d\n", input_string[i], i);
				return false;
			}
		}
		// if it's an escape code.
		if(input_string[i] > 0 && input_string[i] < 0x001fu)
		{
			serrf("code points <= U+001F must be escaped, got: #%d, offset: %d\n", input_string[i], i);
			return false;
		}
		if(j < i)
		{
			input_string[j] = input_string[i];
		}
		++j;
		++i;
	}
	input_string[j] = '\0';
	return true;
}