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
#if RIWO_CONSUME_EXP_HTTP
# include <riwo/experimental/http.h>
# include <riwo/experimental/http/protocol/body_spec.h>
# include <riwo/experimental/http/protocol/v1/engine.h>
# include <riwo/experimental/http/transport/byte_stream.h>
# include <riwo/experimental/http/version.h>
#endif
#if RIWO_CONSUME_WEBSOCKET
# include <riwo/websocket.h>
#endif
#if RIWO_CONSUME_EXP_WEBSOCKET
# include <riwo/experimental/websocket.h>
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

#if RIWO_CONSUME_EXP_HTTP
	// Exercise an exported symbol from the experimental HTTP component.
	riwo::experimental::http::method request_method(riwo::experimental::http::method::get);
	if(request_method.text() != "GET" or !request_method.is_safe())
		return 4;
	riwo::experimental::http::request_head head;
	head.method = request_method;
	head.scheme = "http";
	head.authority = "example.test";
	head.target = "/";
	auto encoded = riwo::experimental::http::v1::serialize_request_head(head,
		riwo::experimental::http::body_spec::empty(),
		riwo::experimental::http::protocol::v1_1);
	if(!encoded or encoded->wire.empty())
		return 6;
	riwo::experimental::http::v1::connection_engine engine({
		.role = riwo::experimental::http::protocol_role::client,
		.version = riwo::experimental::http::protocol::v1_1
	});
	if(!engine.submit(riwo::experimental::http::open_request_action{
		.stream = 1, .head = std::move(head),
		.body = riwo::experimental::http::body_spec::empty()
	}))
		return 7;
#endif

#if RIWO_CONSUME_EXP_WEBSOCKET
	riwo::experimental::websocket::stream_config websocket_config;
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
