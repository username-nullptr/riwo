// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "test.h"

#include <riwo/http_exp.h>
#include <riwo/http_exp/protocol/v2/detail/nghttp2_compat.h>
#include <array>
#include <map>
#include <thread>

namespace
{

namespace http = riwo::http_exp;
namespace nghttp2_api = riwo::http_exp::v2::detail::nghttp2_api;

struct peer_stream
{
	int32_t id = 0;
	std::string path;
	std::string request_body;
	bool request_trailer = false;
	std::string response_body = "hello-http2";
	size_t response_offset = 0;
	bool response_trailer_submitted = false;
};

struct peer_state
{
	enum class behavior { response, reset, reset_once, goaway, redirect_once };
	nghttp2_session *session = nullptr;
	std::map<int32_t,peer_stream> streams;
	bool completed = false;
	size_t expected_streams = 1;
	size_t completed_streams = 0;
	bool saw_request_trailer = false;
	std::string received_body;
	std::vector<std::string> received_bodies;
	std::vector<std::string> received_paths;
	std::string response_body = "hello-http2";
	size_t resets_issued = 0;
	size_t redirects_issued = 0;
	behavior action = behavior::response;
};

nghttp2_nv nv(std::string_view name, std::string_view value)
{
	return {
		reinterpret_cast<uint8_t *>(const_cast<char *>(name.data())),
		reinterpret_cast<uint8_t *>(const_cast<char *>(value.data())),
		name.size(), value.size(), NGHTTP2_NV_FLAG_NONE
	};
}

nghttp2_api::ssize_type_t response_data(nghttp2_session *session, int32_t,
	uint8_t *buffer, size_t length, uint32_t *flags,
	nghttp2_data_source *source, void *)
{
	auto &stream = *static_cast<peer_stream *>(source->ptr);
	const auto remaining = stream.response_body.size() - stream.response_offset;
	const auto size = std::min(length, remaining);
	if( size != 0 )
	{
		std::memcpy(buffer, stream.response_body.data() +
			stream.response_offset, size);
		stream.response_offset += size;
	}
	if( stream.response_offset == stream.response_body.size() )
	{
		*flags |= NGHTTP2_DATA_FLAG_EOF | NGHTTP2_DATA_FLAG_NO_END_STREAM;
		if( !stream.response_trailer_submitted )
		{
			auto trailer = nv("x-server-trailer", "done");
			if( nghttp2_submit_trailer(session, stream.id, &trailer, 1) != 0 )
				return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
			stream.response_trailer_submitted = true;
		}
	}
	return static_cast<nghttp2_api::ssize_type_t>(size);
}

int on_begin_headers(nghttp2_session *, const nghttp2_frame *frame,
	void *user_data)
{
	if( frame->hd.type != NGHTTP2_HEADERS ) return 0;
	auto &state = *static_cast<peer_state *>(user_data);
	if( frame->headers.cat == NGHTTP2_HCAT_REQUEST )
	{
		auto [iterator, inserted] = state.streams.try_emplace(
			frame->hd.stream_id);
		riwo::ignore_unused(inserted);
		iterator->second.id = frame->hd.stream_id;
		iterator->second.response_body = state.response_body;
	}
	return 0;
}

int on_header(nghttp2_session *, const nghttp2_frame *frame,
	const uint8_t *name, size_t name_size, const uint8_t *value,
	size_t value_size, uint8_t, void *user_data)
{
	auto &state = *static_cast<peer_state *>(user_data);
	const std::string_view header_name(
		reinterpret_cast<const char *>(name), name_size);
	const std::string_view header_value(
		reinterpret_cast<const char *>(value), value_size);
	if( frame->headers.cat == NGHTTP2_HCAT_REQUEST &&
		header_name == ":path" )
		state.streams.at(frame->hd.stream_id).path = header_value;
	if( frame->headers.cat == NGHTTP2_HCAT_HEADERS &&
		header_name == "x-upload-trailer" && header_value == "present" )
		state.saw_request_trailer = true;
	return 0;
}

int on_data_chunk(nghttp2_session *, uint8_t, int32_t stream_id,
	const uint8_t *data, size_t size, void *user_data)
{
	auto &state = *static_cast<peer_state *>(user_data);
	state.streams.at(stream_id).request_body.append(
		reinterpret_cast<const char *>(data), size);
	return 0;
}

int on_frame_recv(nghttp2_session *session, const nghttp2_frame *frame,
	void *user_data)
{
	if( (frame->hd.type != NGHTTP2_HEADERS &&
		frame->hd.type != NGHTTP2_DATA) ||
		!(frame->hd.flags & NGHTTP2_FLAG_END_STREAM) ) return 0;
	auto &state = *static_cast<peer_state *>(user_data);
	auto found = state.streams.find(frame->hd.stream_id);
	if( found == state.streams.end() ) return 0;
	auto &stream = found->second;
	state.received_body = stream.request_body;
	state.received_bodies.push_back(stream.request_body);
	state.received_paths.push_back(stream.path);
	if( state.action == peer_state::behavior::reset )
		return nghttp2_submit_rst_stream(session, NGHTTP2_FLAG_NONE,
			stream.id, NGHTTP2_REFUSED_STREAM);
	if( state.action == peer_state::behavior::reset_once &&
		state.resets_issued == 0 )
	{
		++state.resets_issued;
		return nghttp2_submit_rst_stream(session, NGHTTP2_FLAG_NONE,
			stream.id, NGHTTP2_REFUSED_STREAM);
	}
	if( state.action == peer_state::behavior::goaway )
	{
		state.completed = true;
		return nghttp2_submit_goaway(session, NGHTTP2_FLAG_NONE, 0,
			NGHTTP2_NO_ERROR, nullptr, 0);
	}
	if( state.action == peer_state::behavior::redirect_once &&
		state.redirects_issued++ == 0 )
	{
		std::array headers {
			nv(":status", "307"),
			nv("location", "/final")
		};
		nghttp2_api::data_provider_t provider {};
		provider.source.ptr = &stream;
		provider.read_callback = response_data;
		return nghttp2_api::submit_response(session, stream.id, headers.data(),
			headers.size(), &provider);
	}
	std::array headers {
		nv(":status", "200"),
		nv("content-type", "text/plain"),
		nv("x-protocol", "h2")
	};
	nghttp2_api::data_provider_t provider {};
	provider.source.ptr = &stream;
	provider.read_callback = response_data;
	return nghttp2_api::submit_response(session, stream.id, headers.data(),
		headers.size(), &provider);
}

int on_stream_close(nghttp2_session *, int32_t, uint32_t error,
	void *user_data)
{
	auto &state = *static_cast<peer_state *>(user_data);
	if( state.action == peer_state::behavior::reset )
	{
		state.completed = true;
		return 0;
	}
	if( error == NGHTTP2_NO_ERROR ) ++state.completed_streams;
	state.completed = state.completed_streams == state.expected_streams;
	return 0;
}

void flush_peer(asio::ip::tcp::socket &socket, peer_state &state)
{
	for(;;)
	{
		const uint8_t *data = nullptr;
		const auto size = nghttp2_api::session_mem_send(state.session, &data);
		RIWO_TEST_CHECK(size >= 0);
		if( size == 0 ) return;
		riwo::error_code error;
		const auto written = asio::write(socket,
			asio::buffer(data, static_cast<size_t>(size)), error);
		RIWO_TEST_CHECK(!error);
		RIWO_TEST_CHECK_EQ(written, static_cast<size_t>(size));
	}
}

void run_peer(asio::ip::tcp::acceptor &acceptor, peer_state &state)
{
	asio::ip::tcp::socket socket(acceptor.get_executor());
	acceptor.accept(socket);
	nghttp2_session_callbacks *callbacks = nullptr;
	RIWO_TEST_CHECK_EQ(nghttp2_session_callbacks_new(&callbacks), 0);
	nghttp2_session_callbacks_set_on_begin_headers_callback(callbacks,
		on_begin_headers);
	nghttp2_session_callbacks_set_on_header_callback(callbacks, on_header);
	nghttp2_session_callbacks_set_on_data_chunk_recv_callback(callbacks,
		on_data_chunk);
	nghttp2_session_callbacks_set_on_frame_recv_callback(callbacks,
		on_frame_recv);
	nghttp2_session_callbacks_set_on_stream_close_callback(callbacks,
		on_stream_close);
	RIWO_TEST_CHECK_EQ(nghttp2_session_server_new(
		&state.session, callbacks, &state), 0);
	RIWO_TEST_CHECK_EQ(nghttp2_submit_settings(state.session,
		NGHTTP2_FLAG_NONE, nullptr, 0), 0);
	flush_peer(socket, state);

	std::array<uint8_t,16 * 1024> buffer {};
	while( !state.completed )
	{
		riwo::error_code error;
		const auto size = socket.read_some(asio::buffer(buffer), error);
		if( error == asio::error::eof ||
			error == asio::error::connection_reset )
			break;
		RIWO_TEST_CHECK(!error);
		RIWO_TEST_CHECK(size != 0);
		const auto consumed = nghttp2_api::session_mem_recv(state.session,
			buffer.data(), size);
		RIWO_TEST_CHECK_EQ(consumed,
			static_cast<nghttp2_api::ssize_type_t>(size));
		flush_peer(socket, state);
	}
	std::this_thread::sleep_for(std::chrono::milliseconds(20));
	riwo::error_code ignored;
	socket.shutdown(asio::ip::tcp::socket::shutdown_both, ignored);

	nghttp2_session_del(state.session);
	state.session = nullptr;
	nghttp2_session_callbacks_del(callbacks);
}

[[nodiscard]] std::string text(std::span<const std::byte> bytes)
{
	return {reinterpret_cast<const char *>(bytes.data()), bytes.size()};
}

void prior_knowledge_body_and_trailers()
{
	riwo::io_context_t context;
	asio::ip::tcp::acceptor acceptor(context,
		{asio::ip::address_v4::loopback(), 0});
	peer_state state;
	state.response_body.assign(192 * 1024, 'r');
	std::jthread server([&] { run_peer(acceptor, state); });

	http::client_config config;
	config.protocols.requirement = http::protocol_requirement::require_http2;
	config.pool.max_sessions_per_origin = 1;
	http::client client(context, config);
	const auto target = std::format("http://127.0.0.1:{}/upload",
		acceptor.local_endpoint().port());
	const std::string upload(128 * 1024, 'u');
	http::request request(http::method::post, target);
	request.body(asio::buffer(upload));
	http::fields request_trailers;
	request_trailers.add("x-upload-trailer", "present");
	request.trailers(std::move(request_trailers));

	auto reply = client.send(std::move(request));
	RIWO_TEST_CHECK_EQ(reply.selected_protocol(), http::protocol::v2);
	RIWO_TEST_CHECK_EQ(reply.http_version(), http::protocol::v2);
	RIWO_TEST_CHECK_EQ(reply.status(), http::status::ok);
	RIWO_TEST_CHECK_EQ(reply.headers().find_first("x-protocol"), "h2");
	RIWO_TEST_CHECK_EQ(text(reply.read_all()), state.response_body);
	auto response_trailers = reply.body().trailers();
	RIWO_TEST_CHECK_EQ(response_trailers.find_first("x-server-trailer"),
		"done");

	server.join();
	RIWO_TEST_CHECK(state.completed);
	RIWO_TEST_CHECK_EQ(state.received_body, upload);
	RIWO_TEST_CHECK(state.saw_request_trailer);
}

void multiplexed_async_requests()
{
	riwo::io_context_t context;
	asio::ip::tcp::acceptor acceptor(context,
		{asio::ip::address_v4::loopback(), 0});
	peer_state state;
	state.expected_streams = 2;
	std::jthread server([&] { run_peer(acceptor, state); });

	http::client_config config;
	config.protocols.requirement = http::protocol_requirement::require_http2;
	config.pool.max_sessions_per_origin = 1;
	http::client client(context, config);
	const auto base = std::format("http://127.0.0.1:{}",
		acceptor.local_endpoint().port());
	size_t completed = 0;
	std::array<riwo::error_code,2> errors {};
	std::array<std::string,2> bodies;
	for( size_t index = 0; index < 2; ++index )
	{
		client.fetch(http::request(http::method::get,
			base + "/stream/" + std::to_string(index)),
			[&, index](riwo::error_code error, http::reply reply) {
				errors[index] = error;
				if( !error ) bodies[index] = text(reply.buffered_body());
				++completed;
				if( completed == 2 ) context.stop();
			});
	}
	context.run();
	server.join();
	RIWO_TEST_CHECK_EQ(completed, 2U);
	RIWO_TEST_CHECK(!errors[0]);
	RIWO_TEST_CHECK(!errors[1]);
	RIWO_TEST_CHECK_EQ(bodies[0], "hello-http2");
	RIWO_TEST_CHECK_EQ(bodies[1], "hello-http2");
	RIWO_TEST_CHECK_EQ(state.completed_streams, 2U);
}

void reset_and_goaway_errors()
{
	for( const auto action : {peer_state::behavior::reset,
		peer_state::behavior::goaway} )
	{
		riwo::io_context_t context;
		asio::ip::tcp::acceptor acceptor(context,
			{asio::ip::address_v4::loopback(), 0});
		peer_state state;
		state.action = action;
		std::jthread server([&] { run_peer(acceptor, state); });
		http::client_config config;
		config.protocols.requirement =
			http::protocol_requirement::require_http2;
		config.retries.max_attempts = 1;
		http::client client(context, config);
		const auto target = std::format("http://127.0.0.1:{}/failure",
			acceptor.local_endpoint().port());
		riwo::error_code error;
		(void)client.get(target, error);
		server.join();
		RIWO_TEST_CHECK_EQ(error,
			http::make_error_code(http::errc::stream_protocol_error));
	}
}

void refused_stream_retries_async()
{
	riwo::io_context_t context;
	asio::ip::tcp::acceptor acceptor(context,
		{asio::ip::address_v4::loopback(), 0});
	peer_state state;
	state.action = peer_state::behavior::reset_once;
	std::jthread server([&] { run_peer(acceptor, state); });
	http::client_config config;
	config.protocols.requirement =
		http::protocol_requirement::require_http2;
	config.retries.max_attempts = 2;
	http::client client(context, config);
	const auto target = std::format("http://127.0.0.1:{}/retry",
		acceptor.local_endpoint().port());
	riwo::error_code result_error;
	std::string body;
	size_t completions = 0;
	client.fetch(http::request(http::method::get, target),
	[&](riwo::error_code error, http::reply reply) {
		result_error = error;
		if( !error ) body = text(reply.buffered_body());
		++completions;
		context.stop();
	});
	context.run();
	server.join();
	RIWO_TEST_CHECK(!result_error);
	RIWO_TEST_CHECK_EQ(completions, 1U);
	RIWO_TEST_CHECK_EQ(state.resets_issued, 1U);
	RIWO_TEST_CHECK_EQ(state.received_paths.size(), 2U);
	RIWO_TEST_CHECK_EQ(body, "hello-http2");
}

void redirect_rewinds_replayable_source()
{
	riwo::io_context_t context;
	asio::ip::tcp::acceptor acceptor(context,
		{asio::ip::address_v4::loopback(), 0});
	peer_state state;
	state.action = peer_state::behavior::redirect_once;
	state.expected_streams = 2;
	std::jthread server([&] { run_peer(acceptor, state); });
	http::client_config config;
	config.protocols.requirement =
		http::protocol_requirement::require_http2;
	http::client client(context, config);
	const auto target = std::format("http://127.0.0.1:{}/redirect",
		acceptor.local_endpoint().port());
	const std::string payload = "replayable-upload";
	std::vector<std::byte> bytes(payload.size());
	std::memcpy(bytes.data(), payload.data(), payload.size());
	http::request request(http::method::post, target);
	request.source(http::memory_body_source(std::move(bytes)));
	riwo::error_code error;
	auto reply = client.fetch(std::move(request), error);
	server.join();
	RIWO_TEST_CHECK(!error);
	RIWO_TEST_CHECK_EQ(text(reply.buffered_body()), "hello-http2");
	RIWO_TEST_CHECK_EQ(state.received_bodies.size(), 2U);
	RIWO_TEST_CHECK_EQ(state.received_bodies[0], payload);
	RIWO_TEST_CHECK_EQ(state.received_bodies[1], payload);
	RIWO_TEST_CHECK_EQ(state.received_paths.size(), 2U);
	RIWO_TEST_CHECK_EQ(state.received_paths[0], "/redirect");
	RIWO_TEST_CHECK_EQ(state.received_paths[1], "/final");
}

} //namespace

int main(int argc, const char *const argv[])
{
	return riwo::test::run(argc, argv, {
		{"HTTP/2 prior knowledge body and trailers",
			prior_knowledge_body_and_trailers},
		{"HTTP/2 multiplexed async requests", multiplexed_async_requests},
		{"HTTP/2 reset and GOAWAY errors", reset_and_goaway_errors},
		{"HTTP/2 REFUSED_STREAM async retry", refused_stream_retries_async},
		{"HTTP/2 redirect rewinds replayable source",
			redirect_rewinds_replayable_source}
	});
}
