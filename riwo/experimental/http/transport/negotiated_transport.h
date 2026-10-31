// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_EXP_HTTP_TRANSPORT_NEGOTIATED_TRANSPORT_H
#define RIWO_EXP_HTTP_TRANSPORT_NEGOTIATED_TRANSPORT_H

#include <riwo/experimental/http/transport/byte_stream.h>
#include <riwo/experimental/http/version.h>

namespace riwo::experimental::http
{

template<core_concepts::exec Exec = asio::any_io_executor>
struct basic_negotiated_transport
{
	basic_byte_stream<Exec> stream;
	protocol selected_protocol = protocol::v1_1;

	transport_metadata metadata {};
	std::vector<std::byte> initial_input {};
};

using negotiated_transport = basic_negotiated_transport<>;

} //namespace riwo::experimental::http


#endif //RIWO_EXP_HTTP_TRANSPORT_NEGOTIATED_TRANSPORT_H
