// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_TRANSPORT_NEGOTIATED_TRANSPORT_H
#define RIWO_HTTP_EXP_TRANSPORT_NEGOTIATED_TRANSPORT_H

#include <riwo/http_exp/protocol/version.h>
#include <riwo/http_exp/transport/byte_stream.h>

namespace riwo::http_exp
{

template<core_concepts::exec Exec = asio::any_io_executor>
struct basic_negotiated_transport
{
	basic_byte_stream<Exec> stream;
	protocol selected_protocol = protocol::v1_1;
	transport_metadata metadata {};
	std::vector<std::byte> initial_input {};
};

using negotiated_transport_t = basic_negotiated_transport<>;

} //namespace riwo::http_exp

#endif //RIWO_HTTP_EXP_TRANSPORT_NEGOTIATED_TRANSPORT_H
