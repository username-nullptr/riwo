// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "exchange.h"
#include <riwo/http_exp/server/application_session.h>
#include <riwo/http_exp/protocol/compression.h>
#include <riwo/http_exp/protocol/detail/session.h>

namespace riwo::http_exp::detail
{

class RIWO_HTTP_EXP_TAPI server_exchange_state::impl
{
public:
	impl(std::shared_ptr<stream> value, request_head metadata,
		parameter_view path_values, parameter_view query_values,
		std::shared_ptr<application_session_manager> manager,
		std::filesystem::path root, bool compression_enabled,
		uint64_t compression_minimum) :
		m_stream(std::move(value)), m_request(std::move(metadata)),
		m_path_parameters(std::move(path_values)),
		m_query_parameters(std::move(query_values)),
		m_sessions(std::move(manager)), m_resource_root(std::move(root)),
		m_request_body(m_stream ? m_stream->inbound_body() : make_body_reader_state()),
		m_response_body(m_stream ? m_stream->outbound_body() : make_body_writer_state()),
		m_compression_enabled(compression_enabled),
		m_compression_minimum(compression_minimum)
	{
		if( not m_request_body )
			m_request_body = make_body_reader_state();
		if( not m_response_body )
			m_response_body = make_body_writer_state();
	}

	std::shared_ptr<stream> m_stream;
	request_head m_request;
	parameter_view m_path_parameters;
	parameter_view m_query_parameters;
	std::shared_ptr<application_session_manager> m_sessions;
	application_session_ptr m_application_session;
	std::filesystem::path m_resource_root;
	std::shared_ptr<body_reader_state> m_request_body;
	std::shared_ptr<body_writer_state> m_response_body;
	bool m_response_pending = false;
	bool m_response_started = false;
	bool m_duplex_taken = false;
	bool m_set_session_cookie = false;
	bool m_compression_enabled = false;
	uint64_t m_compression_minimum = 1024;
};

server_exchange_state::server_exchange_state(std::shared_ptr<stream> stream,
	request_head request, parameter_view path_parameters,
	parameter_view query_parameters,
	std::shared_ptr<application_session_manager> sessions,
	std::filesystem::path resource_root, bool automatic_compression,
	uint64_t compression_minimum_size) :
	m_impl(std::make_shared<impl>(std::move(stream), std::move(request),
		std::move(path_parameters), std::move(query_parameters),
		std::move(sessions), std::move(resource_root), automatic_compression,
		compression_minimum_size))
{
}

server_exchange_state::~server_exchange_state() = default;

protocol server_exchange_state::selected_protocol() const noexcept {
	return m_impl->m_stream ? m_impl->m_stream->selected_protocol() : protocol::v1_1;
}

std::string server_exchange_state::remote_address() const {
	if( not m_impl->m_stream ) return {};
	error_code error;
	auto endpoint = m_impl->m_stream->remote_endpoint(error);
	return error ? std::string{} : endpoint.address().to_string();
}

uint16_t server_exchange_state::remote_port() const noexcept {
	if( not m_impl->m_stream ) return 0;
	error_code error;
	auto endpoint = m_impl->m_stream->remote_endpoint(error);
	return error ? 0 : endpoint.port();
}

asio::ip::tcp::endpoint server_exchange_state::remote_endpoint(
	error_code &error) const noexcept
{
	if( not m_impl->m_stream )
	{
		error = make_error_code(errc::operation_not_permitted);
		return {};
	}
	return m_impl->m_stream->remote_endpoint(error);
}

asio::ip::tcp::endpoint server_exchange_state::local_endpoint(
	error_code &error) const noexcept
{
	if( not m_impl->m_stream )
	{
		error = make_error_code(errc::operation_not_permitted);
		return {};
	}
	return m_impl->m_stream->local_endpoint(error);
}

stream_capabilities server_exchange_state::capabilities() const noexcept {
	return m_impl->m_stream ? m_impl->m_stream->capabilities() : stream_capabilities{};
}

const request_head &server_exchange_state::request() const noexcept {
	return m_impl->m_request;
}

const parameter_view &server_exchange_state::path_parameters() const noexcept {
	return m_impl->m_path_parameters;
}

const parameter_view &server_exchange_state::query_parameters() const noexcept {
	return m_impl->m_query_parameters;
}

bool server_exchange_state::response_started() const noexcept {
	return m_impl->m_response_started or m_impl->m_response_pending;
}

application_session_ptr server_exchange_state::find_session()
{
	if( m_impl->m_application_session and
		m_impl->m_application_session->valid() )
		return m_impl->m_application_session;
	if( not m_impl->m_sessions ) return {};
	const auto config = m_impl->m_sessions->config();
	auto cookies = parse_cookie_fields(m_impl->m_request.headers);
	if( not cookies ) return {};
	for( const auto &cookie : *cookies )
	{
		if( cookie.name != config.cookie_name ) continue;
		m_impl->m_application_session =
			m_impl->m_sessions->get(cookie.value.to_string());
		break;
	}
	return m_impl->m_application_session;
}

application_session_ptr server_exchange_state::session()
{
	if( auto current = find_session() )
	{
		current->refresh();
		return current;
	}
	if( not m_impl->m_sessions ) return {};
	m_impl->m_application_session = m_impl->m_sessions->create();
	m_impl->m_set_session_cookie = true;
	return m_impl->m_application_session;
}

const std::filesystem::path &server_exchange_state::resource_root()
	const noexcept { return m_impl->m_resource_root; }

bool server_exchange_state::should_compress(const response_head &head,
	optional<uint64_t> length) const noexcept
{
#if !RIWO_HTTP_EXP_ZLIB_SUPPORT
	ignore_unused(head, length);
	return false;
#else
	if( not m_impl->m_compression_enabled or not head.status.allows_body() or
		(length and *length < m_impl->m_compression_minimum) or
		head.headers.find_first("content-encoding") or
		m_impl->m_request.headers.find_first("range") ) return false;
	const auto accepts = m_impl->m_request.headers.find_first("accept-encoding");
	if( not accepts or content_coding_quality(*accepts, "gzip") <= 0. )
		return false;
	const auto type = head.headers.find_first("content-type");
	return type and is_compressible_mime_type(*type);
#endif
}

bool server_exchange_state::prepare_compression(response_head &head,
	optional<uint64_t> length)
{
	if( not should_compress(head, length) ) return false;
	head.headers.set("Content-Encoding", "gzip");
	head.headers.erase("Content-Length");
	if( const auto vary = head.headers.find_first("vary") )
	{
		std::string value(*vary);
		std::string lower = value;
		std::ranges::transform(lower, lower.begin(), [](unsigned char ch) {
			return static_cast<char>(std::tolower(ch));
		});
		if( lower.find("accept-encoding") == std::string::npos )
			head.headers.set("Vary", value + ", Accept-Encoding");
	}
	else head.headers.add("Vary", "Accept-Encoding");
	if( const auto etag_value = head.headers.find_first("etag") )
	{
		if( auto etag = parse_entity_tag(*etag_value) )
			head.headers.set("ETag", "W/\"" + etag->opaque + "-gzip\"");
	}
	return true;
}

namespace
{

void attach_session_cookie(auto &state,
	response_head &head)
{
	if( not state.m_set_session_cookie or not state.m_application_session or
		not state.m_sessions ) return;
	const auto config = state.m_sessions->config();
	add_set_cookie(head.headers, response_cookie {
		.name = config.cookie_name,
		.value = riwo::value(std::string(state.m_application_session->id())),
		.path = config.cookie_path,
		.max_age = config.lifetime,
		.same_site = config.cookie_same_site_value,
		.secure = config.cookie_secure,
		.http_only = config.cookie_http_only
	});
}

} //namespace

std::shared_ptr<body_reader_state>
server_exchange_state::request_body() const noexcept {
	return m_impl->m_request_body;
}

std::shared_ptr<body_writer_state>
server_exchange_state::response_body() const noexcept {
	return m_impl->m_response_body;
}

void *server_exchange_state::protocol_extension(
	const std::type_info &type) noexcept
{
	return m_impl->m_stream ?
		m_impl->m_stream->protocol_extension(type) : nullptr;
}

void server_exchange_state::respond(response_head head, body_spec spec,
	error_code &error) noexcept
{
	auto state = m_impl;
	if( state->m_response_pending )
	{
		error = make_error_code(errc::operation_in_progress);
		return;
	}
	if( state->m_response_started or not state->m_stream )
	{
		error = make_error_code(errc::operation_not_permitted);
		return;
	}
	state->m_response_pending = true;
	attach_session_cookie(*state, head);
	state->m_response_body->configure(spec);
	state->m_stream->respond_sync(std::move(head), spec, error);
	state->m_response_pending = false;
	if( not error )
	{
		state->m_response_started = true;
		state->m_response_body->set_ready();
	}
}

void server_exchange_state::respond(response_head head, body_spec spec,
	operation_handler_t handler)
{
	auto state = m_impl;
	if( state->m_response_pending )
	{
		std::move(handler)(make_error_code(errc::operation_in_progress));
		return;
	}
	if( state->m_response_started or not state->m_stream )
	{
		std::move(handler)(make_error_code(errc::operation_not_permitted));
		return;
	}
	state->m_response_pending = true;
	attach_session_cookie(*state, head);
	state->m_response_body->configure(spec);
	state->m_stream->respond(std::move(head), spec,
	[state, handler = std::move(handler)]
	(error_code error) mutable
	{
		state->m_response_pending = false;
		if( not error )
		{
			state->m_response_started = true;
			state->m_response_body->set_ready();
		}
		std::move(handler)(error);
	});
}

void server_exchange_state::inform(response_head head,
	error_code &error) noexcept
{
	if( head.status.code() < 100 or head.status.code() >= 200 or
		head.status.code() == status::switching_protocols )
	{
		error = make_error_code(errc::invalid_status);
		return;
	}
	if( not m_impl->m_stream or m_impl->m_response_started )
	{
		error = make_error_code(errc::operation_not_permitted);
		return;
	}
	m_impl->m_stream->inform_sync(std::move(head), error);
}

void server_exchange_state::inform(response_head head,
	operation_handler_t handler)
{
	if( head.status.code() < 100 or head.status.code() >= 200 or
		head.status.code() == status::switching_protocols )
	{
		std::move(handler)(make_error_code(errc::invalid_status));
		return;
	}
	if( not m_impl->m_stream or m_impl->m_response_started )
	{
		std::move(handler)(make_error_code(errc::operation_not_permitted));
		return;
	}
	m_impl->m_stream->inform(std::move(head), std::move(handler));
}

std::shared_ptr<duplex_state> server_exchange_state::accept_duplex(
	response_head head, error_code &error) noexcept
{
	auto state = m_impl;
	if( state->m_response_pending or state->m_response_started or
		state->m_duplex_taken or not state->m_stream )
	{
		error = make_error_code(errc::operation_not_permitted);
		return {};
	}
	state->m_response_pending = true;
	state->m_response_body->configure(body_spec::streaming());
	auto result = state->m_stream->open_duplex_sync(
		optional<response_head>(std::move(head)), error);
	state->m_response_pending = false;
	if( not error )
	{
		state->m_response_started = true;
		state->m_duplex_taken = true;
		state->m_response_body->set_ready();
	}
	return result;
}

void server_exchange_state::accept_duplex(response_head head,
	duplex_handler_t handler)
{
	auto state = m_impl;
	if( state->m_response_pending or state->m_response_started or
		state->m_duplex_taken or not state->m_stream )
	{
		std::move(handler)(make_error_code(errc::operation_not_permitted),
			std::shared_ptr<duplex_state>{});
		return;
	}
	state->m_response_pending = true;
	state->m_response_body->configure(body_spec::streaming());
	state->m_stream->open_duplex(optional<response_head>(std::move(head)),
	[state, handler = std::move(handler)]
	(error_code error, std::shared_ptr<duplex_state> channel) mutable {
		state->m_response_pending = false;
		if( not error )
		{
			state->m_response_started = true;
			state->m_duplex_taken = true;
			state->m_response_body->set_ready();
		}
		std::move(handler)(error, std::move(channel));
	});
}

void server_exchange_state::cancel() noexcept
{
	if( m_impl->m_duplex_taken ) return;
	if( m_impl->m_stream and m_impl->m_stream->reusable() ) return;
	m_impl->m_request_body->cancel();
	m_impl->m_response_body->cancel();
	if( m_impl->m_stream )
		m_impl->m_stream->cancel();
}

std::shared_ptr<server_exchange_state> make_server_exchange_state(
	std::shared_ptr<stream> stream, request_head request,
	parameter_view path_parameters, parameter_view query_parameters,
	std::shared_ptr<application_session_manager> sessions,
	std::filesystem::path resource_root, bool automatic_compression,
	uint64_t compression_minimum_size)
{
	return std::make_shared<server_exchange_state>(
		std::move(stream), std::move(request), std::move(path_parameters),
		std::move(query_parameters), std::move(sessions),
		std::move(resource_root), automatic_compression,
		compression_minimum_size);
}

} //namespace riwo::http_exp::detail
