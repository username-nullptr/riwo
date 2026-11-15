// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/crypto/uuid.h>

namespace riwo::crypto::detail
{

bool uuid_append_utf8(std::string &output, uint32_t code_point)
{
	if( code_point <= 0x7F )
		output.push_back(static_cast<char>(code_point));

	else if( code_point <= 0x7FF )
	{
		output.push_back(static_cast<char>(0xC0 | (code_point >> 6)));
		output.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
	}
	else if( code_point >= 0xD800 and code_point <= 0xDFFF )
		return false;

	else if( code_point <= 0xFFFF )
	{
		output.push_back(static_cast<char>(0xE0 | (code_point >> 12)));
		output.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
		output.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
	}
	else if( code_point <= 0x10FFFF )
	{
		output.push_back(static_cast<char>(0xF0 | (code_point >> 18)));
		output.push_back(static_cast<char>(0x80 | ((code_point >> 12) & 0x3F)));
		output.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
		output.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
	}
	else
		return false;
	return true;
}

} //namespace riwo::crypto::detail
