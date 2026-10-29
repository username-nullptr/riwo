// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/http_exp/protocol/v1/codec.h>
#include <cstdlib>

namespace
{

enum class outcome {
	incomplete, rejected, accepted
};

[[nodiscard]] outcome parse_contiguous(
	const uint8_t *data, size_t size, bool head_request)
{
	riwo::http_exp::v1::response_parser parser({}, head_request);
	auto result = parser.ingest(riwo::const_buffer(data, size));
	if( not result )
		return outcome::rejected;

	if( result->head )
	{
		if( not parser.complete() or result->needs_input or result->consumed > size )
			std::abort();
		return outcome::accepted;
	}
	if( parser.complete() or not result->needs_input or result->consumed != size )
		std::abort();
	return outcome::incomplete;
}

[[nodiscard]] outcome parse_fragmented(
	const uint8_t *data, size_t size, bool head_request)
{
	riwo::http_exp::v1::response_parser parser({}, head_request);
	for(size_t offset = 0; offset < size; ++offset)
	{
		auto result = parser.ingest(riwo::const_buffer(data + offset, 1));
		if( not result )
			return outcome::rejected;
		if( result->consumed > 1 )
			std::abort();
		if( result->head )
		{
			if( not parser.complete() or result->needs_input )
				std::abort();
			return outcome::accepted;
		}
		if( parser.complete() or not result->needs_input )
			std::abort();
	}
	return outcome::incomplete;
}

} //namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	if( size < 3 or size > RIWO_FUZZ_MAX_LENGTH )
		return 0;

	const bool head_request = (data[1] & 1U) != 0;
	data += 3;
	size -= 3;

	const auto experimental = parse_contiguous(data, size, head_request);
	if( parse_fragmented(data, size, head_request) != experimental )
		std::abort();
	return 0;
}
