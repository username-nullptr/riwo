// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "client.h"
#include <riwo/http_exp/message/request_head.h>
#include <riwo/http_exp/protocol/detail/session.h>
#include <charconv>

namespace riwo::http_exp::detail
{

struct request_access
{
	[[nodiscard]] static std::shared_ptr<body_source>
	source(const request &value) noexcept {
		return value.source_;
	}

	[[nodiscard]] static fields trailers(const request &value) {
		return value.trailers_;
	}

	[[nodiscard]] static error_code prepare(request &value) noexcept
	{
		if( value.file_.empty() ) return {};
		try {
			file_body_source source(value.file_);
			const auto size = source.size();
			value.source_ = make_body_source(std::move(source));
			value.body_spec_ = body_spec::known(
				*size, value.body_spec_.trailers());
			value.file_.clear();
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

} //namespace

client_state::client_state(std::shared_ptr<client_session> session) :
	session_(std::move(session))
{
}

client_state::~client_state() = default;

std::shared_ptr<stream> client_state::open(request request_value,
	body_spec spec, error_code &error) noexcept
{
	error.clear();
	if( !session_ )
	{
		error = make_error_code(errc::operation_not_supported);
		return {};
	}
	auto head = freeze_request(request_value, spec, true);
	if( !head )
	{
		error = head.error();
		return {};
	}
	return session_->open_sync(std::move(*head), std::move(spec), error);
}

void client_state::open(request request_value, body_spec spec,
	open_handler handler)
{
	if( !session_ )
	{
		std::move(handler)(make_error_code(errc::operation_not_supported),
			std::shared_ptr<stream>{});
		return;
	}
	auto head = freeze_request(request_value, spec, true);
	if( !head )
	{
		std::move(handler)(head.error(), std::shared_ptr<stream>{});
		return;
	}
	session_->open(std::move(*head), std::move(spec), std::move(handler));
}

send_result client_state::send(request request_value,
	error_code &error) noexcept
{
	error.clear();
	if( !session_ )
	{
		error = make_error_code(errc::operation_not_supported);
		return {};
	}
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
	auto source = request_access::source(request_value);
	auto trailers = request_access::trailers(request_value);
	auto active_stream = session_->open_sync(std::move(*head), spec, error);
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

	if( source )
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
	return {
		active_stream->selected_protocol(), std::move(response),
		std::move(body), std::move(active_stream)
	};
}

void client_state::send(request request_value, send_handler handler)
{
	if( !session_ )
	{
		std::move(handler)(make_error_code(errc::operation_not_supported),
			send_result{});
		return;
	}
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
	auto source = request_access::source(request_value);
	auto trailers = request_access::trailers(request_value);

	struct operation : std::enable_shared_from_this<operation>
	{
		std::shared_ptr<stream> active_stream;
		std::shared_ptr<body_source> source;
		std::shared_ptr<body_writer_state> writer;
		body_spec spec;
		fields trailers;
		send_handler handler;
		std::array<std::byte,8192> buffer;
		uint64_t total = 0;

		operation(std::shared_ptr<stream> stream_value,
			std::shared_ptr<body_source> source_value, body_spec description,
			fields trailer_values, send_handler completion) :
			active_stream(std::move(stream_value)), source(std::move(source_value)),
			writer(active_stream ? active_stream->outbound_body() : nullptr),
			spec(std::move(description)), trailers(std::move(trailer_values)),
			handler(std::move(completion))
		{
		}

		void start()
		{
			if( !active_stream || !writer )
			{
				fail(make_error_code(errc::operation_not_permitted));
				return;
			}
			if( !source )
			{
				wait_response();
				return;
			}
			read_next();
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
				auto result = send_result {
					self->active_stream->selected_protocol(), std::move(head),
					std::move(body), self->active_stream
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

	session_->open(std::move(*head), spec,
	[source = std::move(source), spec = std::move(spec),
	 trailers = std::move(trailers), handler = std::move(handler)]
	(error_code error, std::shared_ptr<stream> active_stream) mutable
	{
		if( error )
		{
			std::move(handler)(error, send_result{});
			return;
		}
		std::make_shared<operation>(std::move(active_stream),
			std::move(source), std::move(spec), std::move(trailers),
			std::move(handler))->start();
	});
}

std::shared_ptr<client_state> make_client_state(
	std::shared_ptr<client_session> session)
{
	return std::make_shared<client_state>(std::move(session));
}

} //namespace riwo::http_exp::detail
