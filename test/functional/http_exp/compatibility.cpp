// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "test.h"
#include <riwo/http_exp.h>
#include <riwo/core/cxx/asio.h>
#include <fstream>

namespace
{
namespace http = riwo::http_exp;

[[nodiscard]] std::string body_text(const http::reply &reply)
{
	auto value = reply.buffered_body();
	return {reinterpret_cast<const char*>(value.data()), value.size()};
}

struct trace_middleware final : http::middleware
{
	explicit trace_middleware(std::vector<std::string> &trace) : trace(trace) {}
	riwo::awaitable<bool> before(http::server_exchange&) override {
		trace.emplace_back("before");
		co_return false;
	}
	riwo::awaitable<bool> after(http::server_exchange&) override {
		trace.emplace_back("after");
		co_return false;
	}
	std::vector<std::string> &trace;
};

struct short_circuit_middleware final : http::middleware
{
	riwo::awaitable<bool> before(http::server_exchange &exchange) override
	{
		co_await exchange.respond({http::status::accepted, {}},
			std::string_view("stopped"), riwo::use_awaitable);
		co_return true;
	}
};

struct exception_middleware final : http::middleware
{
	explicit exception_middleware(bool &seen) : seen(seen) {}
	bool exception(http::server_exchange&, const std::exception&) override {
		seen = true;
		return true;
	}
	bool &seen;
};

void semantic_helpers()
{
	const std::string non_ascii_token {'X', '-', static_cast<char>(0xE9)};
	bool invalid_field_rejected = false;
	try {
		http::fields invalid;
		invalid.add(non_ascii_token, "value");
	}
	catch(const std::invalid_argument&) {
		invalid_field_rejected = true;
	}
	RIWO_TEST_CHECK(invalid_field_rejected);
	bool invalid_method_rejected = false;
	try { (void)http::method(non_ascii_token); }
	catch(const std::invalid_argument&) {
		invalid_method_rejected = true;
	}
	RIWO_TEST_CHECK(invalid_method_rejected);

	auto query = http::parse_query_parameters("q=one+two&q=three&empty=");
	RIWO_TEST_CHECK(query.has_value());
	RIWO_TEST_CHECK_EQ(query->find_first("q").value(), "one two");
	RIWO_TEST_CHECK_EQ(query->find_all("q").size(), 2U);

	auto range = http::parse_range_header("bytes=0-2,-2");
	RIWO_TEST_CHECK(range.has_value());
	RIWO_TEST_CHECK(!http::parse_range_header(
		std::string("byt") + static_cast<char>(0xE9) + "s=0-1").has_value());
	auto resolved = http::resolve_byte_ranges(*range, 10);
	RIWO_TEST_CHECK_EQ(resolved.size(), 2U);
	RIWO_TEST_CHECK_EQ(http::format_content_range(resolved.front(), 10),
		"bytes 0-2/10");

	const auto now = std::chrono::system_clock::now();
	const auto date = http::format_http_date(now);
	RIWO_TEST_CHECK(http::parse_http_date(date).has_value());
	RIWO_TEST_CHECK(http::weak_entity_tag_equal("W/\"demo\"", "\"demo\""));
	RIWO_TEST_CHECK(!http::strong_entity_tag_equal("W/\"demo\"", "\"demo\""));

	auto authorization = http::make_basic_authorization("demo", "secret");
	auto credentials = http::parse_basic_authorization(authorization);
	RIWO_TEST_CHECK(credentials.has_value());
	RIWO_TEST_CHECK_EQ(credentials->username, "demo");
	RIWO_TEST_CHECK_EQ(credentials->password, "secret");

	http::multipart_form form("riwo-boundary");
	form.add_field("name", "value").add_file("upload", "a.txt", "abc",
		"text/plain");
	auto parts = http::parse_multipart_form(form.content_type(), form.body());
	RIWO_TEST_CHECK(parts.has_value());
	RIWO_TEST_CHECK_EQ(parts->size(), 2U);
	RIWO_TEST_CHECK_EQ(parts->back().filename, "a.txt");
	RIWO_TEST_CHECK_EQ(parts->back().data, "abc");
	auto form_boundary = http::multipart_boundary(
		"Multipart/Form-Data; charset=utf-8; boundary=\"riwo-boundary\"");
	RIWO_TEST_CHECK(form_boundary.has_value());
	RIWO_TEST_CHECK_EQ(*form_boundary, "riwo-boundary");
	auto ranges_boundary = http::parse_multipart_byte_ranges_boundary(
		"Multipart/ByteRanges; boundary=\"riwo-ranges\"");
	RIWO_TEST_CHECK(ranges_boundary.has_value());
	RIWO_TEST_CHECK_EQ(*ranges_boundary, "riwo-ranges");

	RIWO_TEST_CHECK_EQ(http::content_coding_quality(
		"br;q=0.4, gzip;q=0.8", "gzip"), 0.8);
	RIWO_TEST_CHECK(http::is_compressible_mime_type("text/plain; charset=utf-8"));
#if RIWO_HTTP_EXP_ZLIB_SUPPORT
	auto compressed = http::gzip_compress("compress me");
	RIWO_TEST_CHECK(compressed.has_value());
	auto decompressed = http::gzip_decompress(*compressed);
	RIWO_TEST_CHECK(decompressed.has_value());
	RIWO_TEST_CHECK_EQ(*decompressed, "compress me");
#endif
}

void application_compatibility_for(http::protocol_requirement requirement,
	http::protocol expected_protocol)
{
	riwo::test::temporary_directory temporary;
	const auto asset_path = temporary.path() / "asset.txt";
	{
		std::ofstream output(asset_path, std::ios::binary);
		output << "0123456789";
	}
	const auto stream_upload_path = temporary.path() / "stream-upload.bin";
	const auto c_upload_path = temporary.path() / "c-upload.bin";

	riwo::io_context_t context;
	asio::ip::tcp::acceptor acceptor(context);
	http::server_config config;
	config.resource_root = temporary.path();
	config.sessions.lifetime = std::chrono::seconds(60);
	http::server server(std::move(acceptor), config);
	std::vector<std::string> trace;
	bool controller_called = false;
	bool exception_seen = false;
	auto middleware = std::make_shared<trace_middleware>(trace);
	server
		.bind({riwo::ip_type::v4, 0})
		.on_request<http::method::get>("/users/{id}",
		[](http::server_exchange &exchange) -> riwo::awaitable<void>
		{
			auto session = exchange.session();
			auto count = session->attribute_as<int>("count").value_or(0) + 1;
			session->set_attribute("count", count);
			auto tags = exchange.query_parameters().find_all("tag");
			auto response = std::format("{}:{}:{}",
				exchange.path_parameter("id").value_or(""), tags.size(), count);
			co_await exchange.respond({http::status::ok, {}}, response,
				riwo::use_awaitable);
		}, middleware)
		.on_request<http::method::get>("/asset",
		[](http::server_exchange &exchange) -> riwo::awaitable<void>
		{
			co_await exchange.respond_file("asset.txt", {},
				riwo::use_awaitable);
		})
		.on_request<http::method::get>("/asset-absolute",
		[asset_path]
		(http::server_exchange &exchange) -> riwo::awaitable<void>
		{
			co_await exchange.respond_file(asset_path, {},
				riwo::use_awaitable);
		})
		.on_request<http::method::post>("/upload",
		[stream_upload_path]
		(http::server_exchange &exchange) -> riwo::awaitable<void>
		{
			std::fstream output(stream_upload_path, std::ios::binary |
				std::ios::in | std::ios::out | std::ios::trunc);
			if( not output ) throw std::runtime_error("cannot open upload file");
			const auto saved = co_await exchange.save_file(
				http::borrowed_file(output), riwo::use_awaitable);
			http::response_head head {http::status::no_content, {}};
			head.headers.add("X-Saved-Bytes", std::to_string(saved));
			co_await exchange.respond(std::move(head),
				http::body_spec::empty(), riwo::use_awaitable);
		})
		.on_request<http::method::post>("/upload-c",
		[c_upload_path]
		(http::server_exchange &exchange) -> riwo::awaitable<void>
		{
			auto *output = std::fopen(c_upload_path.string().c_str(), "w+b");
			if( not output ) throw std::runtime_error("cannot open C upload file");
			const auto saved = co_await exchange.save_file(
				http::owned_file(output), riwo::use_awaitable);
			http::response_head head {http::status::no_content, {}};
			head.headers.add("X-Saved-Bytes", std::to_string(saved));
			co_await exchange.respond(std::move(head),
				http::body_spec::empty(), riwo::use_awaitable);
		})
		.on_request<http::method::post>("/duplex-progress",
		[stream_upload_path]
		(http::server_exchange &exchange) -> riwo::awaitable<void>
		{
			std::fstream output(stream_upload_path, std::ios::binary |
				std::ios::in | std::ios::out | std::ios::trunc);
			if( not output ) throw std::runtime_error("cannot open upload file");
			co_await exchange.save_file(http::borrowed_file(output),
				riwo::use_awaitable);
			co_await exchange.respond({http::status::ok, {}},
				std::string_view("response"), riwo::use_awaitable);
		})
#if RIWO_HTTP_EXP_ZLIB_SUPPORT
		.on_request<http::method::get>("/compress",
		[](http::server_exchange &exchange) -> riwo::awaitable<void>
		{
			http::response_head head;
			head.headers.add("Content-Type", "text/plain");
			head.headers.add("Vary", "X-Accept-Encoding-Suffix");
			const std::string body(2048, 'z');
			co_await exchange.respond(std::move(head), body,
				riwo::use_awaitable);
		})
#endif
		.on_request<http::method::get>("/redirect",
		[](http::server_exchange &exchange) -> riwo::awaitable<void>
		{
			http::response_head head {http::status::found, {}};
			head.headers.add("Location", "/users/redirected");
			co_await exchange.respond(std::move(head), std::string_view{},
				riwo::use_awaitable);
		})
		.on_request<http::method::get>("/short",
		[&](http::server_exchange&) -> riwo::awaitable<void>
		{
			controller_called = true;
			co_return;
		}, std::make_shared<short_circuit_middleware>())
		.on_request<http::method::get>("/exception",
		[](http::server_exchange&) -> riwo::awaitable<void>
		{
			throw std::runtime_error("expected");
			co_return;
		}, std::make_shared<exception_middleware>(exception_seen))
		.start();

	const auto base = std::format("http://127.0.0.1:{}",
		server.acceptor().local_endpoint().port());
	http::client_config client_config;
	client_config.protocols.requirement = requirement;
	client_config.pool.idle_timeout = std::chrono::milliseconds(1);
	http::client client(context, client_config);
	auto completed = asio::co_spawn(context, [&]() -> riwo::awaitable<void>
	{
		auto first = co_await client.fetch(http::request(http::method::get,
			base + "/users/alice?tag=a&tag=b"), riwo::use_awaitable);
		RIWO_TEST_CHECK_EQ(first.selected_protocol(), expected_protocol);
		RIWO_TEST_CHECK_EQ(body_text(first), "alice:2:1");
		auto second = co_await client.fetch(http::request(http::method::get,
			base + "/users/alice"), riwo::use_awaitable);
		RIWO_TEST_CHECK_EQ(body_text(second), "alice:0:2");
		// The client can receive the response before the server coroutine resumes
		// from respond() and unwinds the middleware chain.
		asio::steady_timer trace_timer(context);
		for(size_t attempt = 0; attempt < 100 and trace.size() < 4U; ++attempt)
		{
			trace_timer.expires_after(std::chrono::milliseconds(1));
			co_await trace_timer.async_wait(riwo::use_awaitable);
		}
		RIWO_TEST_CHECK_EQ(trace, (std::vector<std::string>{
			"before", "after", "before", "after"}));

		uint64_t downloaded = 0;
		uint64_t download_total = 0;
		http::request file_request(http::method::get, base + "/asset");
		file_request.on_download_progress([&](uint64_t transferred,
			uint64_t total) {
			downloaded = transferred;
			download_total = total;
		});
		auto file = co_await client.fetch(std::move(file_request),
			riwo::use_awaitable);
		RIWO_TEST_CHECK_EQ(body_text(file), "0123456789");
		RIWO_TEST_CHECK_EQ(downloaded, 10U);
		RIWO_TEST_CHECK_EQ(download_total, 10U);
		RIWO_TEST_CHECK_EQ(file.headers().find_first("content-type").value(),
			"text/plain; charset=utf-8");
		RIWO_TEST_CHECK(file.headers().find_first("etag").has_value());
		auto absolute_file = co_await client.fetch(http::request(
			http::method::get, base + "/asset-absolute"), riwo::use_awaitable);
		RIWO_TEST_CHECK_EQ(body_text(absolute_file), "0123456789");

		uint64_t uploaded = 0;
		uint64_t upload_total = 0;
		http::request upload(http::method::post, base + "/upload");
		upload.body("payload").on_upload_progress(
			[&](uint64_t transferred, uint64_t total) {
				uploaded = transferred;
				upload_total = total;
			});
		auto uploaded_reply = co_await client.fetch(std::move(upload),
			riwo::use_awaitable);
		RIWO_TEST_CHECK_EQ(uploaded_reply.status(), http::status::no_content);
		RIWO_TEST_CHECK_EQ(uploaded, 7U);
		RIWO_TEST_CHECK_EQ(upload_total, 7U);
		RIWO_TEST_CHECK_EQ(uploaded_reply.headers().find_first(
			"x-saved-bytes").value(), "7");
		{
			std::ifstream input(stream_upload_path, std::ios::binary);
			RIWO_TEST_CHECK_EQ(std::string((std::istreambuf_iterator<char>(input)),
				{}), "payload");
		}

		uint64_t duplex_uploaded = 0;
		uint64_t duplex_upload_total = 0;
		uint64_t duplex_downloaded = 0;
		uint64_t duplex_download_total = 0;
		http::request duplex(http::method::post, base + "/duplex-progress");
		duplex.body("request")
			.on_upload_progress(
			[&](uint64_t transferred, uint64_t total) {
				duplex_uploaded = transferred;
				duplex_upload_total = total;
			})
			.on_download_progress(
			[&](uint64_t transferred, uint64_t total) {
				duplex_downloaded = transferred;
				duplex_download_total = total;
			});
		auto duplex_reply = co_await client.fetch(std::move(duplex),
			riwo::use_awaitable);
		RIWO_TEST_CHECK_EQ(body_text(duplex_reply), "response");
		RIWO_TEST_CHECK_EQ(duplex_uploaded, 7U);
		RIWO_TEST_CHECK_EQ(duplex_upload_total, 7U);
		RIWO_TEST_CHECK_EQ(duplex_downloaded, 8U);
		RIWO_TEST_CHECK_EQ(duplex_download_total, 8U);

		http::request c_upload(http::method::post, base + "/upload-c");
		c_upload.body("c-file");
		auto c_uploaded_reply = co_await client.fetch(std::move(c_upload),
			riwo::use_awaitable);
		RIWO_TEST_CHECK_EQ(c_uploaded_reply.status(), http::status::no_content);
		RIWO_TEST_CHECK_EQ(c_uploaded_reply.headers().find_first(
			"x-saved-bytes").value(), "6");
		{
			std::ifstream input(c_upload_path, std::ios::binary);
			RIWO_TEST_CHECK_EQ(std::string((std::istreambuf_iterator<char>(input)),
				{}), "c-file");
		}

#if RIWO_HTTP_EXP_ZLIB_SUPPORT
		http::request compression_request(http::method::get,
			base + "/compress");
		compression_request.headers().add("Accept-Encoding", "gzip");
		compression_request.automatic_decompression(false);
		auto compressed_reply = co_await client.fetch(
			std::move(compression_request), riwo::use_awaitable);
		RIWO_TEST_CHECK_EQ(compressed_reply.headers().find_first(
			"content-encoding").value(), "gzip");
		size_t vary_count = 0;
		bool varies_on_encoding = false;
		for( const auto &vary : compressed_reply.headers().find_all("vary") )
		{
			++vary_count;
			if( vary.value.to_string() == "Accept-Encoding" )
				varies_on_encoding = true;
		}
		RIWO_TEST_CHECK_EQ(vary_count, 2U);
		RIWO_TEST_CHECK(varies_on_encoding);
		auto decoded = http::gzip_decompress(body_text(compressed_reply));
		RIWO_TEST_CHECK(decoded.has_value());
		RIWO_TEST_CHECK_EQ(decoded->size(), 2048U);
#endif

		http::request partial_request(http::method::get, base + "/asset");
		partial_request.headers().add("Range", "bytes=2-5");
		auto partial = co_await client.fetch(std::move(partial_request),
			riwo::use_awaitable);
		RIWO_TEST_CHECK_EQ(partial.status(), http::status::partial_content);
		RIWO_TEST_CHECK_EQ(body_text(partial), "2345");

		http::request conditional(http::method::get, base + "/asset");
		conditional.headers().add("If-None-Match",
			file.headers().find_first("etag").value());
		auto unchanged = co_await client.fetch(std::move(conditional),
			riwo::use_awaitable);
		RIWO_TEST_CHECK_EQ(unchanged.status(), http::status::not_modified);
		RIWO_TEST_CHECK(body_text(unchanged).empty());

		http::request no_redirect(http::method::get, base + "/redirect");
		no_redirect.redirects({.max_redirects = 0});
		auto found = co_await client.fetch(std::move(no_redirect),
			riwo::use_awaitable);
		RIWO_TEST_CHECK_EQ(found.status(), http::status::found);

		auto stopped = co_await client.fetch(http::request(http::method::get,
			base + "/short"), riwo::use_awaitable);
		RIWO_TEST_CHECK_EQ(stopped.status(), http::status::accepted);
		RIWO_TEST_CHECK_EQ(body_text(stopped), "stopped");
		RIWO_TEST_CHECK(!controller_called);

		auto recovered = co_await client.fetch(http::request(http::method::get,
			base + "/exception"), riwo::use_awaitable);
		RIWO_TEST_CHECK(exception_seen);
		RIWO_TEST_CHECK_EQ(recovered.status(), http::status::ok);

		auto missing = co_await client.fetch(http::request(http::method::get,
			base + "/missing"), riwo::use_awaitable);
		RIWO_TEST_CHECK_EQ(missing.status(), http::status::not_found);
		RIWO_TEST_CHECK(missing.headers().find_first("content-type")->starts_with(
			"text/html"));
		RIWO_TEST_CHECK(body_text(missing).find("Not Found") != std::string::npos);

		auto head = co_await client.fetch(http::request(http::method::head,
			base + "/asset"), riwo::use_awaitable);
		RIWO_TEST_CHECK_EQ(head.status(), http::status::ok);
		RIWO_TEST_CHECK(body_text(head).empty());
		server.stop();
	}, asio::use_future);

	riwo::ignore_unused(context.run_for(std::chrono::seconds(8)));
	RIWO_TEST_CHECK(completed.wait_for(std::chrono::milliseconds(0)) ==
		std::future_status::ready);
	completed.get();
}

void application_compatibility_http1()
{
	application_compatibility_for(http::protocol_requirement::require_http1,
		http::protocol::v1_1);
}

#if RIWO_HTTP_EXP_HTTP2_SUPPORT
void application_compatibility_http2()
{
	application_compatibility_for(http::protocol_requirement::require_http2,
		http::protocol::v2);
}
#endif

} //namespace

int main(int argc, const char *const argv[])
{
	return riwo::test::run(argc, argv, {
		{"semantic helpers", semantic_helpers},
		{"application compatibility over HTTP/1",
			application_compatibility_http1},
#if RIWO_HTTP_EXP_HTTP2_SUPPORT
		{"application compatibility over HTTP/2",
			application_compatibility_http2}
#endif
	});
}
