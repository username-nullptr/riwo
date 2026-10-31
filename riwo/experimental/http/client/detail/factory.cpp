// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "factory.h"
#include <riwo/experimental/http/session/v1/session.h>

#if RIWO_EXP_HTTP_HTTP2_SUPPORT
# include <riwo/experimental/http/session/v2/session.h>
#endif //RIWO_EXP_HTTP_HTTP2_SUPPORT

namespace riwo::experimental::http::detail { namespace
{

[[nodiscard]] bool is_v1(protocol value) noexcept
{
	return value == protocol::v1_0 or value == protocol::v1_1;
}

class RIWO_DECL_HIDDEN default_session_factory final : public session_factory
{
	RIWO_DISABLE_COPY_MOVE(default_session_factory)

public:
	default_session_factory() = default;

	sys_expected<client_session_ptr> create
	(const session_key &key, negotiated_transport transport, const client_config &config) override
	{
		if( not transport.stream )
			return sys_unexpected(make_error_code(errc::negotiation_failed));

#if RIWO_EXP_HTTP_HTTP2_SUPPORT
		if( transport.selected_protocol == protocol::v2 )
		{
			return v2::make_client_session (
				std::move(transport.stream), {
					.timeout = config.timeout,
					.reply_limits = config.reply_limits,
					.h2 = config.h2
				},
				transport.remote, transport.local
			);
		}
#endif //RIWO_EXP_HTTP_HTTP2_SUPPORT

		if( not is_v1(transport.selected_protocol) )
			return sys_unexpected(make_error_code(errc::operation_not_supported));

		ignore_unused(key);

		auto executor = transport.stream->get_executor();
		auto selected = transport.selected_protocol;
		auto limits = config.reply_limits;

		return v1::make_client_session (
			std::move(executor), selected, limits, std::move(transport.stream),
			config.timeout, transport.absolute_form
		);
	}
};

} //namespace

session_factory_ptr make_default_session_factory()
{
	return std::make_shared<default_session_factory>();
}

} //namespace riwo::experimental::http::detail
