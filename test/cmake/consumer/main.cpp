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
# include <riwo/http/protocol/body_spec.h>
# include <riwo/http/protocol/v1/engine.h>
# include <riwo/http/transport/byte_stream.h>
# include <riwo/http/version.h>
#endif
#if RIWO_CONSUME_WEBSOCKET
# include <riwo/websocket.h>
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
	// Exercise exported protocol symbols from the HTTP component.
	riwo::http::method request_method(riwo::http::method::get);
	if(request_method.text() != "GET" or !request_method.is_safe())
		return 4;
	riwo::http::request_head head;
	head.method = request_method;
	head.scheme = "http";
	head.authority = "example.test";
	head.target = "/";
	auto encoded = riwo::http::v1::serialize_request_head(head,
		riwo::http::body_spec::empty(),
		riwo::http::protocol::v1_1);
	if(!encoded or encoded->wire.empty())
		return 6;
	riwo::http::v1::connection_engine engine({
		.role = riwo::http::protocol_role::client,
		.version = riwo::http::protocol::v1_1
	});
	if(!engine.submit(riwo::http::open_request_action{
		.stream = 1, .head = std::move(head),
		.body = riwo::http::body_spec::empty()
	}))
		return 7;
#endif

#if RIWO_CONSUME_WEBSOCKET
	riwo::websocket::stream_config websocket_config;
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
