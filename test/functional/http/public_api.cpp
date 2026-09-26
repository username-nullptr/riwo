// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "test.h"

#include <riwo/http/client.h>
#include <riwo/http/client/connection_pool.h>
#include <riwo/http/client/connector.h>
#include <riwo/http/client/reply.h>
#include <riwo/http/cxx/configs.h>
#include <riwo/http/cxx/container.h>
#include <riwo/http/protocol/utils/core/compression.h>
#include <riwo/http/server/acceptor_wrap.h>
#include <riwo/http/server/aop.h>
#include <riwo/http/server/request.h>
#include <riwo/http/server/response.h>
#include <riwo/http/server/server.h>
#include <riwo/http/server/service_context.h>
#include <riwo/http/server/session_manager.h>
#include <riwo/http/client/connection_lease.h>
#include <riwo/http/utils/connection.h>
#include <riwo/http/utils/file_opt_token.h>
#include <riwo/http/utils/opt_token.h>
#include <riwo/http/utils/tcp_connection.h>
#if RIWO_OPENSSL_SUPPORT
# include <riwo/http/utils/tls_connection.h>
#endif

namespace
{

static_assert(riwo::test::canonical_executor_type<riwo::http::client>);
static_assert(riwo::test::canonical_executor_type<riwo::http::connection>);
static_assert(riwo::test::canonical_executor_type<riwo::http::connection_lease>);
static_assert(riwo::test::canonical_executor_type<riwo::http::connection_pool>);
static_assert(riwo::test::canonical_executor_type<riwo::http::connector>);
static_assert(riwo::test::canonical_executor_type<riwo::http::basic_reply<>>);
static_assert(riwo::test::canonical_executor_type<
	riwo::http::request_context<riwo::http::method::get>>);
static_assert(riwo::test::canonical_executor_type<riwo::http::acceptor_wrap>);
static_assert(riwo::test::canonical_executor_type<riwo::http::aop>);
static_assert(riwo::test::canonical_executor_type<riwo::http::ctrlr_aop>);
static_assert(riwo::test::canonical_executor_type<riwo::http::request>);
static_assert(riwo::test::canonical_executor_type<riwo::http::response>);
static_assert(riwo::test::canonical_executor_type<riwo::http::server>);
static_assert(riwo::test::canonical_executor_type<riwo::http::service_context>);
static_assert(riwo::test::canonical_executor_type<riwo::http::session>);
static_assert(riwo::test::canonical_executor_type<riwo::http::tcp_connection>);
#if RIWO_OPENSSL_SUPPORT
static_assert(riwo::test::canonical_executor_type<riwo::http::tls_connection>);
#endif
static_assert(requires(riwo::http::session &value) {
	{ value.get_executor() } -> std::same_as<riwo::http::session::executor_t>;
});
static_assert(requires(riwo::http::acceptor_wrap &value) {
	{ value.get_executor() } -> std::same_as<riwo::http::acceptor_wrap::executor_t>;
});
static_assert(requires(riwo::http::connection_lease &value) {
	{ value.get_executor() } -> std::same_as<riwo::http::connection_lease::executor_t>;
});

template <riwo::http::method_enum Method>
struct rejected_client_completion
{
	size_t *completed;

