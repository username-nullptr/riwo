// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_TRANSPORT_DETAIL_BYTE_STREAM_CONNECTION_H
#define RIWO_HTTP_TRANSPORT_DETAIL_BYTE_STREAM_CONNECTION_H

#include <riwo/http/transport/detail/byte_stream_access.h>
#include <riwo/http/transport/negotiated_transport.h>

namespace riwo::http::detail
{

template<core_concepts::exec Exec>
[[nodiscard]] RIWO_HTTP_TAPI
connection_ptr make_connection(basic_negotiated_transport<Exec> transport)
{
	return byte_stream_access::release (
		std::move(transport.stream), std::move(transport.initial_input),
		std::move(transport.metadata)
	);
}

} //namespace riwo::http::detail


#endif //RIWO_HTTP_TRANSPORT_DETAIL_BYTE_STREAM_CONNECTION_H
