// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/experimental/http/client/client.h>

#include <riwo/experimental/http/body/detail/source_transfer.h>
#include <riwo/experimental/http/body/detail/progress.h>
#include <riwo/experimental/http/body/detail/decoder.h>

#include "request_access.h"
#include "session_pool.h"
#include "connector.h"
#include "factory.h"

namespace riwo::experimental::http::detail { namespace
{

[[nodiscard]] bool ascii(std::string_view text) noexcept
{
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

[[nodiscard]] sys_expected<request_head> freeze_request
(const request &value, const body_spec &spec, bool require_unbound) noexcept
try
{
	if( require_unbound and value.has_body_source() )
		return sys_unexpected(make_error_code(errc::operation_not_permitted));

	const auto &url = value.url();
	const auto scheme = lower(url.protocol());

	if( not url.is_valid() or (scheme != "http" and scheme != "https") or
		url.host().empty() or url.has_fragment() or not ascii(url.host()) )
		return sys_unexpected(make_error_code(errc::invalid_target));

	for(const auto &header : value.headers())
	{
		if( owned_header(header.name) )
			return sys_unexpected(make_error_code(errc::invalid_field_name));
	}
	if( auto length = value.headers().find_first("content-length") )
	{
		if( not spec.trailers().names.empty() or not spec.length() )
			return sys_unexpected(make_error_code(errc::body_length_mismatch));

		uint64_t parsed = 0;
		auto result = std::from_chars(length->data(), length->data() + length->size(), parsed);

		if( result.ec != std::errc{} or
			result.ptr != length->data() + length->size() or parsed != *spec.length() )
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

[[nodiscard]] std::string decodable_encoding(const reply_head &head)
{
#if RIWO_EXP_HTTP_ZLIB_SUPPORT
	std::string_view value {};
	auto values = head.headers.find_all("Content-Encoding");

	for(auto iterator=values.begin(); iterator!=values.end(); ++iterator)
		value = iterator->value.to_string();

	while( not value.empty() and (value.front() == ' ' or value.front() == '\t') )
		value.remove_prefix(1);

	while( not value.empty() and (value.back() == ' ' or value.back() == '\t') )
		value.remove_suffix(1);

	auto lower = strtls::to_lower(value);
	if( lower == "gzip" or lower == "x-gzip" or lower == "deflate" )
		return lower == "x-gzip" ? "gzip" : lower;
	return {};

#else //RIWO_EXP_HTTP_ZLIB_SUPPORT
	RIWO_UNUSED(head);
	return {};
#endif //RIWO_EXP_HTTP_ZLIB_SUPPORT
}

[[nodiscard]] body_reader_state_ptr decoded_body
(body_reader_state_ptr body, reply_head &head, bool auto_decompression)
{
	if( not auto_decompression or not body or body->is_finished() )
		return body;

	auto encoding = decodable_encoding(head);
	if( encoding.empty() )
		return body;

	head.headers.erase("Content-Encoding");
	head.headers.erase("Content-Length");

	return make_decoding_body_reader(std::move(body), std::move(encoding));
}

void add_accept_encoding(request_head &head, const client_config &config)
{
#if RIWO_EXP_HTTP_ZLIB_SUPPORT
	if( not config.auto_decompression or head.headers.find_first("Accept-Encoding") )
		return ;
	head.headers.add("Accept-Encoding", "gzip, deflate");

#else //RIWO_EXP_HTTP_ZLIB_SUPPORT
	ignore_unused(head, config);
#endif //RIWO_EXP_HTTP_ZLIB_SUPPORT
}

void add_proxy_authorization(request_head &head, const client_config &config)
{
	if( head.scheme != "http" or head.headers.find_first("Proxy-Authorization") )
		return ;

	const auto key = make_session_key(head, config);
	if( not key.proxy.endpoint )
		return ;

	auto type = key.proxy.type;
	if( type == proxy_type::automatic )
	{
		url endpoint(*key.proxy.endpoint);

		if( endpoint.protocol().starts_with("socks5") )
			type = proxy_type::socks5;
		else
			type = proxy_type::http;
	}
	if( type != proxy_type::http or not key.proxy.authorization )
		return ;

	head.headers.add("Proxy-Authorization", *key.proxy.authorization);
}

[[nodiscard]] optional<uint64_t> content_length(const reply_head &head) noexcept
{
	auto value = head.headers.find_first("Content-Length");
	if( not value )
		return {};

	uint64_t result = 0;
	auto parsed = std::from_chars (
		value->data(), value->data() + value->size(), result
	);
	if( parsed.ec != std::errc{} or parsed.ptr != value->data() + value->size() )
		return {};
	return result;
}

void notify_progress
(const progress_observer &observer, uint64_t transferred, optional<uint64_t> total) noexcept
{
	if( not observer )
		return ;
	try {
		observer(transferred, total.value_or(0));
	}
	catch(...) {}
}

[[nodiscard]] error_code add_request_cookies(request_head &head, const cookie_jar_ptr &store) noexcept
{
	if( not store or head.headers.find_first("Cookie") )
		return {};
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

[[nodiscard]] uint16_t effective_port(const url &value) noexcept
{
	if( value.port() != 0 )
		return value.port();
	if( value.protocol() == "http" )
		return 80;
	if( value.protocol() == "https" )
		return 443;
	return 0;
}

[[nodiscard]] bool same_origin(const url &lhs, const url &rhs) noexcept
try
{
	return lower(lhs.protocol()) == lower(rhs.protocol()) and
		   lower(lhs.host()) == lower(rhs.host()) and
		   effective_port(lhs) == effective_port(rhs);
}
catch(...) {
	return false;
}

[[nodiscard]] bool retry_allowed
(const request &value, const retry_policy &policy, request_delivery delivery) noexcept
{
	if( delivery == request_delivery::possibly_processed or
		request_access::retry_attempt(value) >= policy.max_attempts )
		return false;

	if( value.method().is_idempotent() )
	{
		if( not policy.retry_idempotent )
			return false;
	}
	else if( not policy.retry_non_idempotent )
		return false;

	const auto source = request_access::source(value);
	return not source or source->replayable();
}

[[nodiscard]] error_code rewind_sync(const body_source_ptr &source) noexcept
{
	if( not source )
		return {};
	try {
		struct state
		{
			std::mutex mutex {};
			bool completed = false;
			error_code error {};
		};
		auto result = std::make_shared<state>();
		source->rewind([result](error_code error)
		{
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

[[nodiscard]] std::unique_ptr<request> make_redirect_request
(const request &value, const reply_head &head, const redirect_policy &policy, error_code &error) noexcept
{
	error.clear();
	const auto code = head.status.code();

	if( code != 301 and code != 302 and code != 303 and
		code != 307 and code != 308 )
		return {};

	if( request_access::redirect_count(value) >= policy.max_redirects )
		return {};

	const auto location = head.headers.find_first("Location");
	if( not location )
		return {};

	try {
		auto target = url::resolve(value.url(), *location);
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
		if( from_scheme == "https" and to_scheme == "http" and not policy.allow_https_to_http )
			return {};

		auto next_method = value.method();
		bool drop_body = false;

		if( (code == 303 and value.method().known() != method::head) or
			((code == 301 or code == 302) and value.method().known() == method::post) )
		{
			next_method = method::get;
			drop_body = true;
		}
		const auto source = request_access::source(value);
		if( not drop_body and source and not source->replayable() )
			return {};

		const auto strip_credentials = not same_origin(value.url(), target);

		auto next = std::make_unique<request>(
			request_access::clone(value, std::move(target), std::move(next_method),
				drop_body, strip_credentials
			)
		);
		request_access::increment_redirect(*next);
		return next;
	}
	catch(...) {
		error = exception_error(std::current_exception());
	}
	return {};
}

} //namespace

session_stream_ptr client_state::open_stream_sync
(const request_head &head, const body_spec &spec, const client_config &config, error_code &error) noexcept
{
	error.clear();
	try {
		if( m_injected_session )
			return m_injected_session->open_sync(head, spec, error);

		return m_pool->open_sync (
			make_session_key(head, config), head, spec, error
		);
	}
	catch(...) {
		error = exception_error(std::current_exception());
	}
	return {};
}

void client_state::open_stream
(const request_head &head, body_spec spec, const client_config &config, open_handler_t handler)
{
	if( m_injected_session )
	{
		m_injected_session->open(head, std::move(spec), std::move(handler));
		return ;
	}
	m_pool->open(make_session_key(head, config),
		head, std::move(spec), std::move(handler)
	);
}

client_state::client_state(asio::any_io_executor executor,
	client_config config, client_session_ptr session, connector_ptr connector) :
	m_config(std::move(config)), m_injected_session(std::move(session))
{
	if( not m_injected_session )
	{
		m_pool = std::make_shared<session_pool>(
			std::move(executor), m_config, std::move(connector)
		);
	}
}

client_state::client_state
(client_config config, client_session_ptr session, connector_ptr connector) :
	client_state(asio::any_io_executor(get_executor()),
		std::move(config), std::move(session), std::move(connector)
	)
{

}

client_state::~client_state() = default;

session_stream_ptr client_state::open(const request &request_value,
	const body_spec &spec, error_code &error) noexcept
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
	auto config = request_access::effective_config(request_value, m_config);

	add_proxy_authorization(*head, config);
	return open_stream_sync(*head, spec, config, error);
}

void client_state::open(const request &request_value, body_spec spec, open_handler_t handler)
{
	auto head = freeze_request(request_value, spec, true);
	if( not head )
	{
		std::move(handler)(head.error(), session_stream_ptr{});
		return ;
	}
	if( auto cookie_error = add_request_cookies(*head, cookie_store()) )
	{
		std::move(handler)(cookie_error, session_stream_ptr{});
		return ;
	}
	auto config = request_access::effective_config(request_value, m_config);

	add_proxy_authorization(*head, config);
	open_stream(*head, std::move(spec), config, std::move(handler));
}

void client_state::capture_cookies(const url &origin, const reply_head &head) noexcept
{
	const auto store = cookie_store();
	if( not store )
		return ;
	try {
		store->store(origin, head.headers);
	}
	catch(...) {}
}

cookie_jar_ptr client_state::cookie_store() const noexcept
{
	return m_config.cookies.jar;
}

send_result client_state::send(request request_value, error_code &error) noexcept
{
	error.clear();
	if( auto prepare_error = request_access::prepare(request_value) )
	{
		error = prepare_error;
		return {};
	}
	auto spec = request_value.body_description();
	auto config = request_access::effective_config(request_value, m_config);

	auto upload_progress = request_access::upload_progress(request_value);
	auto download_progress = request_access::download_progress(request_value);

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
	add_accept_encoding(*head, config);
	add_proxy_authorization(*head, config);

	auto request_body = request_access::body(request_value);
	auto source = request_access::source(request_value);
	auto trailers = request_access::trailers(request_value);

	auto retry_request =
	[&](request_delivery delivery, bool rewind_body, send_result &result) -> bool
	{
		if( not retry_allowed(request_value, config.retries, delivery) )
			return false;
		try {
			auto next = request_access::clone(request_value);
			request_access::increment_retry(next);

			if( rewind_body )
			{
				error = rewind_sync(source);
				if( error )
					return true;
			}
			result = send(std::move(next), error);
		}
		catch(...) {
			error = exception_error(std::current_exception());
		}
		return true;
	};
	auto active_stream = open_stream_sync(*head, spec, config, error);
	if( error or not active_stream )
	{
		if( not error )
			error = make_error_code(errc::operation_not_supported);

		send_result retried {};
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
		if( not error )
			notify_progress(upload_progress, written, spec.length());

		if( not error and written != request_body->size() )
			error = make_error_code(errc::body_length_mismatch);

		if( not error )
			writer->finish(std::move(trailers), error);

		if( error )
		{
			const auto delivery = active_stream->delivery();
			active_stream->cancel();

			send_result retried {};
			if( retry_request(delivery, false, retried) )
				return retried;
			return {};
		}
	}
	else if( source )
	{
		auto length = spec.length();
		source_transfer_options options {
			.length = length,
			.trailers = std::move(trailers),
			.progress = [upload_progress, length](uint64_t transferred) {
				notify_progress(upload_progress, transferred, length);
			}
		};
		(void)transfer_source_sync (
			*source, *writer, std::move(options), error
		);
		if( error )
		{
			const auto delivery = active_stream->delivery();
			source->cancel();
			active_stream->cancel();

			send_result retried {};
			if( retry_request(delivery, true, retried) )
				return retried;
			return {};
		}
	}
	auto reply_head_value = active_stream->wait_response_sync(error);
	if( error )
	{
		const auto delivery = active_stream->delivery();
		if( source )
			source->cancel();

		active_stream->cancel();
		send_result retried {};

		if( retry_request(delivery, source != nullptr, retried) )
			return retried;
		return {};
	}
	capture_cookies(request_value.url(), reply_head_value);
	auto body = active_stream->inbound_body();

	if( not body )
	{
		error = make_error_code(errc::operation_not_permitted);
		active_stream->cancel();
		return {};
	}
	error_code redirect_error {};
	auto redirect = make_redirect_request (
		request_value, reply_head_value, config.redirects, redirect_error
	);
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
			if( error )
				return {};
		}
		return send(std::move(*redirect), error);
	}
	body->set_ready();
	if( download_progress )
	{
		const auto total = content_length(reply_head_value).value_or(0);
		body = make_progress_body_reader (
			std::move(body), total, std::move(download_progress)
		);
	}
	body = decoded_body(std::move(body),
		reply_head_value, config.auto_decompression
	);
	return send_result {
		.selected_protocol = active_stream->selected_protocol(),
		.reply_protocol = active_stream->response_protocol(),
		.head = std::move(reply_head_value),
		.body = std::move(body),
		.owner = std::move(active_stream),
		.max_buffered_body = m_config.reply_limits.max_buffered_body
	};
}

struct send_operation : std::enable_shared_from_this<send_operation>
{
	using send_handler_t = client_state::send_handler_t;

	client_state_ptr owner {};
	request request_value;

	session_stream_ptr active_stream {};
	optional<const_buffer> body {};

	body_source_ptr source {};
	body_writer_state_ptr writer {};

	body_spec spec;
	fields trailers {};
	send_handler_t handler {};

	uint64_t max_buffered_body = 8U * 1024U * 1024U;
	bool auto_decompression = true;

	retry_policy retries {};
	redirect_policy redirects {};

	progress_observer upload_progress {};
	progress_observer download_progress {};

	send_operation(client_state_ptr owner_value, request original_request, session_stream_ptr stream_value,
		optional<const_buffer> body_value, body_source_ptr source_value, body_spec description,
		fields trailer_values, uint64_t max_buffered_body_value, bool auto_decompression_value,
		retry_policy retry_values, redirect_policy redirect_values,
		progress_observer upload_progress_value, progress_observer download_progress_value,
		send_handler_t completion
	) :
		owner(std::move(owner_value)), request_value(std::move(original_request)),
		active_stream(std::move(stream_value)), body(std::move(std::move(body_value))),
		source(std::move(source_value)),
		writer(active_stream ? active_stream->outbound_body() : nullptr),
		spec(std::move(description)), trailers(std::move(trailer_values)), handler(std::move(completion)),
		max_buffered_body(max_buffered_body_value), auto_decompression(auto_decompression_value),
		retries(retry_values), redirects(redirect_values),
		upload_progress(std::move(upload_progress_value)),
		download_progress(std::move(download_progress_value)) {}

		void start()
		{
			if( not active_stream or not writer )
			{
				fail(make_error_code(errc::operation_not_permitted));
				return ;
			}
			if( body )
			{
				write_body();
				return ;
			}
			if( not source )
			{
				wait_reply();
				return ;
			}
			transfer_body_source();
		}

		void write_body()
		{
			auto self = shared_from_this();
			writer->write(*body,
			[self = std::move(self)](error_code error, size_t written) mutable
			{
				if( error )
					self->fail(error);

				else if( written != self->body->size() )
					self->fail(make_error_code(errc::body_length_mismatch));
				else
				{
					notify_progress(self->upload_progress, written, self->spec.length());
					self->finish_buffer_body();
				}
			});
		}

		void finish_buffer_body()
		{
			auto self = shared_from_this();
			writer->finish(std::move(trailers),
			[self = std::move(self)](error_code error) mutable
			{
				if( error )
					self->fail(error);
				else
					self->wait_reply();
			});
		}

		void transfer_body_source()
		{
			auto length = spec.length();
			source_transfer_options options {
				.length = length,
				.trailers = std::move(trailers),
				.progress = [observer = upload_progress, length](uint64_t transferred) {
					notify_progress(observer, transferred, length);
				}
			};
			auto self = shared_from_this();
			transfer_source(source, writer, std::move(options),
			[self = std::move(self)](error_code error, uint64_t) mutable
			{
				if( error )
					self->fail(error);
				else
					self->wait_reply();
			});
		}

		void wait_reply()
		{
			auto self = shared_from_this();
			active_stream->wait_response(
			[self = std::move(self)](error_code error, reply_head head) mutable
			{
				if( error )
				{
					self->fail(error);
					return ;
				}
				self->owner->capture_cookies(self->request_value.url(), head);
				auto body = self->active_stream->inbound_body();

				if( not body )
				{
					self->fail(make_error_code(errc::operation_not_permitted));
					return ;
				}
				error_code redirect_error {};
				auto redirect = make_redirect_request (
					self->request_value, head, self->redirects, redirect_error
				);
				if( redirect_error )
				{
					self->fail(redirect_error);
					return ;
				}
				if( redirect )
				{
					body->set_ready();
					self->follow_redirect(std::move(*redirect), body);
					return ;
				}
				body->set_ready();

				if( self->download_progress )
				{
					const auto total = content_length(head).value_or(0);
					body = make_progress_body_reader(std::move(body), total,
						self->download_progress);
				}
				body = decoded_body(std::move(body), head, self->auto_decompression);

				auto result = send_result {
					.selected_protocol = self->active_stream->selected_protocol(),
					.reply_protocol = self->active_stream->response_protocol(),
					.head = std::move(head),
					.body = std::move(body),
					.owner = self->active_stream,
					.max_buffered_body = self->max_buffered_body
				};
				std::move(self->handler)(error_code{}, std::move(result));
			});
		}

		void follow_redirect(request next, const body_reader_state_ptr &reply_body)
		{
			auto self = shared_from_this();
			reply_body->discard(
			[self = std::move(self), next = std::move(next)](error_code error) mutable
			{
				if( error )
				{
					self->fail(error);
					return ;
				}
				auto redirect_source = request_access::source(next);

				if( not redirect_source )
				{
					auto owner = self->owner;
					auto completion = std::move(self->handler);

					owner->send(std::move(next), std::move(completion));
					return ;
				}
				redirect_source->cancel();

				redirect_source->rewind (
				[owner = self->owner, next = std::move(next), completion = std::move(self->handler)]
				(error_code rewind_error) mutable
				{
					if( rewind_error )
					{
						std::move(completion)(rewind_error, send_result{});
						return ;
					}
					owner->send(std::move(next), std::move(completion));
				});
			});
		}

		void fail(error_code error)
		{
			const auto delivery = active_stream ?
				active_stream->delivery() : request_delivery::not_started;

			if( source )
				source->cancel();

			if( active_stream )
				active_stream->cancel();

			if( retry_allowed(request_value, retries, delivery) )
			{
				std::unique_ptr<request> next {};
				try {
					next = std::make_unique<request>(request_access::clone(request_value));
					request_access::increment_retry(*next);
				}
				catch(...)
				{
					error = exception_error(std::current_exception());
					std::move(handler)(error, send_result{});
					return ;
				}
				if( not source )
				{
					auto completion = std::move(handler);
					owner->send(std::move(*next), std::move(completion));
					return ;
				}
				source->rewind(
				[owner = owner, next = std::move(next), completion = std::move(handler)]
				(error_code rewind_error) mutable
				{
					if( rewind_error )
					{
						std::move(completion)(rewind_error, send_result{});
						return ;
					}
					owner->send(std::move(*next), std::move(completion));
				});
				return ;
			}
			std::move(handler)(error, send_result{});
		}
	};

void client_state::send(request request_value, send_handler_t handler)
{
	if( auto prepare_error = request_access::prepare(request_value) )
	{
		std::move(handler)(prepare_error, send_result{});
		return ;
	}
	auto spec = request_value.body_description();
	auto config = request_access::effective_config(request_value, m_config);

	auto upload_progress = request_access::upload_progress(request_value);
	auto download_progress = request_access::download_progress(request_value);

	auto head = freeze_request(request_value, spec, false);
	if( not head )
	{
		std::move(handler)(head.error(), send_result{});
		return ;
	}
	if( auto cookie_error = add_request_cookies(*head, cookie_store()) )
	{
		std::move(handler)(cookie_error, send_result{});
		return ;
	}
	add_accept_encoding(*head, config);
	add_proxy_authorization(*head, config);

	auto request_body = request_access::body(request_value);
	auto source = request_access::source(request_value);
	auto trailers = request_access::trailers(request_value);

	// Keep the operation's body contract separate from the value passed to
	// Session::open. Function-argument evaluation order must not be allowed to
	// move the trailer declaration out before open() copies it.
	auto operation_spec = spec;
	auto owner = shared_from_this();

	open_stream(*head, std::move(spec), config, [
		owner, request_value = std::move(request_value), request_body, source = std::move(source),
		spec = std::move(operation_spec), trailers = std::move(trailers),
		max_buffered_body = m_config.reply_limits.max_buffered_body,
		auto_decompression = config.auto_decompression, retries = config.retries,
		redirects = config.redirects, upload_progress = std::move(upload_progress),
		download_progress = std::move(download_progress), handler = std::move(handler)
	](error_code error, const session_stream_ptr &active_stream) mutable
	{
		if( error )
		{
			if( retry_allowed(request_value, retries,
				request_delivery::not_started) )
			{
				try {
					auto next = request_access::clone(request_value);
					request_access::increment_retry(next);

					owner->send(std::move(next), std::move(handler));
					return ;
				}
				catch(...) {
					error = exception_error(std::current_exception());
				}
			}
			std::move(handler)(error, send_result{});
			return ;
		}
		std::make_shared<send_operation>(owner, std::move(request_value), active_stream, request_body,
			std::move(source), std::move(spec), std::move(trailers), max_buffered_body,
			auto_decompression, retries, redirects, std::move(upload_progress),
			std::move(download_progress), std::move(handler)
		)->start();
	});
}

client_state_ptr make_client_state
(asio::any_io_executor executor, client_config config, client_session_ptr session, connector_ptr connector)
{
	return std::make_shared<client_state>(std::move(executor),
		std::move(config), std::move(session), std::move(connector)
	);
}

client_state_ptr make_client_state
(client_config config, client_session_ptr session, connector_ptr connector)
{
	return std::make_shared<client_state>(
		std::move(config), std::move(session), std::move(connector)
	);
}

} //namespace riwo::experimental::http::detail
