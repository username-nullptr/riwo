// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_TRANSPORT_DETAIL_BYTE_STREAM_CONNECTION_H
#define RIWO_HTTP_EXP_TRANSPORT_DETAIL_BYTE_STREAM_CONNECTION_H

#include <riwo/http_exp/transport/negotiated_transport.h>
#include <riwo/http_exp/transport/byte_stream.h>
#include <riwo/http_exp/transport/connection.h>

namespace riwo::http_exp::detail
{

struct RIWO_HTTP_EXP_API byte_stream_access
{
	template<core_concepts::exec Exec>
	[[nodiscard]] static connection_ptr release
	(basic_byte_stream<Exec> stream, std::vector<std::byte> initial_input, transport_metadata metadata)
	{
		return std::move(stream).release_connection (
			std::move(initial_input), std::move(metadata)
		);
	}
};

template<core_concepts::exec Exec>
[[nodiscard]] RIWO_HTTP_EXP_TAPI
connection_ptr make_connection(basic_negotiated_transport<Exec> transport)
{
	return byte_stream_access::release (
		std::move(transport.stream), std::move(transport.initial_input),
		std::move(transport.metadata)
	);
}

} //namespace riwo::http_exp::detail


#endif //RIWO_HTTP_EXP_TRANSPORT_DETAIL_BYTE_STREAM_CONNECTION_H
