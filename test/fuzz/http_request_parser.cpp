// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/http/protocol/utils/server/parser.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	if( size < 2 )
		return 0;

	const auto initial_size = size_t(data[0] % 64) + 1;
	riwo::http::server_parser parser(initial_size);
	riwo::http::server_parser contiguous(initial_size);
	const auto payload = riwo::const_buffer(data + 2, size - 2);
	const auto contiguous_result = contiguous.append(payload);
	riwo::error_code fragmented_error;

	size_t offset = 2;
	while( offset < size )
	{
		const auto chunk_size = size_t(data[offset % size] % 127) + 1;
		const auto available = std::min(chunk_size, size - offset);
		auto parsed = parser.append(riwo::const_buffer(data + offset, available));
		offset += available;
		if( not parsed )
		{
			fragmented_error = parsed.error();
			break;
		}
		if( parser.stage() == riwo::http::stage::finished )
			break;
	}
	if(size == 2)
	{
		auto parsed = parser.append(riwo::const_buffer(data + 2, 0));
		if(not parsed)
			fragmented_error = parsed.error();
	}

	if(bool(contiguous_result) == bool(fragmented_error))
		std::abort();
	if(not contiguous_result)
	{
		if(contiguous_result.error() != fragmented_error)
			std::abort();
		return 0;
	}
	if(contiguous.stage() != parser.stage())
		std::abort();

	if( parser.stage() != riwo::http::stage::header )
	{
		if(parser.method() != contiguous.method() or
			parser.target_form() != contiguous.target_form() or
			parser.target() != contiguous.target() or parser.path() != contiguous.path() or
			parser.version() != contiguous.version() or
			parser.keep_alive() != contiguous.keep_alive() or
			parser.support_gzip() != contiguous.support_gzip())
			std::abort();
		riwo::ignore_unused(parser.method());
		riwo::ignore_unused(parser.target_form());
		riwo::ignore_unused(parser.target());
		riwo::ignore_unused(parser.path());
		riwo::ignore_unused(parser.version());
		riwo::ignore_unused(parser.keep_alive());
		riwo::ignore_unused(parser.support_gzip());
		if(parser.take_body() != contiguous.take_body())
			std::abort();
		// Once the fragmented parser finishes a message, bytes after the final
		// supplied chunk have intentionally not been appended.  Pending data is
		// comparable only when both parsers received the complete fuzz payload.
		if(offset == size and
			parser.take_pending_data() != contiguous.take_pending_data())
			std::abort();
	}
	return 0;
}
