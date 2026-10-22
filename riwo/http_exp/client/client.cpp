// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "client.h"
#include "detail/connector.h"
#include "detail/session_pool.h"
#include <riwo/http_exp/protocol/request_head.h>
#include <riwo/http_exp/protocol/detail/session.h>
#include <riwo/http_exp/protocol/v1/detail/session.h>
#include <riwo/http_exp/body/detail/decoder.h>
#include <riwo/core/utils/string_tools.h>
#include <charconv>

namespace riwo::http_exp::detail
{

struct request_access
{
	[[nodiscard]] static std::shared_ptr<body_source>
	source(const request &value) noexcept {
		return value.m_impl->m_source;
	}

	[[nodiscard]] static optional<const_buffer>
	body(const request &value) noexcept {
		return value.m_impl->m_body;
	}

	[[nodiscard]] static fields trailers(const request &value) {
		return value.m_impl->m_trailers;
	}

	[[nodiscard]] static size_t retry_attempt(const request &value) noexcept {
		return value.m_impl->m_retry_attempt;
	}

	[[nodiscard]] static size_t redirect_count(const request &value) noexcept {
		return value.m_impl->m_redirect_count;
	}

	[[nodiscard]] static request clone(const request &value,
		optional<riwo::url> target = {}, optional<method> request_method = {},
		bool drop_body = false, bool strip_credentials = false)
	{
		request result(request_method ? *request_method : value.method(),
			target ? std::move(*target) : value.url());
		result.m_impl->m_head.headers = value.m_impl->m_head.headers;
		result.m_impl->m_head.connect_protocol =
			value.m_impl->m_head.connect_protocol;
		result.m_impl->m_head.upgrade_protocol =
			value.m_impl->m_head.upgrade_protocol;
		if( strip_credentials )
		{
			result.m_impl->m_head.headers.erase("Authorization");
			result.m_impl->m_head.headers.erase("Proxy-Authorization");
			result.m_impl->m_head.headers.erase("Cookie");
		}
		result.m_impl->m_retry_attempt = value.m_impl->m_retry_attempt;
		result.m_impl->m_redirect_count = value.m_impl->m_redirect_count;
		if( not drop_body )
		{
			result.m_impl->m_body_spec = value.m_impl->m_body_spec;
			result.m_impl->m_body = value.m_impl->m_body;
			result.m_impl->m_source = value.m_impl->m_source;
			result.m_impl->m_trailers = value.m_impl->m_trailers;
		}
		return result;
	}

	static void increment_retry(request &value) noexcept {
		++value.m_impl->m_retry_attempt;
	}

	static void increment_redirect(request &value) noexcept {
		++value.m_impl->m_redirect_count;
	}

