// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "test.h"

#include <riwo/http/protocol/utils/client/cookie_jar.h>
#include <riwo/http/protocol/utils/client/form_data.h>
#include <riwo/http/protocol/utils/client/generator.h>
#include <riwo/http/protocol/utils/client/parser.h>
#include <riwo/http/protocol/utils/core/conditional.h>
#include <riwo/http/protocol/utils/core/range.h>
#include <riwo/http/protocol/utils/core/upgrade.h>
#include <riwo/http/protocol/utils/server/generator.h>
#include <riwo/http/protocol/utils/server/parser.h>

#include <array>

static_assert(std::is_error_code_enum_v<riwo::http::parse_errc>);
static_assert(not riwo::http::version::is_valid_v<riwo::http::version::none>);
static_assert(riwo::http::version::is_valid_v<riwo::http::version::v10>);
static_assert(riwo::http::version::is_valid_v<riwo::http::version::v11>);
static_assert(not riwo::http::version::is_valid_v<
	static_cast<riwo::http::version_enum>(0x0102)>);
static_assert(std::string_view(
	riwo::http::version::string<riwo::http::version::v10>()) == "1.0");
static_assert(std::string_view(
	riwo::http::version::string<riwo::http::version::v11>()) == "1.1");
static_assert(riwo::http::version::number<riwo::http::version::v10>() == 1.0);
static_assert(riwo::http::version::number<riwo::http::version::v11>() == 1.1);

