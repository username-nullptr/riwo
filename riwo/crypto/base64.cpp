// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "base64.h"
#include <array>

namespace riwo::crypto { namespace
{

constexpr std::string_view g_base64_alphabet =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

constexpr std::string_view g_base64url_alphabet =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

using decode_table_t = std::array<uint8_t,256>;
constexpr uint8_t g_invalid_digit = 0xFF;

constexpr decode_table_t make_decode_table_t(std::string_view alphabet)
{
	decode_table_t table {};
	table.fill(g_invalid_digit);

	for(size_t index = 0; index < alphabet.size(); ++index)
		table[static_cast<unsigned char>(alphabet[index])] = static_cast<uint8_t>(index);

	return table;
}

std::string encode(std::span<const std::byte> input, std::string_view alphabet, bool padded)
{
	std::string output {};
	const auto tail = input.size() % 3;
	auto output_size = input.size() / 3 * 4;

	if( tail != 0 )
		output_size += padded ? 4 : tail + 1;
	output.reserve(output_size);

	for(size_t offset=0; offset<input.size(); offset+=3)
	{
		uint32_t value = std::to_integer<uint32_t>(input[offset]) << 16U;

		if( offset + 1 < input.size() )
			value |= std::to_integer<uint32_t>(input[offset + 1]) << 8U;

		if( offset + 2 < input.size() )
			value |= std::to_integer<uint32_t>(input[offset + 2]);

		output += alphabet[(value >> 18) & 0x3F];
		output += alphabet[(value >> 12) & 0x3F];

		if( offset + 1 < input.size() )
			output += alphabet[(value >> 6) & 0x3F];

		if( offset + 2 < input.size() )
			output += alphabet[value & 0x3F];
	}
	if( padded and tail != 0 )
		output.append(3 - tail, '=');
	return output;
}

optional<std::string> decode(std::string_view input, const decode_table_t &table, bool allow_unpadded)
{
	const auto digits = input.substr(0, input.find('='));
	const auto padding = input.substr(digits.size());

	const auto remainder = digits.size() % 4;
	const auto required_padding = (4 - remainder) % 4;

	if( remainder == 1 or
		((not allow_unpadded or not padding.empty()) and padding.size() != required_padding) or
		padding.find_first_not_of('=') != std::string_view::npos )
		return {};

	std::string output;
	output.reserve(digits.size() / 4 * 3 + remainder * 3 / 4);

	uint32_t bits = 0;
	unsigned int pending = 0;

	for(char character : digits)
	{
		const auto digit = table[static_cast<unsigned char>(character)];
		if( digit == g_invalid_digit )
			return {};

		bits = (bits << 6) | digit;
		pending += 6;

		if( pending >= 8 )
		{
			pending -= 8;
			output += static_cast<char>((bits >> pending) & 0xFF);
		}
	}
	return output;
}

} //namespace

std::string base64_encode(std::span<const std::byte> input)
{
	return encode(input, g_base64_alphabet, true);
}

std::string base64_encode(std::string_view input)
{
	return base64_encode(std::as_bytes(std::span(input.data(), input.size())));
}

optional<std::string> base64_decode(std::string_view input)
{
	static constexpr auto table = make_decode_table_t(g_base64_alphabet);
	return decode(input, table, false);
}

std::string base64url_encode(std::span<const std::byte> input, bool padded)
{
	return encode(input, g_base64url_alphabet, padded);
}

std::string base64url_encode(std::string_view input, bool padded)
{
	return base64url_encode(std::as_bytes(std::span(input.data(), input.size())), padded);
}

optional<std::string> base64url_decode(std::string_view input)
{
	static constexpr auto table = make_decode_table_t(g_base64url_alphabet);
	return decode(input, table, true);
}

} //namespace riwo::crypto
