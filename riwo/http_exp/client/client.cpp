// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "client.h"
#include "detail/connector.h"
#include "detail/session_pool.h"
#include <riwo/http_exp/protocol/request_head.h>
#include <riwo/http_exp/protocol/detail/session.h>
#include <riwo/http_exp/protocol/detail/h1/session.h>
#include <riwo/http_exp/body/detail/decoder.h>
#include <charconv>

namespace riwo::http_exp::detail
{

struct request_access
{
	[[nodiscard]] static std::shared_ptr<body_source>
	source(const request &value) noexcept {
		return value.m_impl->source;
	}

	[[nodiscard]] static optional<const_buffer>
	body(const request &value) noexcept {
		return value.m_impl->body;
	}

	[[nodiscard]] static fields trailers(const request &value) {
		return value.m_impl->trailers;
	}

	[[nodiscard]] static error_code prepare(request &value) noexcept
	{
		if( value.m_impl->file.empty() ) return {};
		try {
			file_body_source source(value.m_impl->file);
			const auto size = source.size();
			value.m_impl->body.reset();
			value.m_impl->source = make_body_source(std::move(source));
			value.m_impl->body_spec = body_spec::known(
				*size, value.m_impl->body_spec.trailers());
			value.m_impl->file.clear();
			return {};
		}
		catch(...) {
			return exception_error(std::current_exception());
		}
	}
};

namespace
{

[[nodiscard]] bool ascii(std::string_view text) noexcept {
	return std::ranges::all_of(text, [](unsigned char value) {
		return value < 0x80;
	});
}

[[nodiscard]] std::string lower(std::string_view text)
{
	std::string result(text);
	std::ranges::transform(result, result.begin(), [](unsigned char value) {
		return static_cast<char>(std::tolower(value));
	});
	return result;
}

[[nodiscard]] bool owned_header(std::string_view name) noexcept
{
	static constexpr std::array<std::string_view,7> names {
		"host", "transfer-encoding", "trailer", "connection", "keep-alive",
		"proxy-connection", "upgrade"
	};
	return std::ranges::any_of(names, [name](std::string_view value) {
		return name.size() == value.size() &&
			std::ranges::equal(name, value, [](char lhs, char rhs) {
				return std::tolower(static_cast<unsigned char>(lhs)) ==
					std::tolower(static_cast<unsigned char>(rhs));
			});
	});
}

[[nodiscard]] sys_expected<request_head> freeze_request(
	const request &value, const body_spec &spec, bool require_unbound) noexcept
{
	try {
		if( require_unbound && value.has_body_source() )
			return sys_unexpected(make_error_code(errc::operation_not_permitted));

		const auto &url = value.url();
		const auto scheme = lower(url.protocol());
		if( !url.is_valid() || (scheme != "http" && scheme != "https") ||
			url.host().empty() || url.has_fragment() || !ascii(url.host()) )
			return sys_unexpected(make_error_code(errc::invalid_target));

		for( const auto &header : value.headers() )
		{
			if( owned_header(header.name) )
				return sys_unexpected(make_error_code(errc::invalid_field_name));
		}
		if( auto length = value.headers().find_first("content-length") )
		{
			if( !spec.trailers().names.empty() || !spec.length() )
				return sys_unexpected(make_error_code(errc::body_length_mismatch));
			uint64_t parsed = 0;
			auto result = std::from_chars(length->data(),
				length->data() + length->size(), parsed);
			if( result.ec != std::errc{} || result.ptr != length->data() + length->size() ||
				parsed != *spec.length() )
				return sys_unexpected(make_error_code(errc::body_length_mismatch));
		}

		std::string authority;
		const auto host = lower(url.host());
		if( host.find(':') != std::string::npos )
			authority = "[" + host + "]";
		else
			authority = host;
		const auto default_port = scheme == "http" ? 80 : 443;
		if( url.port() != 0 && url.port() != default_port )
			authority += ":" + std::to_string(url.port());

		auto target = std::string(url.encoded_path());
		if( target.empty() )
			target = "/";
		if( url.has_query() )
			target += "?" + url.encoded_query();

		return request_head {
			value.method(), url, scheme, std::move(authority),
			std::move(target), value.headers()
		};
	}
	catch(...) {
		return sys_unexpected(exception_error(std::current_exception()));
	}
}

[[nodiscard]] std::string decodable_encoding(const response_head &head)
{
	if( !gzip_available_v ) return {};
	std::string_view value;
	auto values = head.headers.find_all("Content-Encoding");
	for( auto iterator = values.begin(); iterator != values.end(); ++iterator )
		value = iterator->value;
	while( !value.empty() &&
		(value.front() == ' ' || value.front() == '\t') )
		value.remove_prefix(1);
	while( !value.empty() &&
		(value.back() == ' ' || value.back() == '\t') )
		value.remove_suffix(1);
	std::string lower(value);
	std::ranges::transform(lower, lower.begin(), [](unsigned char byte) {
		return static_cast<char>(std::tolower(byte));
	});
	if( lower == "gzip" || lower == "x-gzip" || lower == "deflate" )
		return lower == "x-gzip" ? "gzip" : lower;
	return {};
}

[[nodiscard]] std::shared_ptr<body_reader_state> decoded_body(
	std::shared_ptr<body_reader_state> body, response_head &head,
	bool automatic_decompression)
{
	if( !automatic_decompression || !body || body->is_finished() )
		return body;
	auto encoding = decodable_encoding(head);
	if( encoding.empty() ) return body;
	head.headers.erase("Content-Encoding");
	head.headers.erase("Content-Length");
	return make_decoding_body_reader(std::move(body), std::move(encoding));
}

void add_accept_encoding(request_head &head, const client_config &config)
{
	if( !config.automatic_decompression || !gzip_available_v ) return;
	if( head.headers.find_first("Accept-Encoding") ) return;
	head.headers.add("Accept-Encoding", "gzip, deflate");
}

} //namespace

class client_state::impl
{
public:
	impl(asio::any_io_executor executor_value, client_config config_value,
		std::shared_ptr<client_session> session_value) :
		executor(std::move(executor_value)), config(std::move(config_value)),
		injected_session(std::move(session_value))
	{
		if( !injected_session )
			pool = std::make_shared<session_pool>(executor, config);
	}

