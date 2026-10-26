// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#if RIWO_CONSUME_UMBRELLA
# include <riwo.h>
#endif

#if RIWO_CONSUME_CORE
# include <riwo/core.h>
#endif
#if RIWO_CONSUME_CORO
# include <riwo/coro.h>
#endif
#if RIWO_CONSUME_HTTP
# include <riwo/http.h>
#endif
#if RIWO_CONSUME_HTTP_EXP
# include <riwo/http_exp.h>
# include <riwo/http_exp/protocol/body_spec.h>
# include <riwo/http_exp/protocol/v1/engine.h>
# include <riwo/http_exp/transport/byte_stream.h>
# include <riwo/http_exp/version.h>
#endif
#if RIWO_CONSUME_WEBSOCKET
# include <riwo/websocket.h>
#endif
#if RIWO_CONSUME_WEBSOCKET_EXP
# include <riwo/websocket_exp.h>
#endif
#if RIWO_CONSUME_UTILS
# include <riwo/utils.h>
# include <spdlog/spdlog.h>
#endif

#include <riwo/core/global.h>

#include <string_view>

int main()
{
	const std::string_view version = riwo::version_string();
	if(version.empty())
		return 1;

#if RIWO_CONSUME_HTTP
	// These functions are implemented in riwo.http.  Keep this in the installed
	// consumer so shared and static package tests both verify exported symbols,
	// not merely that version.h can be included.
	using http_version = riwo::http::version;
	if(not http_version::check(http_version::v11) or
		std::string_view(http_version::string(http_version::v10)) != "1.0" or
		http_version::number(http_version::v11) != 1.1 or
		http_version::from_string("1.1") != http_version::v11)
	{
		return 2;
	}
#endif

#if RIWO_CONSUME_HTTP_EXP
	// Exercise an exported symbol from the experimental HTTP component.
	riwo::http_exp::method request_method(riwo::http_exp::method::get);
	if(request_method.text() != "GET" or !request_method.is_safe())
		return 4;
	riwo::http_exp::request_head head;
	head.method = request_method;
	head.scheme = "http";
	head.authority = "example.test";
	head.target = "/";
	auto encoded = riwo::http_exp::v1::serialize_request_head(head,
		riwo::http_exp::body_spec::empty(),
		riwo::http_exp::protocol::v1_1);
	if(!encoded or encoded->wire.empty())
		return 6;
	riwo::http_exp::v1::connection_engine engine({
		.role = riwo::http_exp::protocol_role::client,
		.version = riwo::http_exp::protocol::v1_1
	});
	if(!engine.submit(riwo::http_exp::open_request_action{
		.stream = 1, .head = std::move(head),
		.body = riwo::http_exp::body_spec::empty()
	}))
		return 7;
#endif

#if RIWO_CONSUME_WEBSOCKET_EXP
	riwo::websocket_exp::stream_config websocket_config;
	if(websocket_config.max_message_size == 0)
		return 5;
#endif

#if RIWO_CONSUME_UTILS
	// riwo.utils carries one compiled spdlog implementation.  Exercise a direct
	// imported symbol so shared-package tests verify the spdlog export/import
	// contract instead of only Riwo's logger wrapper.
	const auto original_level = spdlog::get_level();
	spdlog::set_level(spdlog::level::info);
	if(spdlog::get_level() != spdlog::level::info)
		return 3;
	spdlog::set_level(original_level);
#endif

	return 0;
}