namespace
{

riwo::const_buffer buffer(std::string_view value)
{
	return {value.data(), value.size()};
}

void parser_errors()
{
	using namespace riwo::http;
	const riwo::error_code empty = parse_errc::inserted_data_empty;
	RIWO_TEST_CHECK(empty == parse_errc::inserted_data_empty);
	RIWO_TEST_CHECK(parse_errc::inserted_data_empty == empty);
	RIWO_TEST_CHECK(empty != parse_errc::request_end);
	RIWO_TEST_CHECK(parse_errc::request_end != empty);
	RIWO_TEST_CHECK_EQ(empty, make_error_code(parse_errc::inserted_data_empty));
	RIWO_TEST_CHECK_EQ(empty.category(), parse_error_category());
	RIWO_TEST_CHECK_EQ(empty.message(), "The inserted data is empty.");

	server_parser parser;
	auto result = parser.append({});
	RIWO_TEST_CHECK(not result);
	RIWO_TEST_CHECK(result.error() == parse_errc::inserted_data_empty);

	server_parser malformed_header;
	result = malformed_header.append(buffer(
		"GET / HTTP/1.1\r\nMissing-Colon\r\n\r\n"
	));
	RIWO_TEST_CHECK(not result);
	RIWO_TEST_CHECK(result.error() == parse_errc::invalid_header_line);

	server_parser invalid_method;
	result = invalid_method.append(buffer("FETCH / HTTP/1.1\r\n\r\n"));
	RIWO_TEST_CHECK(not result);
	RIWO_TEST_CHECK(result.error() == parse_errc::invalid_method);

	server_parser conflicting_size;
	result = conflicting_size.append(buffer(
		"POST / HTTP/1.1\r\n"
		"Host: example.test\r\n"
		"Content-Length: 1\r\n"
		"Transfer-Encoding: chunked\r\n\r\n"
	));
	RIWO_TEST_CHECK(not result);
	RIWO_TEST_CHECK(result.error() == parse_errc::invalid_size_format);
}

void enum_input_validation()
{
	using namespace riwo::http;
	const auto invalid_status_code = static_cast<status_enum>(999);
	const auto invalid_method = static_cast<method_enum>(0x8000);

	RIWO_TEST_CHECK(not status::check(invalid_status_code, false));
	RIWO_TEST_CHECK_EQ(
		std::string(status::description(invalid_status_code, false)), "None"
	);
	RIWO_TEST_CHECK_THROWS(status::check(invalid_status_code), riwo::invalid_argument);

	RIWO_TEST_CHECK(not method::check(invalid_method, false));
	RIWO_TEST_CHECK_EQ(std::string(method::string(invalid_method, false)), "NONE");
	RIWO_TEST_CHECK_EQ(method::from_string("FETCH"), method::none);
	RIWO_TEST_CHECK_THROWS(method::from_string("FETCH", true), riwo::invalid_argument);
	RIWO_TEST_CHECK_THROWS(method("FETCH"), riwo::invalid_argument);

}

void version_contract()
{
	using namespace riwo::http;
	struct valid_case
	{
		version_enum value;
		std::string_view text;
		double number;
	};
	constexpr std::array valid_cases {
		valid_case {version::v10, "1.0", 1.0},
		valid_case {version::v11, "1.1", 1.1},
	};
	for(const auto &[value, text, number] : valid_cases)
	{
		RIWO_TEST_CHECK(version::check(value));
		RIWO_TEST_CHECK(version::check(value, false));
		RIWO_TEST_CHECK_EQ(std::string_view(version::string(value)), text);
		RIWO_TEST_CHECK_EQ(version::number(value), number);
		RIWO_TEST_CHECK_EQ(version::from_string(text), value);

		const version from_enum(value);
		const version from_text(text);
		RIWO_TEST_CHECK(from_enum.check());
		RIWO_TEST_CHECK_EQ(std::string_view(from_enum.string()), text);
		RIWO_TEST_CHECK_EQ(from_enum.number(), number);
		RIWO_TEST_CHECK_EQ(from_text.value, value);
	}

	constexpr std::array invalid_values {
		version::none,
		static_cast<version_enum>(0x0001),
		static_cast<version_enum>(0x00ff),
		static_cast<version_enum>(0x0102),
		static_cast<version_enum>(0x0200),
		static_cast<version_enum>(0xffff),
	};
	for(const auto value : invalid_values)
	{
		RIWO_TEST_CHECK(not version::check(value, false));
		RIWO_TEST_CHECK_EQ(std::string_view(version::string(value, false)), "0.0");
		RIWO_TEST_CHECK_EQ(version::number(value, false), 0.0);
		RIWO_TEST_CHECK_THROWS(version::check(value), riwo::runtime_error);
		RIWO_TEST_CHECK_THROWS(version::string(value), riwo::runtime_error);
		RIWO_TEST_CHECK_THROWS(version::number(value), riwo::runtime_error);
	}

	const version unset;
	RIWO_TEST_CHECK(not unset.check(false));
	RIWO_TEST_CHECK_EQ(std::string_view(unset.string(false)), "0.0");
	RIWO_TEST_CHECK_EQ(unset.number(false), 0.0);

	for(const std::string_view text : {
		"", "0.0", "1", "1.00", "1.2", "2.0", "01.1", "1.1 ", " 1.1", "9.9"
	})
	{
		RIWO_TEST_CHECK_THROWS(version::from_string(text), riwo::runtime_error);
		RIWO_TEST_CHECK_THROWS(version(text), riwo::runtime_error);
	}
}

void parser_reuse()
{
	using namespace riwo::http;
	constexpr std::string_view request =
		"GET /repeat?q=42 HTTP/1.1\r\n"
		"Host: example.test\r\n\r\n";
	server_parser parser(32);

	for(size_t round = 0; round < 2'000; ++round)
	{
		auto result = parser.append(buffer(request));
		RIWO_TEST_CHECK(result and *result);
		RIWO_TEST_CHECK_EQ(parser.method(), method::get);
		RIWO_TEST_CHECK_EQ(parser.path(), "/repeat");
		auto parameter = parser.parameter("q");
		RIWO_TEST_CHECK(parameter);
		RIWO_TEST_CHECK_EQ(parameter->to_int().value_or(0), 42);
		RIWO_TEST_CHECK(parser.keep_alive());
		RIWO_TEST_CHECK(parser.take_body().empty());
		parser.reset();
		RIWO_TEST_CHECK_EQ(parser.stage(), stage::header);
	}

	server_parser source(32);
	auto parsed = source.append(buffer(request));
	RIWO_TEST_CHECK(parsed and *parsed);
	RIWO_TEST_CHECK(source.path_match("/{name}") >= 0);
	RIWO_TEST_CHECK_EQ(source.path_arg("name")->to_string(), "repeat");

	server_parser moved(std::move(source));
	RIWO_TEST_CHECK_EQ(moved.path(), "/repeat");
	moved.reset();
	RIWO_TEST_CHECK(moved.path_args().empty());

	// A moved-from parser remains reusable without eagerly reserving a large buffer.
	parsed = source.append(buffer(request));
	RIWO_TEST_CHECK(parsed and *parsed);
	RIWO_TEST_CHECK_EQ(source.path(), "/repeat");
}

void request_parser()
{
	using namespace riwo::http;
	const std::string head =
		"POST /users/42?q=hello%20world&flag HTTP/1.1\r\n"
		"Host: example.test\r\n"
		"Accept-Encoding: br, gzip\r\n"
		"Cookie: sid=abc; theme=dark\r\n"
		"Content-Length: 5\r\n\r\n";
	server_parser parser;
	auto partial = parser.append(buffer(std::string_view(head).substr(0, 20)));
	RIWO_TEST_CHECK(partial and not *partial);
	auto complete = parser.append(buffer(std::string_view(head).substr(20)));
	RIWO_TEST_CHECK(complete and *complete);
	RIWO_TEST_CHECK_EQ(parser.stage(), stage::body);
	complete = parser.append(buffer("hello"));
	RIWO_TEST_CHECK(complete and *complete);
	RIWO_TEST_CHECK_EQ(parser.stage(), stage::body);

	RIWO_TEST_CHECK_EQ(parser.method(), method::post);
	RIWO_TEST_CHECK_EQ(parser.version(), version::v11);
	RIWO_TEST_CHECK_EQ(parser.path(), "/users/42");
	RIWO_TEST_CHECK_EQ(parser.parameter("q")->to_string(), "hello world");
	RIWO_TEST_CHECK(parser.contains_parameter("flag"));
	RIWO_TEST_CHECK_EQ(parser.cookie("sid")->to_string(), "abc");
	RIWO_TEST_CHECK(parser.support_gzip());
	RIWO_TEST_CHECK(parser.keep_alive());
	RIWO_TEST_CHECK(parser.path_match("/users/{id}") >= 0);
	RIWO_TEST_CHECK_EQ(parser.path_arg("id")->to_string(), "42");
	RIWO_TEST_CHECK_EQ(parser.take_body(), "hello");
	RIWO_TEST_CHECK_EQ(parser.stage(), stage::finished);
}

void response_parser()
{
	using namespace riwo::http;
	for(const auto invalid_response : {
		"HTTP/1.1 10\r\n\r\n",
		"HTTP/1.1 099\r\n\r\n",
		"HTTP/1.1 599\r\n\r\n",
		"HTTP/1.1 600\r\n\r\n",
		"HTTP/1.1 2x0\r\n\r\n",
	})
	{
		client_parser invalid_parser;
		auto invalid_result = invalid_parser.append(buffer(invalid_response));
		RIWO_TEST_CHECK(not invalid_result);
		RIWO_TEST_CHECK(invalid_result.error() == parse_errc::invalid_status_code);
	}

	const std::string response =
		"HTTP/1.1 200 OK\r\n"
		"Content-Type: text/plain\r\n"
		"Transfer-Encoding: chunked\r\n"
		"Set-Cookie: sid=abc; Path=/; HttpOnly\r\n\r\n"
		"5\r\nhello\r\n6; trace=yes\r\n world\r\n0\r\nX-End: yes\r\n\r\n";
	client_parser parser;
	auto parsed = parser.append(buffer(response));
	RIWO_TEST_CHECK(parsed and *parsed);
	RIWO_TEST_CHECK_EQ(parser.status(), status::ok);
	RIWO_TEST_CHECK_EQ(parser.version(), version::v11);
	RIWO_TEST_CHECK(parser.is_chunked());
	RIWO_TEST_CHECK(parser.keep_alive());
	RIWO_TEST_CHECK_EQ(parser.take_body(), "hello world");
	RIWO_TEST_CHECK_EQ(parser.set_cookies().size(), 1U);
	RIWO_TEST_CHECK_EQ(parser.set_cookies().front().first, "sid");
	RIWO_TEST_CHECK_EQ(
		parser.set_cookies().front().second.value().to_string(), "abc"
	);
	RIWO_TEST_CHECK_EQ(*parser.set_cookies().front().second.path(), "/");
	RIWO_TEST_CHECK(*parser.set_cookies().front().second.http_only());
}

void generators_round_trip()
{
	using namespace riwo::http;
	request_arg argument;
	argument.set_header("X-Request", "yes")
		.set_cookie("sid", "token");
	client_generator request(riwo::url("http://example.test/api?q=42"), argument);
	std::string request_data = request.header_data(method::post, 7);
	request_data += request.body_data(buffer("payload"));
	RIWO_TEST_CHECK_EQ(request.pro_state(), generator_state::finish);

	server_parser request_parser;
	auto request_result = request_parser.append(buffer(request_data));
	RIWO_TEST_CHECK(request_result and *request_result);
	RIWO_TEST_CHECK_EQ(request_parser.path(), "/api");
	RIWO_TEST_CHECK_EQ(request_parser.parameter("q")->to_int().value_or(0), 42);
	RIWO_TEST_CHECK_EQ(request_parser.header("X-Request")->to_string(), "yes");
	RIWO_TEST_CHECK_EQ(request_parser.cookie("sid")->to_string(), "token");
	RIWO_TEST_CHECK_EQ(request_parser.take_body(), "payload");

	server_generator response;
	response.set_status(status::created)
		.set_header("X-Response", "yes")
		.set_cookie("session", cookie("ready").set_path("/"));
	std::string response_data = response.header_data(2);
	response_data += response.body_data(buffer("ok"));
	RIWO_TEST_CHECK_EQ(response.pro_state(), generator_state::finish);

	client_parser response_parser;
	auto response_result = response_parser.append(buffer(response_data));
	RIWO_TEST_CHECK(response_result and *response_result);
	RIWO_TEST_CHECK_EQ(response_parser.status(), status::created);
	RIWO_TEST_CHECK_EQ(response_parser.header("X-Response")->to_string(), "yes");
	RIWO_TEST_CHECK_EQ(response_parser.take_body(), "ok");
	RIWO_TEST_CHECK_EQ(response_parser.set_cookies().front().first, "session");
}

void request_url_boundaries()
{
	using namespace riwo::http;
	const riwo::url target(
		"http://example.test/a%2Fb//c?flag&empty=#client-fragment"
	);

	client_generator origin_form(target, request_arg {});
	auto origin_head = origin_form.header_data(method::get, 0);
	RIWO_TEST_CHECK(
		origin_head.starts_with("GET /a%2Fb//c?flag&empty= HTTP/1.1\r\n")
	);
	RIWO_TEST_CHECK(
		origin_head.find("Host: example.test\r\n") != std::string::npos
	);
	RIWO_TEST_CHECK(
		origin_head.find("example.test:0") == std::string::npos
	);
	RIWO_TEST_CHECK(
		origin_head.find("client-fragment") == std::string::npos
	);

	client_generator absolute_form(target, request_arg {});
	absolute_form.set_target_form(request_target_form::absolute);
	auto absolute_head = absolute_form.header_data(method::get, 0);
	RIWO_TEST_CHECK(
		absolute_head.starts_with(
			"GET http://example.test/a%2Fb//c?flag&empty= HTTP/1.1\r\n"
		)
	);
}

void range_headers()
{
	using namespace riwo::http;
	auto specifier = parse_range_header("bytes=0-9, 20-, -5");
	RIWO_TEST_CHECK(specifier);
	RIWO_TEST_CHECK_EQ(specifier->unit, "bytes");
	RIWO_TEST_CHECK_EQ(specifier->ranges.size(), 3U);

	const auto resolved = resolve_byte_ranges(*specifier, 30);
	RIWO_TEST_CHECK_EQ(resolved.size(), 3U);
	RIWO_TEST_CHECK_EQ(resolved[0].begin, 0U);
	RIWO_TEST_CHECK_EQ(resolved[0].total, 10U);
	RIWO_TEST_CHECK_EQ(resolved[1].begin, 20U);
	RIWO_TEST_CHECK_EQ(resolved[1].total, 10U);
	RIWO_TEST_CHECK_EQ(resolved[2].begin, 25U);
	RIWO_TEST_CHECK_EQ(resolved[2].total, 5U);
	RIWO_TEST_CHECK_EQ(format_content_range(resolved[0], 30), "bytes 0-9/30");
	RIWO_TEST_CHECK_EQ(format_unsatisfied_content_range(30), "bytes */30");

	auto content = parse_content_range("bytes 10-19/30");
	RIWO_TEST_CHECK(content and content->satisfied);
	RIWO_TEST_CHECK_EQ(content->length(), 10U);
	RIWO_TEST_CHECK_EQ(*content->complete_length, 30U);
	RIWO_TEST_CHECK(not parse_range_header("bytes=9-2"));
}

void multipart_byte_ranges_streaming()
{
	using namespace riwo::http;
	const std::string body =
		"preamble\r\n"
		"--riwo-range\r\n"
		"Content-Range: bytes 0-2/6\r\n"
		"X-Part: first\r\n\r\n"
		"abc\r\n"
		"--riwo-range\r\n"
		"Content-Range: bytes 3-5/6\r\n\r\n"
		"def\r\n"
		"--riwo-range--\r\n";

	multipart_byte_ranges_parser parser("riwo-range");
	std::array<std::string,2> payloads {};
	for(size_t offset=0; offset<body.size(); )
	{
		const auto size = std::min<size_t>((offset % 7) + 1, body.size() - offset);
		auto chunks = parser.append(std::string_view(body).substr(offset, size));
		RIWO_TEST_CHECK(chunks);
		for(auto &chunk : *chunks)
		{
			RIWO_TEST_CHECK(chunk.part_index < payloads.size());
			payloads[chunk.part_index] += chunk.data;
		}
		offset += size;
	}
	RIWO_TEST_CHECK(parser.finished());
	RIWO_TEST_CHECK(not parser.finish());
	RIWO_TEST_CHECK_EQ(parser.parts().size(), 2U);
	RIWO_TEST_CHECK_EQ(payloads[0], "abc");
	RIWO_TEST_CHECK_EQ(payloads[1], "def");
	RIWO_TEST_CHECK_EQ(parser.parts()[0].range.first, 0U);
	RIWO_TEST_CHECK_EQ(parser.parts()[1].range.first, 3U);
}

void conditional_and_upgrade_headers()
{
	using namespace riwo::http;
	auto tag = parse_entity_tag("W/\"revision-1\"");
	RIWO_TEST_CHECK(tag and tag->weak);
	RIWO_TEST_CHECK_EQ(tag->opaque, "revision-1");
	RIWO_TEST_CHECK(not strong_entity_tag_equal("W/\"x\"", "\"x\""));
	RIWO_TEST_CHECK(weak_entity_tag_equal("W/\"x\"", "\"x\""));

	const auto point = std::chrono::system_clock::time_point(std::chrono::seconds(784111777));
	const auto date = format_http_date(point);
	RIWO_TEST_CHECK(parse_http_date(date).has_value());

	headers representation {{header::etag, "\"revision-1\""}};
	headers request {{header::if_none_match, "W/\"revision-1\""}};
	RIWO_TEST_CHECK_EQ(
		evaluate_preconditions(method::get, request, representation),
		precondition_result::not_modified
	);
	RIWO_TEST_CHECK_EQ(
		evaluate_preconditions(method::post, request, representation),
		precondition_result::precondition_failed
	);

	headers upgrade {
		{header::connection, "keep-alive, Upgrade"},
		{header::upgrade, "websocket"}
	};
	RIWO_TEST_CHECK(header_has_token(upgrade, header::connection, "upgrade"));
	RIWO_TEST_CHECK(is_upgrade_request(upgrade));
	RIWO_TEST_CHECK(is_upgrade_response(status::switching_protocols, upgrade));
	RIWO_TEST_CHECK_EQ(upgrade_protocol(upgrade).value_or(""), "websocket");
}

void form_data_and_authentication()
{
	using namespace riwo::http;
	multipart_form_data form("riwo-boundary");
	form.add_field("title", "hello")
		.add_file("upload", "a.txt", "file-data", "text/plain");
	RIWO_TEST_CHECK_EQ(form.parts().size(), 2U);
	RIWO_TEST_CHECK_EQ(form_data_boundary(form.content_type()).value_or(""), "riwo-boundary");
	auto parsed = parse_multipart_form_data(form.content_type(), form.body());
	RIWO_TEST_CHECK(parsed);
	RIWO_TEST_CHECK_EQ(parsed->size(), 2U);
	RIWO_TEST_CHECK_EQ((*parsed)[0].name, "title");
	RIWO_TEST_CHECK_EQ((*parsed)[0].data, "hello");
	RIWO_TEST_CHECK_EQ((*parsed)[1].filename, "a.txt");
	RIWO_TEST_CHECK_EQ((*parsed)[1].data, "file-data");

	request_arg argument;
	argument.set_basic_auth("user", "password");
	const auto basic = argument.header(header::authorization)->to_string();
	if(basic != "Basic dXNlcjpwYXNzd29yZA==")
		throw std::runtime_error("unexpected Basic credentials: " + basic);
	argument.set_bearer_auth("token");
	RIWO_TEST_CHECK_EQ(
		argument.header(header::authorization)->to_string(), "Bearer token"
	);
	form.apply(argument);
	RIWO_TEST_CHECK_EQ(argument.header(header::content_type)->to_string(), form.content_type());
}

void cookie_storage_policy()
{
	using namespace riwo::http;
	cookie_jar jar;
	const riwo::url origin("https://api.example.test/account/login");
	RIWO_TEST_CHECK(jar.store(origin, "host", cookie("one").set_path("/account")));
	RIWO_TEST_CHECK(jar.store(origin, "domain",
		cookie("two").set_domain("example.test").set_path("/")
	));
	RIWO_TEST_CHECK(jar.store(origin, "secure", cookie("three").set_secure(true)));
	RIWO_TEST_CHECK(not jar.store(
		riwo::url("http://api.example.test/"), "invalid-secure", cookie("x").set_secure(true)
	));
	RIWO_TEST_CHECK_EQ(jar.size(), 3U);

	auto account = jar.cookies_for(riwo::url("https://api.example.test/account/profile"));
	RIWO_TEST_CHECK_EQ(account.at("host").to_string(), "one");
	RIWO_TEST_CHECK_EQ(account.at("domain").to_string(), "two");
	RIWO_TEST_CHECK_EQ(account.at("secure").to_string(), "three");
	auto sibling = jar.cookies_for(riwo::url("https://www.example.test/"));
	RIWO_TEST_CHECK(not sibling.contains("host"));
	RIWO_TEST_CHECK(sibling.contains("domain"));
	auto plain = jar.cookies_for(riwo::url("http://api.example.test/account/profile"));
	RIWO_TEST_CHECK(not plain.contains("secure"));

	RIWO_TEST_CHECK(jar.store(origin, "host", cookie("gone").set_path("/account").set_max_age(0)));
	RIWO_TEST_CHECK(not jar.cookies_for(origin).contains("host"));
	const auto future_expiry = static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::seconds>(
			std::chrono::system_clock::now().time_since_epoch()
		).count() + 3'600
	);
	RIWO_TEST_CHECK(jar.store(origin, "expiry-index",
		cookie("timed").set_expires(future_expiry)
	));
	RIWO_TEST_CHECK(jar.store(origin, "expiry-index", cookie("session")));
	RIWO_TEST_CHECK_EQ(
		jar.cookies_for(origin).at("expiry-index").to_string(), "session"
	);
	RIWO_TEST_CHECK(jar.store(origin, "expiry-index",
		cookie("gone").set_max_age(0)
	));

	// Unrelated domains must not affect candidate lookup for the requested host.
	for(size_t index = 0; index < 500; ++index)
	{
		const auto host = "host" + std::to_string(index) + ".invalid";
		RIWO_TEST_CHECK(jar.store(
			riwo::url("https://" + host + "/"),
			"unrelated", cookie(std::to_string(index)).set_path("/")
		));
	}
	auto indexed = jar.cookies_for(origin);
	RIWO_TEST_CHECK_EQ(indexed.size(), 2U);
	RIWO_TEST_CHECK_EQ(indexed.at("domain").to_string(), "two");
	RIWO_TEST_CHECK_EQ(indexed.at("secure").to_string(), "three");
	jar.clear();
	RIWO_TEST_CHECK_EQ(jar.size(), 0U);
}

} //namespace

int main(int argc, const char *const argv[])
{
	return riwo::test::run(argc, argv, {
		{"parser errors", parser_errors},
		{"enum input validation", enum_input_validation},
		{"HTTP version contract", version_contract},
		{"parser reuse", parser_reuse},
		{"request parser", request_parser},
		{"response parser", response_parser},
		{"generators round trip", generators_round_trip},
		{"request URL boundaries", request_url_boundaries},
		{"range headers", range_headers},
		{"multipart byte ranges streaming", multipart_byte_ranges_streaming},
		{"conditional and upgrade headers", conditional_and_upgrade_headers},
		{"form data and authentication", form_data_and_authentication},
		{"cookie storage policy", cookie_storage_policy},
	});
}