	[[nodiscard]] std::shared_ptr<stream> open_sync(const request_head &head,
		body_spec spec, error_code &error) noexcept
	{
		error.clear();
		try {
			if( injected_session )
				return injected_session->open_sync(head, std::move(spec), error);
			return pool->open_sync(make_session_key(head, config), head,
				std::move(spec), error);
		}
		catch(...) {
			error = exception_error(std::current_exception());
			return {};
		}
	}

	void open(const request_head &head, body_spec spec,
		client_session::open_handler handler)
	{
		if( injected_session )
		{
			injected_session->open(head, std::move(spec), std::move(handler));
			return;
		}
		pool->open(make_session_key(head, config), head, std::move(spec),
			std::move(handler));
	}

	asio::any_io_executor executor;
	client_config config;
	std::shared_ptr<client_session> injected_session;
	std::shared_ptr<session_pool> pool;
};

client_state::client_state(asio::any_io_executor executor, client_config config,
	std::shared_ptr<client_session> session) :
	m_impl(std::make_shared<impl>(std::move(executor), std::move(config),
		std::move(session)))
{
}

client_state::~client_state() = default;

std::shared_ptr<stream> client_state::open(request request_value,
	body_spec spec, error_code &error) noexcept
{
	error.clear();
	auto head = freeze_request(request_value, spec, true);
	if( !head )
	{
		error = head.error();
		return {};
	}
	return m_impl->open_sync(*head, std::move(spec), error);
}

void client_state::open(request request_value, body_spec spec,
	open_handler handler)
{
	auto head = freeze_request(request_value, spec, true);
	if( !head )
	{
		std::move(handler)(head.error(), std::shared_ptr<stream>{});
		return;
	}
	m_impl->open(*head, std::move(spec), std::move(handler));
}

send_result client_state::send(request request_value,
	error_code &error) noexcept
{
	error.clear();
	if( auto prepare_error = request_access::prepare(request_value) )
	{
		error = prepare_error;
		return {};
	}
	auto spec = request_value.body_description();
	auto head = freeze_request(request_value, spec, false);
	if( !head )
	{
		error = head.error();
		return {};
	}
	add_accept_encoding(*head, m_impl->config);
	auto request_body = request_access::body(request_value);
	auto source = request_access::source(request_value);
	auto trailers = request_access::trailers(request_value);
	auto active_stream = m_impl->open_sync(*head, spec, error);
	if( error || !active_stream )
	{
		if( !error ) error = make_error_code(errc::operation_not_supported);
		return {};
	}
	auto writer = active_stream->outbound_body();
	if( !writer )
	{
		error = make_error_code(errc::operation_not_permitted);
		active_stream->cancel();
		return {};
	}

	if( request_body )
	{
		auto written = writer->write(*request_body, error);
		if( !error && written != request_body->size() )
			error = make_error_code(errc::body_length_mismatch);
		if( !error ) writer->finish(std::move(trailers), error);
		if( error )
		{
			active_stream->cancel();
			return {};
		}
	}
	else if( source )
	{
		std::array<std::byte,8192> buffer;
		uint64_t total = 0;
		for(;;)
		{
			if( auto length = spec.length(); length && total == *length )
				break;
			auto target = riwo::buffer(buffer);
			if( auto length = spec.length() )
				target = mutable_buffer(target.data(), static_cast<size_t>(
					std::min<uint64_t>(target.size(), *length - total)));
			auto size = source->read_some_sync(target, error);
			if( error == asio::error::make_error_code(asio::error::eof) )
			{
				if( spec.type() == body_spec::mode::streaming )
					error.clear();
				else
					error = make_error_code(errc::body_length_mismatch);
				break;
			}
			if( error ) break;
			if( size == 0 )
			{
				error = make_error_code(errc::body_length_mismatch);
				break;
			}
			auto written = writer->write(const_buffer(target.data(), size), error);
			total += written;
			if( error ) break;
		}
		if( !error ) writer->finish(std::move(trailers), error);
		if( error )
		{
			source->cancel();
			active_stream->cancel();
			return {};
		}
	}

	auto response = active_stream->wait_response_sync(error);
	if( error )
	{
		active_stream->cancel();
		return {};
	}
	auto body = active_stream->inbound_body();
	if( !body )
	{
		error = make_error_code(errc::operation_not_permitted);
		active_stream->cancel();
		return {};
	}
	body->set_ready();
	body = decoded_body(std::move(body), response,
		m_impl->config.automatic_decompression);
	return send_result {
		.selected_protocol = active_stream->selected_protocol(),
		.response_protocol = active_stream->response_protocol(),
		.head = std::move(response),
		.body = std::move(body),
		.owner = std::move(active_stream),
		.max_buffered_body = m_impl->config.response_limits.max_buffered_body
	};
}

void client_state::send(request request_value, send_handler handler)
{
	if( auto prepare_error = request_access::prepare(request_value) )
	{
		std::move(handler)(prepare_error, send_result{});
		return;
	}
	auto spec = request_value.body_description();
	auto head = freeze_request(request_value, spec, false);
	if( !head )
	{
		std::move(handler)(head.error(), send_result{});
		return;
	}
	add_accept_encoding(*head, m_impl->config);
	auto request_body = request_access::body(request_value);
	auto source = request_access::source(request_value);
	auto trailers = request_access::trailers(request_value);
	struct operation : std::enable_shared_from_this<operation>
	{
		std::shared_ptr<stream> active_stream;
		optional<const_buffer> body;
		std::shared_ptr<body_source> source;
		std::shared_ptr<body_writer_state> writer;
		body_spec spec;
		fields trailers;
		send_handler handler;
		std::array<std::byte,8192> buffer;
		uint64_t total = 0;
			uint64_t max_buffered_body = 8U * 1024U * 1024U;
			bool automatic_decompression = true;

		operation(std::shared_ptr<stream> stream_value,
			optional<const_buffer> body_value,
			std::shared_ptr<body_source> source_value, body_spec description,
				fields trailer_values, uint64_t max_buffered_body_value,
				bool automatic_decompression_value, send_handler completion) :
			active_stream(std::move(stream_value)), body(body_value),
			source(std::move(source_value)),
			writer(active_stream ? active_stream->outbound_body() : nullptr),
			spec(std::move(description)), trailers(std::move(trailer_values)),
				handler(std::move(completion)),
				max_buffered_body(max_buffered_body_value),
				automatic_decompression(automatic_decompression_value)
		{
		}

		void start()
		{
			if( !active_stream || !writer )
			{
				fail(make_error_code(errc::operation_not_permitted));
				return;
			}
			if( body )
			{
				write_body();
				return;
			}
			if( !source )
			{
				wait_response();
				return;
			}
			read_next();
		}

		void write_body()
		{
			auto self = shared_from_this();
			writer->write(*body,
			[self = std::move(self)](error_code error, size_t written) mutable {
				if( error ) self->fail(error);
				else if( written != self->body->size() )
					self->fail(make_error_code(errc::body_length_mismatch));
				else self->finish_request();
			});
		}

		void read_next()
		{
			if( auto length = spec.length(); length && total == *length )
			{
				finish_request();
				return;
			}
			auto target = riwo::buffer(buffer);
			if( auto length = spec.length() )
				target = mutable_buffer(target.data(), static_cast<size_t>(
					std::min<uint64_t>(target.size(), *length - total)));
			auto self = shared_from_this();
			source->read_some(target,
			[self = std::move(self), target]
			(error_code error, size_t size) mutable {
				self->read_complete(target, error, size);
			});
		}

		void read_complete(mutable_buffer target, error_code error, size_t size)
		{
			if( error == asio::error::make_error_code(asio::error::eof) )
			{
				if( spec.type() == body_spec::mode::streaming )
					finish_request();
				else
					fail(make_error_code(errc::body_length_mismatch));
				return;
			}
			if( error )
			{
				fail(error);
				return;
			}
			if( size == 0 )
			{
				fail(make_error_code(errc::body_length_mismatch));
				return;
			}
			auto self = shared_from_this();
			writer->write(const_buffer(target.data(), size),
			[self = std::move(self)](error_code write_error, size_t written) mutable {
				self->total += written;
				if( write_error ) self->fail(write_error);
				else self->read_next();
			});
		}

		void finish_request()
		{
			auto self = shared_from_this();
			writer->finish(std::move(trailers),
			[self = std::move(self)](error_code error) mutable {
				if( error ) self->fail(error);
				else self->wait_response();
			});
		}

		void wait_response()
		{
			auto self = shared_from_this();
			active_stream->wait_response(
			[self = std::move(self)](error_code error, response_head head) mutable {
				if( error )
				{
					self->fail(error);
					return;
				}
				auto body = self->active_stream->inbound_body();
				if( !body )
				{
					self->fail(make_error_code(errc::operation_not_permitted));
					return;
				}
				body->set_ready();
				body = decoded_body(std::move(body), head,
					self->automatic_decompression);
				auto result = send_result {
					.selected_protocol =
						self->active_stream->selected_protocol(),
					.response_protocol =
						self->active_stream->response_protocol(),
					.head = std::move(head),
					.body = std::move(body),
					.owner = self->active_stream,
					.max_buffered_body = self->max_buffered_body
				};
				std::move(self->handler)(error_code{}, std::move(result));
			});
		}

		void fail(error_code error)
		{
			if( source ) source->cancel();
			if( active_stream ) active_stream->cancel();
			std::move(handler)(error, send_result{});
		}
	};

	m_impl->open(*head, spec,
	[request_body, source = std::move(source), spec = std::move(spec),
	 trailers = std::move(trailers),
	 max_buffered_body = m_impl->config.response_limits.max_buffered_body,
	 automatic_decompression = m_impl->config.automatic_decompression,
	 handler = std::move(handler)]
	(error_code error, std::shared_ptr<stream> active_stream) mutable
	{
		if( error )
		{
			std::move(handler)(error, send_result{});
			return;
		}
		std::make_shared<operation>(std::move(active_stream), request_body,
			std::move(source), std::move(spec), std::move(trailers),
			max_buffered_body, automatic_decompression,
			std::move(handler))->start();
	});
}

std::shared_ptr<client_state> make_client_state(
	asio::any_io_executor executor, client_config config,
	std::shared_ptr<client_session> session)
{
	return std::make_shared<client_state>(std::move(executor),
		std::move(config), std::move(session));
}

} //namespace riwo::http_exp::detail
