// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/http/protocol/utils/client/parser.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	if( size < 3 )
		return 0;

	static constexpr std::array methods {
		riwo::http::method::get,
		riwo::http::method::head,
		riwo::http::method::connect,
	};
	const auto initial_size = size_t(data[0] % 64) + 1;
	const auto request_method = methods[data[1] % methods.size()];
	const bool decompress = (data[1] & 0x80) != 0;
	riwo::http::client_parser parser(initial_size);
	riwo::http::client_parser contiguous(initial_size);
	parser.set_request_method(request_method);
	parser.set_automatic_decompression(decompress);
	contiguous.set_request_method(request_method);
	contiguous.set_automatic_decompression(decompress);
	const auto contiguous_result = contiguous.append(
		riwo::const_buffer(data + 3, size - 3));

	size_t offset = 3;
	riwo::error_code fragmented_error;
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
	if(size == 3)
	{
		auto parsed = parser.append(riwo::const_buffer(data + 3, 0));
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
	if(parser.stage() != riwo::http::stage::header)
	{
		const auto fragmented_finished = parser.finish_eof();
		const auto contiguous_finished = contiguous.finish_eof();
		if(fragmented_finished != contiguous_finished)
			std::abort();
	}
	if(contiguous.stage() != parser.stage())
		std::abort();

	if( parser.stage() != riwo::http::stage::header )
	{
		if(parser.version() != contiguous.version() or
			parser.status() != contiguous.status() or
			parser.keep_alive() != contiguous.keep_alive() or
			parser.is_chunked() != contiguous.is_chunked() or
			parser.is_range_response() != contiguous.is_range_response() or
			parser.is_multipart_byte_ranges() !=
				contiguous.is_multipart_byte_ranges() or
			parser.is_informational() != contiguous.is_informational() or
			parser.is_upgrade() != contiguous.is_upgrade())
			std::abort();
		riwo::ignore_unused(parser.version());
		riwo::ignore_unused(parser.status());
		riwo::ignore_unused(parser.keep_alive());
		riwo::ignore_unused(parser.is_chunked());
		riwo::ignore_unused(parser.is_range_response());
		riwo::ignore_unused(parser.is_multipart_byte_ranges());
		riwo::ignore_unused(parser.is_informational());
		riwo::ignore_unused(parser.is_upgrade());
		if(parser.take_body() != contiguous.take_body())
			std::abort();
		riwo::ignore_unused(parser.take_range_body(1024));
		riwo::ignore_unused(parser.take_pending_data());
	}
	return 0;
}
