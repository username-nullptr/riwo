// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "base64.h"

namespace riwo
{

std::string base64_encode(std::span<const std::byte> input)
{
	static constexpr std::string_view alphabet =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

	std::string output;
	output.reserve((input.size() + 2) / 3 * 4);

	for(size_t offset = 0; offset < input.size(); offset += 3)
	{
		uint32_t value = static_cast<uint32_t>(
			std::to_integer<uint8_t>(input[offset])) << 16U;

		if( offset + 1 < input.size() )
			value |= static_cast<uint32_t>(
				std::to_integer<uint8_t>(input[offset + 1])) << 8U;

		if( offset + 2 < input.size() )
			value |= static_cast<uint32_t>(
				std::to_integer<uint8_t>(input[offset + 2]));

		output += alphabet[(value >> 18) & 0x3F];
		output += alphabet[(value >> 12) & 0x3F];
		output += offset + 1 < input.size() ?
			alphabet[(value >> 6) & 0x3F] : '=';
		output += offset + 2 < input.size() ?
			alphabet[value & 0x3F] : '=';
	}
	return output;
}

std::string base64_encode(std::string_view input)
{
	return base64_encode(std::as_bytes(std::span(input.data(), input.size())));
}

} //namespace riwo