	[[nodiscard]] static error_code prepare(request &value) noexcept
	{
		if( value.m_impl->m_file.empty() ) return {};
		try {
			file_body_source source(value.m_impl->m_file);
			const auto size = source.size();
			value.m_impl->m_body.reset();
			value.m_impl->m_source = make_body_source(std::move(source));
			value.m_impl->m_body_spec = body_spec::known(
				*size, value.m_impl->m_body_spec.trailers());
			value.m_impl->m_file.clear();
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
	return strtls::is_ascii(text);
}

[[nodiscard]] std::string lower(std::string_view text)
{
	return strtls::to_lower(text);
}

[[nodiscard]] bool owned_header(std::string_view name) noexcept
{
	static constexpr std::array<std::string_view,7> names {
		"host", "transfer-encoding", "trailer", "connection", "keep-alive",
		"proxy-connection", "upgrade"
	};
	return std::ranges::any_of(names, [name](std::string_view value) {
		return strtls::ascii_equal(name, value, false);
	});
}

[[nodiscard]] sys_expected<request_head> freeze_request(
	const request &value, const body_spec &spec, bool require_unbound) noexcept
{
	try {
		if( require_unbound and value.has_body_source() )
			return sys_unexpected(make_error_code(errc::operation_not_permitted));

		const auto &url = value.url();
		const auto scheme = lower(url.protocol());
		if( not url.is_valid() or (scheme != "http" and scheme != "https") or
			url.host().empty() or url.has_fragment() or not ascii(url.host()) )
			return sys_unexpected(make_error_code(errc::invalid_target));

		for( const auto &header : value.headers() )
		{
			if( owned_header(header.name) )
				return sys_unexpected(make_error_code(errc::invalid_field_name));
		}
		if( auto length = value.headers().find_first("content-length") )
		{
			if( not spec.trailers().names.empty() or not spec.length() )
				return sys_unexpected(make_error_code(errc::body_length_mismatch));
			uint64_t parsed = 0;
			auto result = std::from_chars(length->data(),
				length->data() + length->size(), parsed);
			if( result.ec != std::errc{} or result.ptr != length->data() + length->size() or
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
		if( url.port() != 0 and url.port() != default_port )
			authority += ":" + std::to_string(url.port());

		auto target = std::string(url.encoded_path());
		if( target.empty() )
			target = "/";
		if( url.has_query() )
			target += "?" + url.encoded_query();

		auto result = request_head {
			value.method(), url, scheme, std::move(authority),
			std::move(target), value.headers(), value.connect_protocol()
		};
		result.upgrade_protocol = value.upgrade_protocol();
		return result;
	}
	catch(...) {
		return sys_unexpected(exception_error(std::current_exception()));
	}
}

[[nodiscard]] std::string decodable_encoding(const response_head &head)
{
	if( not gzip_available_v ) return {};
	std::string_view value;
	auto values = head.headers.find_all("Content-Encoding");
	for( auto iterator = values.begin(); iterator != values.end(); ++iterator )
		value = iterator->value.to_string();
	while( not value.empty() and
		(value.front() == ' ' or value.front() == '\t') )
		value.remove_prefix(1);
	while( not value.empty() and
		(value.back() == ' ' or value.back() == '\t') )
		value.remove_suffix(1);
	auto lower = strtls::to_lower(value);
	if( lower == "gzip" or lower == "x-gzip" or lower == "deflate" )
		return lower == "x-gzip" ? "gzip" : lower;
	return {};
}

[[nodiscard]] std::shared_ptr<body_reader_state> decoded_body(
	std::shared_ptr<body_reader_state> body, response_head &head,
	bool automatic_decompression)
{
	if( not automatic_decompression or not body or body->is_finished() )
		return body;
	auto encoding = decodable_encoding(head);
	if( encoding.empty() ) return body;
	head.headers.erase("Content-Encoding");
	head.headers.erase("Content-Length");
	return make_decoding_body_reader(std::move(body), std::move(encoding));
}

void add_accept_encoding(request_head &head, const client_config &config)
{
	if( not config.automatic_decompression or not gzip_available_v ) return;
	if( head.headers.find_first("Accept-Encoding") ) return;
	head.headers.add("Accept-Encoding", "gzip, deflate");
}

[[nodiscard]] error_code add_request_cookies(request_head &head,
	const std::shared_ptr<cookie_jar> &store) noexcept
{
	if( not store or head.headers.find_first("Cookie") ) return {};
	try
	{
		cookie_request_context context;
		context.request_method = head.method;
		auto values = store->select(head.url, context);
		if( not values.empty() )
			head.headers.add("Cookie", serialize_cookie(values));
		return {};
	}
	catch(...) {
		return exception_error(std::current_exception());
	}
}

[[nodiscard]] uint16_t effective_port(const riwo::url &value) noexcept
{
	if( value.port() != 0 ) return value.port();
	if( value.protocol() == "http" ) return 80;
	if( value.protocol() == "https" ) return 443;
	return 0;
}

[[nodiscard]] bool same_origin(const riwo::url &lhs,
	const riwo::url &rhs) noexcept
{
	try {
		return lower(lhs.protocol()) == lower(rhs.protocol()) and
			lower(lhs.host()) == lower(rhs.host()) and
			effective_port(lhs) == effective_port(rhs);
	}
	catch(...) {
		return false;
	}
}

[[nodiscard]] bool retry_allowed(const request &value,
	const retry_policy &policy, request_delivery delivery) noexcept
{
	if( delivery == request_delivery::possibly_processed or
		request_access::retry_attempt(value) >= policy.max_attempts )
		return false;
	if( value.method().is_idempotent() )
	{
		if( not policy.retry_idempotent ) return false;
	}
	else if( not policy.retry_non_idempotent ) return false;
	const auto source = request_access::source(value);
	return not source or source->replayable();
}

[[nodiscard]] error_code rewind_sync(
	const std::shared_ptr<body_source> &source) noexcept
{
	if( not source ) return {};
	try {
		struct state
		{
			std::mutex mutex;
			bool completed = false;
			error_code error;
		};
		auto result = std::make_shared<state>();
		source->rewind([result](error_code error) {
			const std::lock_guard lock(result->mutex);
			result->error = error;
			result->completed = true;
		});
		const std::lock_guard lock(result->mutex);
		return result->completed ? result->error :
			make_error_code(errc::operation_not_supported);
	}
	catch(...) {
		return exception_error(std::current_exception());
	}
}

[[nodiscard]] std::unique_ptr<request> make_redirect_request(
	const request &value, const response_head &response,
	const redirect_policy &policy, error_code &error) noexcept
{
	error.clear();
	const auto code = response.status.code();
	if( code != 301 and code != 302 and code != 303 and
		code != 307 and code != 308 )
		return {};
	if( request_access::redirect_count(value) >= policy.max_redirects )
		return {};
	const auto location = response.headers.find_first("Location");
	if( not location ) return {};
	try {
		auto target = riwo::url::resolve(value.url(), *location);
		if( not target.is_valid() or target.host().empty() )
		{
			error = make_error_code(errc::invalid_target);
			return {};
		}
		const auto from_scheme = lower(value.url().protocol());
		const auto to_scheme = lower(target.protocol());
		if( to_scheme != "http" and to_scheme != "https" )
		{
			error = make_error_code(errc::invalid_target);
			return {};
		}
		if( from_scheme == "https" and to_scheme == "http" and
			not policy.allow_https_to_http )
			return {};

		auto next_method = value.method();
		bool drop_body = false;
		if( code == 303 and value.method().known() != method::head )
		{
			next_method = method::get;
			drop_body = true;
		}
		else if( (code == 301 or code == 302) and
			value.method().known() == method::post )
		{
			next_method = method::get;
			drop_body = true;
		}
		const auto source = request_access::source(value);
		if( not drop_body and source and not source->replayable() ) return {};
		const auto strip_credentials = not same_origin(value.url(), target);
		auto next = std::make_unique<request>(request_access::clone(value,
			std::move(target), std::move(next_method), drop_body,
			strip_credentials));
		request_access::increment_redirect(*next);
		return next;
	}
	catch(...) {
		error = exception_error(std::current_exception());
		return {};
	}
}

} //namespace

class client_state::impl
{
public:
	impl(asio::any_io_executor executor_value, client_config config_value,
		std::shared_ptr<client_session> session_value,
		connector_ptr_t connector_value) :
		m_executor(std::move(executor_value)), m_config(std::move(config_value)),
		m_injected_session(std::move(session_value))
	{
		if( not m_injected_session )
			m_pool = std::make_shared<session_pool>(m_executor, m_config,
				std::move(connector_value));
	}

	[[nodiscard]] std::shared_ptr<stream> open_sync(const request_head &head,
		body_spec spec, error_code &error) noexcept
	{
		error.clear();
		try {
			if( m_injected_session )
				return m_injected_session->open_sync(head, std::move(spec), error);
			return m_pool->open_sync(make_session_key(head, m_config), head,
				std::move(spec), error);
		}
		catch(...) {
			error = exception_error(std::current_exception());
			return {};
		}
	}

	void open(const request_head &head, body_spec spec,
		client_session::open_handler_t handler)
	{
		if( m_injected_session )
		{
			m_injected_session->open(head, std::move(spec), std::move(handler));
			return;
		}
		m_pool->open(make_session_key(head, m_config), head, std::move(spec),
			std::move(handler));
	}

	asio::any_io_executor m_executor;
	client_config m_config;
	std::shared_ptr<client_session> m_injected_session;
	std::shared_ptr<session_pool> m_pool;
};

client_state::client_state(asio::any_io_executor executor, client_config config,
	std::shared_ptr<client_session> session, connector_ptr_t connector) :
	m_impl(std::make_shared<impl>(std::move(executor), std::move(config),
		std::move(session), std::move(connector)))
{
}

client_state::client_state(client_config config,
	std::shared_ptr<client_session> session, connector_ptr_t connector) :
	client_state(asio::any_io_executor(riwo::get_executor()),
		std::move(config), std::move(session), std::move(connector))
{
}

client_state::~client_state() = default;

std::shared_ptr<stream> client_state::open(request request_value,
	body_spec spec, error_code &error) noexcept
{
	error.clear();
	auto head = freeze_request(request_value, spec, true);
	if( not head )
	{
		error = head.error();
		return {};
	}
	if( auto cookie_error = add_request_cookies(*head, cookie_store()) )
	{
		error = cookie_error;
		return {};
	}
	return m_impl->open_sync(*head, std::move(spec), error);
}

void client_state::open(request request_value, body_spec spec,
	open_handler_t handler)
{
	auto head = freeze_request(request_value, spec, true);
	if( not head )
	{
		std::move(handler)(head.error(), std::shared_ptr<stream>{});
		return;
	}
	if( auto cookie_error = add_request_cookies(*head, cookie_store()) )
	{
		std::move(handler)(cookie_error, std::shared_ptr<stream>{});
		return;
	}
	m_impl->open(*head, std::move(spec), std::move(handler));
}

std::shared_ptr<cookie_jar> client_state::cookie_store() const noexcept {
	return m_impl->m_config.cookies.jar;
}

void client_state::capture_cookies(const riwo::url &origin,
	const response_head &response) noexcept
{
	const auto store = cookie_store();
	if( not store ) return;
	try {
		store->store(origin, response.headers);
	}
	catch(...) {
	}
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
	if( not head )
	{
		error = head.error();
		return {};
	}
	if( auto cookie_error = add_request_cookies(*head, cookie_store()) )
	{
		error = cookie_error;
		return {};
	}
	add_accept_encoding(*head, m_impl->m_config);
	auto request_body = request_access::body(request_value);
	auto source = request_access::source(request_value);
	auto trailers = request_access::trailers(request_value);
	auto retry_request = [&](request_delivery delivery, bool rewind_body,
		send_result &result) -> bool
	{
		if( not retry_allowed(request_value, m_impl->m_config.retries, delivery) )
			return false;
		try
		{
			auto next = request_access::clone(request_value);
			request_access::increment_retry(next);
			if( rewind_body )
			{
				error = rewind_sync(source);
				if( error ) return true;
			}
			result = send(std::move(next), error);
		}
		catch(...) {
			error = exception_error(std::current_exception());
		}
		return true;
	};
	auto active_stream = m_impl->open_sync(*head, spec, error);
	if( error or not active_stream )
	{
		if( not error ) error = make_error_code(errc::operation_not_supported);
		send_result retried;
		if( retry_request(request_delivery::not_started, false, retried) )
			return retried;
		return {};
	}
	auto writer = active_stream->outbound_body();
	if( not writer )
	{
		error = make_error_code(errc::operation_not_permitted);
		active_stream->cancel();
		return {};
	}

	if( request_body )
	{
		auto written = writer->write(*request_body, error);
		if( not error and written != request_body->size() )
			error = make_error_code(errc::body_length_mismatch);
		if( not error ) writer->finish(std::move(trailers), error);
		if( error )
		{
			const auto delivery = active_stream->delivery();
			active_stream->cancel();
			send_result retried;
			if( retry_request(delivery, false, retried) ) return retried;
			return {};
		}
	}
	else if( source )
	{
		std::array<std::byte,8192> buffer;
		uint64_t total = 0;
		for(;;)
		{
			if( auto length = spec.length(); length and total == *length )
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
		if( not error ) source->finish_sync(error);
		if( not error ) writer->finish(std::move(trailers), error);
		if( error )
		{
			const auto delivery = active_stream->delivery();
			source->cancel();
			active_stream->cancel();
			send_result retried;
			if( retry_request(delivery, true, retried) ) return retried;
			return {};
		}
	}

	auto response = active_stream->wait_response_sync(error);
	if( error )
	{
		const auto delivery = active_stream->delivery();
		if( source ) source->cancel();
		active_stream->cancel();
		send_result retried;
		if( retry_request(delivery, source != nullptr, retried) )
			return retried;
		return {};
	}
	capture_cookies(request_value.url(), response);
	auto body = active_stream->inbound_body();
	if( not body )
	{
		error = make_error_code(errc::operation_not_permitted);
		active_stream->cancel();
		return {};
	}
	error_code redirect_error;
	auto redirect = make_redirect_request(request_value, response,
		m_impl->m_config.redirects, redirect_error);
	if( redirect_error )
	{
		error = redirect_error;
		active_stream->cancel();
		return {};
	}
	if( redirect )
	{
		body->set_ready();
		body->discard(error);
		if( error )
		{
			active_stream->cancel();
			return {};
		}
		if( auto redirect_source = request_access::source(*redirect) )
		{
			redirect_source->cancel();
			error = rewind_sync(redirect_source);
			if( error ) return {};
		}
		return send(std::move(*redirect), error);
	}
	body->set_ready();
	body = decoded_body(std::move(body), response,
		m_impl->m_config.automatic_decompression);
	return send_result {
		.selected_protocol = active_stream->selected_protocol(),
		.response_protocol = active_stream->response_protocol(),
		.head = std::move(response),
		.body = std::move(body),
		.owner = std::move(active_stream),
		.max_buffered_body = m_impl->m_config.response_limits.max_buffered_body
	};
}

void client_state::send(request request_value, send_handler_t handler)
{
	if( auto prepare_error = request_access::prepare(request_value) )
	{
		std::move(handler)(prepare_error, send_result{});
		return;
	}
	auto spec = request_value.body_description();
	auto head = freeze_request(request_value, spec, false);
	if( not head )
	{
		std::move(handler)(head.error(), send_result{});
		return;
	}
	if( auto cookie_error = add_request_cookies(*head, cookie_store()) )
	{
		std::move(handler)(cookie_error, send_result{});
		return;
	}
	add_accept_encoding(*head, m_impl->m_config);
	auto request_body = request_access::body(request_value);
	auto source = request_access::source(request_value);
	auto trailers = request_access::trailers(request_value);
	struct operation : std::enable_shared_from_this<operation>
	{
		std::shared_ptr<client_state> owner;
		request request_value;
		std::shared_ptr<stream> active_stream;
		optional<const_buffer> body;
		std::shared_ptr<body_source> source;
		std::shared_ptr<body_writer_state> writer;
		body_spec spec;
		fields trailers;
		send_handler_t handler;
		std::array<std::byte,8192> buffer;
		uint64_t total = 0;
		uint64_t max_buffered_body = 8U * 1024U * 1024U;
		bool automatic_decompression = true;
		bool source_finished = false;
		retry_policy retries;
		redirect_policy redirects;

		operation(std::shared_ptr<client_state> owner_value,
			request original_request, std::shared_ptr<stream> stream_value,
			optional<const_buffer> body_value,
			std::shared_ptr<body_source> source_value, body_spec description,
			fields trailer_values, uint64_t max_buffered_body_value,
			bool automatic_decompression_value, retry_policy retry_values,
			redirect_policy redirect_values, send_handler_t completion) :
			owner(std::move(owner_value)),
			request_value(std::move(original_request)),
			active_stream(std::move(stream_value)), body(body_value),
			source(std::move(source_value)),
			writer(active_stream ? active_stream->outbound_body() : nullptr),
			spec(std::move(description)), trailers(std::move(trailer_values)),
			handler(std::move(completion)),
			max_buffered_body(max_buffered_body_value),
			automatic_decompression(automatic_decompression_value),
			retries(std::move(retry_values)),
			redirects(std::move(redirect_values))
		{
		}

		void start()
		{
			if( not active_stream or not writer )
			{
				fail(make_error_code(errc::operation_not_permitted));
				return;
			}
			if( body )
			{
				write_body();
				return;
			}
			if( not source )
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
			if( auto length = spec.length(); length and total == *length )
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
			if( source and not source_finished )
			{
				auto self = shared_from_this();
				source->finish(
				[self = std::move(self)](error_code error) mutable {
					if( error )
					{
						self->fail(error);
						return;
					}
					self->source_finished = true;
					self->finish_request();
				});
				return;
			}
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
				self->owner->capture_cookies(self->request_value.url(), head);
				auto body = self->active_stream->inbound_body();
				if( not body )
				{
					self->fail(make_error_code(errc::operation_not_permitted));
					return;
				}
				error_code redirect_error;
				auto redirect = make_redirect_request(self->request_value, head,
					self->redirects, redirect_error);
				if( redirect_error )
				{
					self->fail(redirect_error);
					return;
				}
				if( redirect )
				{
					body->set_ready();
					self->follow_redirect(std::move(*redirect), std::move(body));
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

		void follow_redirect(request next,
			std::shared_ptr<body_reader_state> response_body)
		{
			auto self = shared_from_this();
			response_body->discard(
			[self = std::move(self), next = std::move(next)]
			(error_code error) mutable {
				if( error )
				{
					self->fail(error);
					return;
				}
				auto redirect_source = request_access::source(next);
				if( not redirect_source )
				{
					auto owner = self->owner;
					auto completion = std::move(self->handler);
					owner->send(std::move(next), std::move(completion));
					return;
				}
				redirect_source->cancel();
				redirect_source->rewind(
				[owner = self->owner, next = std::move(next),
				 completion = std::move(self->handler)]
				(error_code rewind_error) mutable {
					if( rewind_error )
					{
						std::move(completion)(rewind_error, send_result{});
						return;
					}
					owner->send(std::move(next), std::move(completion));
				});
			});
		}

		void fail(error_code error)
		{
			const auto delivery = active_stream ? active_stream->delivery() :
				request_delivery::not_started;
			if( source ) source->cancel();
			if( active_stream ) active_stream->cancel();
			if( retry_allowed(request_value, retries, delivery) )
			{
				std::unique_ptr<request> next;
				try
				{
					next = std::make_unique<request>(
						request_access::clone(request_value));
					request_access::increment_retry(*next);
				}
				catch(...) {
					error = exception_error(std::current_exception());
					std::move(handler)(error, send_result{});
					return;
				}
				if( not source )
				{
					auto completion = std::move(handler);
					owner->send(std::move(*next), std::move(completion));
					return;
				}
				source->rewind(
				[owner = owner, next = std::move(next),
				 completion = std::move(handler)]
				(error_code rewind_error) mutable {
					if( rewind_error )
					{
						std::move(completion)(rewind_error, send_result{});
						return;
					}
					owner->send(std::move(*next), std::move(completion));
				});
				return;
			}
			std::move(handler)(error, send_result{});
		}
	};

	// Keep the operation's body contract separate from the value passed to
	// Session::open. Function-argument evaluation order must not be allowed to
	// move the trailer declaration out before open() copies it.
	auto operation_spec = spec;
	auto owner = shared_from_this();
	m_impl->open(*head, std::move(spec),
	[owner, request_value = std::move(request_value), request_body,
	 source = std::move(source), spec = std::move(operation_spec),
	 trailers = std::move(trailers),
	 max_buffered_body = m_impl->m_config.response_limits.max_buffered_body,
	 automatic_decompression = m_impl->m_config.automatic_decompression,
	 retries = m_impl->m_config.retries, redirects = m_impl->m_config.redirects,
	 handler = std::move(handler)]
	(error_code error, std::shared_ptr<stream> active_stream) mutable
	{
		if( error )
		{
			if( retry_allowed(request_value, retries,
				request_delivery::not_started) )
			{
				try
				{
					auto next = request_access::clone(request_value);
					request_access::increment_retry(next);
					owner->send(std::move(next), std::move(handler));
					return;
				}
				catch(...) {
					error = exception_error(std::current_exception());
				}
			}
			std::move(handler)(error, send_result{});
			return;
		}
		std::make_shared<operation>(owner, std::move(request_value),
			std::move(active_stream), request_body,
			std::move(source), std::move(spec), std::move(trailers),
			max_buffered_body, automatic_decompression,
			std::move(retries), std::move(redirects),
			std::move(handler))->start();
	});
}

std::shared_ptr<client_state> make_client_state(
	asio::any_io_executor executor, client_config config,
	std::shared_ptr<client_session> session, connector_ptr_t connector)
{
	return std::make_shared<client_state>(std::move(executor),
		std::move(config), std::move(session), std::move(connector));
}

std::shared_ptr<client_state> make_client_state(
	client_config config, std::shared_ptr<client_session> session,
	connector_ptr_t connector)
{
	return std::make_shared<client_state>(std::move(config),
		std::move(session), std::move(connector));
}

} //namespace riwo::http_exp::detail
