// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_TRANSPORT_DETAIL_BYTE_STREAM_ACCESS_H
#define RIWO_HTTP_EXP_TRANSPORT_DETAIL_BYTE_STREAM_ACCESS_H

#include <riwo/http_exp/transport/byte_stream.h>

namespace riwo::http_exp::detail
{

// Transport adapters consume this bridge; basic_byte_stream owns the state.
// Keeping it separate avoids making byte_stream.h depend on negotiated
// transport or connection-composition details.
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

} //namespace riwo::http_exp::detail


#endif //RIWO_HTTP_EXP_TRANSPORT_DETAIL_BYTE_STREAM_ACCESS_H