	void operator()(riwo::error_code error,
		riwo::http::client::context_ptr<Method> context) const
	{
		RIWO_TEST_CHECK_EQ(error,
			std::make_error_code(std::errc::protocol_not_supported));
		RIWO_TEST_CHECK(not context);
		++*completed;
	}
};

void endpoints_and_case_insensitive_containers()
{
	riwo::http::endpoint endpoint;
	RIWO_TEST_CHECK(endpoint.from_string("127.0.0.1:8080"));
	RIWO_TEST_CHECK_EQ(endpoint.to_string(), "127.0.0.1:8080");
	RIWO_TEST_CHECK(endpoint.from_string("[::1]:443"));
	RIWO_TEST_CHECK_EQ(endpoint.to_string(), "[::1]:443");
	RIWO_TEST_CHECK(not endpoint.from_string("::1:443"));
	RIWO_TEST_CHECK(not endpoint.from_string("127.0.0.1:65536"));
	RIWO_TEST_CHECK(not endpoint.from_string("host.test:80"));

	riwo::http::value_map values {{"Content-Type", "text/plain"}};
	RIWO_TEST_CHECK_EQ(
		riwo::http::value_map_get(values, "content-type")->to_string(),
		"text/plain"
	);
	RIWO_TEST_CHECK(not riwo::http::value_map_get(values, "missing"));
	riwo::http::value_set names {"GZip", "br"};
	RIWO_TEST_CHECK_EQ(
		riwo::http::value_set_get(names, "gzip")->to_string(), "GZip"
	);
}

void client_configuration()
{
	riwo::io_context_t context;
	riwo::http::client default_client(context.get_executor());
	RIWO_TEST_CHECK(default_client.config().no_delay);
	RIWO_TEST_CHECK(std::holds_alternative<riwo::http::use_global_proxy_t>(
		default_client.config().default_proxy));

	riwo::http::client_config config;
	config.no_delay = false;
	config.default_proxy = riwo::http::no_proxy;
	riwo::http::client delayed_client(config);
	RIWO_TEST_CHECK(not delayed_client.config().no_delay);
	RIWO_TEST_CHECK(std::holds_alternative<riwo::http::no_proxy_t>(
		delayed_client.config().default_proxy));

	riwo::http::client::req_info request("http://example.test/");
	RIWO_TEST_CHECK(not request.proxy);
	request.proxy = riwo::url("http://proxy.test:8080");
	RIWO_TEST_CHECK(std::holds_alternative<riwo::url>(*request.proxy));
}

void client_convenience_overloads()
{
	using namespace riwo::http;
	riwo::io_context_t context;
	client requester(context.get_executor());
	constexpr std::string_view unsupported = "ftp://example.test/resource";
	size_t completed = 0;

	requester.make_get(unsupported, rejected_client_completion<method::get>{&completed});
	requester.make_put(unsupported, rejected_client_completion<method::put>{&completed});
	requester.make_post(unsupported, rejected_client_completion<method::post>{&completed});
	requester.make_head(unsupported, rejected_client_completion<method::head>{&completed});
	requester.make_patch(unsupported, rejected_client_completion<method::patch>{&completed});
	requester.make_delete(unsupported, rejected_client_completion<method::delet>{&completed});
	requester.make_options(unsupported, rejected_client_completion<method::options>{&completed});
	requester.make_trace(unsupported, rejected_client_completion<method::trace>{&completed});
	requester.make_connect(unsupported, rejected_client_completion<method::connect>{&completed});

	requester.request_get(unsupported, rejected_client_completion<method::get>{&completed});
	requester.request_put(unsupported, rejected_client_completion<method::put>{&completed});
	requester.request_post(unsupported, rejected_client_completion<method::post>{&completed});
	requester.request_head(unsupported, rejected_client_completion<method::head>{&completed});
	requester.request_patch(unsupported, rejected_client_completion<method::patch>{&completed});
	requester.request_delete(unsupported, rejected_client_completion<method::delet>{&completed});
	requester.request_options(unsupported, rejected_client_completion<method::options>{&completed});
	requester.request_trace(unsupported, rejected_client_completion<method::trace>{&completed});
	requester.request_connect(unsupported, rejected_client_completion<method::connect>{&completed});

	requester.make_context<method::get>(unsupported,
		rejected_client_completion<method::get>{&completed});
	requester.request<method::get>(unsupported,
		rejected_client_completion<method::get>{&completed});
	RIWO_TEST_CHECK_EQ(completed, 0U);
	context.run();
	RIWO_TEST_CHECK_EQ(completed, 20U);
}

void content_coding_and_mime_policy()
{
	using namespace riwo::http;
	RIWO_TEST_CHECK(std::abs(content_coding_quality(
		"br;q=0.4, gzip; q=0.8, *;q=0.1", "GZIP") - 0.8) < 0.0001);
	RIWO_TEST_CHECK_EQ(content_coding_quality("*;q=0.5", "deflate"), 0.5);
	RIWO_TEST_CHECK_EQ(content_coding_quality("gzip;q=1.1, *;q=0.2", "gzip"), 0.0);
	RIWO_TEST_CHECK_EQ(content_coding_quality("br", "gzip"), 0.0);

	RIWO_TEST_CHECK(is_compressible_mime_type("Text/Plain; charset=utf-8"));
	RIWO_TEST_CHECK(is_compressible_mime_type("application/problem+json"));
	RIWO_TEST_CHECK(not is_compressible_mime_type("image/png"));
	RIWO_TEST_CHECK(is_precompressed_mime_type("application/pdf"));
	RIWO_TEST_CHECK(not is_precompressed_mime_type("image/svg+xml"));
}

void gzip_codec()
{
	using namespace riwo::http;
	const std::string input = std::string(4096, 'a') + std::string(4096, 'b');
#if RIWO_HTTP_ZLIB_SUPPORT
	auto compressed = gzip_compress(input);
	RIWO_TEST_CHECK(compressed);
	RIWO_TEST_CHECK(compressed->size() < input.size());
	RIWO_TEST_CHECK_EQ(gzip_decompress(*compressed).value(), input);
	RIWO_TEST_CHECK(not gzip_decompress("not gzip"));
	RIWO_TEST_CHECK(not gzip_decompress(*compressed, input.size() - 1));

	gzip_encoder encoder;
	auto first = encoder.append(std::string_view(input).substr(0, 1024));
	auto last = encoder.append(std::string_view(input).substr(1024), true);
	RIWO_TEST_CHECK(first and last and encoder.finished());
	gzip_decoder decoder(input.size());
	auto decoded_first = decoder.append(*first);
	auto decoded_last = decoder.append(*last, true);
	RIWO_TEST_CHECK(decoded_first and decoded_last and decoder.finished());
	RIWO_TEST_CHECK_EQ(*decoded_first + *decoded_last, input);
	RIWO_TEST_CHECK_EQ(decoder.output_size(), input.size());
#else
	auto compressed = gzip_compress(input);
	RIWO_TEST_CHECK(not compressed);
	RIWO_TEST_CHECK(compressed.error() == std::errc::operation_not_supported);
	gzip_encoder encoder;
	RIWO_TEST_CHECK(not encoder.append(input, true));
	RIWO_TEST_CHECK(not encoder.finished());
	gzip_decoder decoder;
	RIWO_TEST_CHECK(not decoder.append(input, true));
	RIWO_TEST_CHECK_EQ(decoder.output_size(), 0U);
#endif
}

void file_option_tokens()
{
	using namespace riwo::http;
	using namespace riwo::http::operators;
	riwo::test::temporary_directory directory;
	const auto file = directory.path() / "payload.txt";
	{
		std::ofstream output(file, std::ios::binary);
		output << "0123456789";
	}

	auto single = make_file_opt_token(file, file_range {2, 4});
	static_assert(decltype(single)::optype == file_optype::single);
	RIWO_TEST_CHECK(single.init(std::ios::in | std::ios::binary));
	RIWO_TEST_CHECK_EQ(single.file_size, 10U);
	RIWO_TEST_CHECK_EQ(single.mime_type, "text/plain");
	RIWO_TEST_CHECK_EQ(single.range->begin, 2U);

	auto multiple = file | file_range {0, 2} | file_range {8, 2};
	static_assert(decltype(multiple)::optype == file_optype::multiple);
	RIWO_TEST_CHECK(multiple.init(std::ios::in | std::ios::binary));
	RIWO_TEST_CHECK_EQ(multiple.ranges.size(), 2U);
	RIWO_TEST_CHECK_EQ(multiple.file_size, 10U);

	std::ifstream input(file, std::ios::binary);
	auto borrowed = make_file_opt_token(input, file_ranges {{1, 2}, {5, 3}});
	RIWO_TEST_CHECK(borrowed.init(std::ios::in | std::ios::binary));
	RIWO_TEST_CHECK(borrowed.stream == &input);
	RIWO_TEST_CHECK_EQ(borrowed.ranges.size(), 2U);

	auto missing = make_file_opt_token(file.string() + ".missing");
	auto missing_result = missing.init(std::ios::in | std::ios::binary);
	RIWO_TEST_CHECK(not missing_result);
	RIWO_TEST_CHECK(missing_result.error() == std::errc::no_such_file_or_directory);

	path_opt_token paths("/one", "/two");
	RIWO_TEST_CHECK_EQ(paths.paths.size(), 2U);
	RIWO_TEST_CHECK_EQ(paths.paths[1], "/two");
}

void sessions()
{
	using namespace std::chrono_literals;
	riwo::io_context_t context;
	riwo::http::session_manager manager;
	manager.set_lifecycle(1500ms).set_cookie_key("sid");
	RIWO_TEST_CHECK_EQ(manager.lifecycle(), 1s);
	RIWO_TEST_CHECK_EQ(manager.cookie_key(), "sid");
	RIWO_TEST_CHECK_THROWS(manager.set_cookie_key(""), riwo::runtime_error);

	auto session = manager.make(context.get_executor());
	RIWO_TEST_CHECK(session->get_executor() == context.get_executor());
	const std::string id(session->id());
	RIWO_TEST_CHECK(not id.empty());
	RIWO_TEST_CHECK(session->is_valid());
	RIWO_TEST_CHECK_EQ(session->lifecycle(), 1s);
	session->set_attribute("counter", 42).set_attribute("name", std::string("test"));
	RIWO_TEST_CHECK_EQ(std::any_cast<int>(*session->attribute("COUNTER")), 42);
	RIWO_TEST_CHECK_EQ(std::any_cast<std::string>(*session->attribute("name")), "test");
	session->unset_attribute("counter");
	RIWO_TEST_CHECK(not session->attribute("counter"));
	RIWO_TEST_CHECK(manager.get(id) == session);
	RIWO_TEST_CHECK(not manager.get_or("missing"));

	riwo::io_context_t callback_context;
	auto callback_free = std::make_shared<riwo::http::session>(
		0s, callback_context.get_executor());
	callback_context.run();
	RIWO_TEST_CHECK(not callback_free->is_valid());
	RIWO_TEST_CHECK_THROWS(manager.get("missing"), riwo::runtime_error);
	session->invalidate();
	RIWO_TEST_CHECK(not session->is_valid());
}

} //namespace

int main(int argc, const char *const argv[])
{
	return riwo::test::run(argc, argv, {
		{"endpoints and case-insensitive containers", endpoints_and_case_insensitive_containers},
		{"client configuration", client_configuration},
		{"client convenience overloads", client_convenience_overloads},
		{"content coding and MIME policy", content_coding_and_mime_policy},
		{"gzip codec", gzip_codec},
		{"file option tokens", file_option_tokens},
		{"sessions", sessions},
	});
}
