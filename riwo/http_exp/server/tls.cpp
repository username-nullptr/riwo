// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "tls.h"

#if RIWO_OPENSSL_SUPPORT

#include <riwo/http_exp/transport/tls_connection.h>
#include <openssl/ssl.h>

namespace riwo::http_exp
{
namespace
{

[[nodiscard]] std::string endpoint_text(
	const asio::ip::tcp::endpoint &value)
{
	auto address = value.address().to_string();
	if( address.find(':') != std::string::npos )
		address = '[' + address + ']';
	return address + ':' + std::to_string(value.port());
}

[[nodiscard]] optional<protocol> alpn_protocol(std::string_view value)
{
#if RIWO_HTTP_EXP_HTTP2_SUPPORT
	if( value == "h2" )
		return protocol::v2;
#endif
	if( value == "http/1.1" )
		return protocol::v1_1;
	return {};
}

[[nodiscard]] bool contains_protocol(const protocol_policy &policy,
	protocol value) noexcept
{
	return std::ranges::find(policy.preference, value) !=
		policy.preference.end();
}

[[nodiscard]] bool valid_tls_policy(const protocol_policy &policy) noexcept
{
	if( policy.preference.empty() )
		return false;
	for( auto current = policy.preference.begin();
		current != policy.preference.end(); ++current )
		if( std::ranges::find(policy.preference.begin(), current, *current) !=
			current )
			return false;
	if( policy.requirement == protocol_requirement::require_http1 )
		return contains_protocol(policy, protocol::v1_1);
#if RIWO_HTTP_EXP_HTTP2_SUPPORT
	if( policy.requirement == protocol_requirement::require_http2 )
		return contains_protocol(policy, protocol::v2);
	return contains_protocol(policy, protocol::v1_1) or
		contains_protocol(policy, protocol::v2);
#else
	return contains_protocol(policy, protocol::v1_1);
#endif
}

class tls_stream_model
{
public:
	using executor_type = asio::any_io_executor;
	explicit tls_stream_model(std::shared_ptr<tls_connection> value,
		transport_metadata metadata) :
		m_value(std::move(value)), m_metadata(std::move(metadata)) {}

	[[nodiscard]] executor_type get_executor() const noexcept {
		return m_value->get_executor();
	}
	void read_some(mutable_buffer buffer,
		asio::any_completion_handler<void(error_code,size_t)> handler) {
		m_value->read_some(buffer, std::move(handler));
	}
	void write(const_buffer buffer,
		asio::any_completion_handler<void(error_code,size_t)> handler) {
		m_value->write(buffer, std::move(handler));
	}
	void close(asio::any_completion_handler<void(error_code)> handler) {
		m_value->close(std::move(handler));
	}
	void cancel() noexcept { m_value->cancel(); }
	[[nodiscard]] bool is_open() const noexcept { return m_value->is_open(); }
	[[nodiscard]] size_t read_some_sync(mutable_buffer buffer,
		error_code &error) noexcept {
		return m_value->read_some_sync(buffer, error);
	}
	[[nodiscard]] size_t write_sync(const_buffer buffer,
		error_code &error) noexcept {
		return m_value->write_sync(buffer, error);
	}
	[[nodiscard]] transport_capabilities capabilities() const noexcept {
		return {.synchronous = true, .cancellable = true, .secure = true};
	}
	[[nodiscard]] transport_metadata metadata() const { return m_metadata; }

private:
	std::shared_ptr<tls_connection> m_value;
	transport_metadata m_metadata;
};

} //namespace

server_tls_context::server_tls_context(server_tls_config config) :
	m_config(std::move(config)), m_context(asio::ssl::context::tls_server)
{
	if( m_config.certificate_chain_file.empty() or
		m_config.private_key_file.empty() )
		throw std::invalid_argument("HTTP TLS certificate and private key are required");
	if( not valid_tls_policy(m_config.protocols) )
		throw std::invalid_argument("HTTP TLS protocol policy is invalid");
	m_context.set_options(asio::ssl::context::default_workarounds |
		asio::ssl::context::no_sslv2 | asio::ssl::context::no_sslv3);
	m_context.use_certificate_chain_file(
		m_config.certificate_chain_file.string());
	m_context.use_private_key_file(m_config.private_key_file.string(),
		asio::ssl::context::pem);
	if( m_config.certificate_authority_file )
		m_context.load_verify_file(
			m_config.certificate_authority_file->string());
	m_context.set_verify_mode(m_config.verify_peer ?
		asio::ssl::verify_peer : asio::ssl::verify_none);
	::SSL_CTX_set_alpn_select_cb(m_context.native_handle(),
		[](SSL *ssl, const unsigned char **out, unsigned char *out_length,
			const unsigned char *input, unsigned int input_length,
			void *argument) noexcept {
			return select_alpn(ssl, out, out_length, input, input_length,
				argument);
		}, this);
}

server_tls_context::~server_tls_context() = default;

asio::ssl::context &server_tls_context::native_context() noexcept {
	return m_context;
}

const server_tls_config &server_tls_context::config() const noexcept {
	return m_config;
}

int server_tls_context::select_alpn(void *, const unsigned char **out,
	unsigned char *out_length, const unsigned char *input,
	unsigned int input_length, void *argument) noexcept
{
	const auto &policy = static_cast<server_tls_context*>(argument)->m_config.protocols;
	for( auto preferred : policy.preference )
	{
		if( policy.requirement == protocol_requirement::require_http1 and
			preferred != protocol::v1_1 )
			continue;
#if RIWO_HTTP_EXP_HTTP2_SUPPORT
		if( policy.requirement == protocol_requirement::require_http2 and
			preferred != protocol::v2 )
			continue;
		const std::string_view wanted = preferred == protocol::v2 ?
			std::string_view("h2") : std::string_view("http/1.1");
#else
		const std::string_view wanted = "http/1.1";
#endif
		for( unsigned int offset = 0; offset < input_length; )
		{
			const auto length = input[offset++];
			if( offset + length > input_length )
				return SSL_TLSEXT_ERR_ALERT_FATAL;
			if( std::string_view(reinterpret_cast<const char*>(input + offset),
				length) == wanted )
			{
				*out = input + offset;
				*out_length = length;
				return SSL_TLSEXT_ERR_OK;
			}
			offset += length;
		}
	}
	return policy.requirement == protocol_requirement::automatic ?
		SSL_TLSEXT_ERR_NOACK : SSL_TLSEXT_ERR_ALERT_FATAL;
}

class tls_listener::impl : public std::enable_shared_from_this<impl>
{
public:
	impl(http_exp::acceptor_wrap acceptor,
		std::shared_ptr<server_tls_context> context) :
		m_acceptor(std::move(acceptor)), m_context(std::move(context))
	{
		if( not m_context )
			throw std::invalid_argument("TLS context is null");
	}

	void accept(accept_handler_t handler)
	{
		{
			std::lock_guard lock(m_mutex);
			if( m_cancelled )
			{
				asio::post(m_acceptor.get_executor(),
					[handler = std::move(handler)]() mutable {
						std::move(handler)(asio::error::make_error_code(
							asio::error::operation_aborted), result_t{});
					});
				return ;
			}
		}
		auto socket = std::make_shared<tls_socket_t>(
			m_acceptor.get_executor(), m_context->native_context());
		auto self = shared_from_this();
		m_acceptor.acceptor().async_accept(socket->next_layer(),
			[self, socket, handler = std::move(handler)]
			(error_code error) mutable
			{
				if( error )
					return std::move(handler)(error, result_t{});
				if( not self->track(socket) )
					return std::move(handler)(asio::error::make_error_code(
						asio::error::operation_aborted), result_t{});
				auto timer = std::make_shared<asio::steady_timer>(
					self->m_acceptor.get_executor());
				auto expired = std::make_shared<std::atomic_bool>(false);
				const auto timeout = self->m_context->config().handshake_timeout;
				if( timeout.count() > 0 )
				{
					timer->expires_after(timeout);
					timer->async_wait([socket, expired](error_code timer_error) {
						if( timer_error )
							return ;
						expired->store(true, std::memory_order_relaxed);
						error_code ignored;
						socket->next_layer().close(ignored);
					});
				}
				socket->async_handshake(asio::ssl::stream_base::server,
					[self, socket, timer, expired,
					 handler = std::move(handler)](error_code handshake_error) mutable
					{
						timer->cancel();
						self->untrack(socket);
						if( expired->load(std::memory_order_relaxed) )
							handshake_error =
							asio::error::make_error_code(asio::error::timed_out);
						if( handshake_error )
							return std::move(handler)(handshake_error, result_t{});
						const unsigned char *data = nullptr;
						unsigned int size = 0;
						::SSL_get0_alpn_selected(socket->native_handle(), &data, &size);
						optional<std::string> alpn;
						optional<protocol> selected;
						if( data and size )
						{
							alpn.emplace(reinterpret_cast<const char*>(data), size);
							selected = alpn_protocol(*alpn);
						}
						if( not selected )
						{
							const auto &policy =
								self->m_context->config().protocols;
							if( policy.requirement !=
								protocol_requirement::require_http1 and
								(policy.requirement !=
									protocol_requirement::automatic or
								not contains_protocol(policy, protocol::v1_1)) )
								return std::move(handler)(make_error_code(
									errc::negotiation_failed), result_t{});
							selected = protocol::v1_1;
						}
						error_code endpoint_error;
						auto remote = socket->next_layer().remote_endpoint(endpoint_error);
						if( endpoint_error )
							return std::move(handler)(
							endpoint_error, result_t{});
						auto local = socket->next_layer().local_endpoint(endpoint_error);
						if( endpoint_error )
							return std::move(handler)(
							endpoint_error, result_t{});
						transport_metadata metadata {
							.local_endpoint = endpoint_text(local),
							.remote_endpoint = endpoint_text(remote),
							.tls = true,
							.alpn = alpn
						};
						auto connection = std::make_shared<tls_connection>(
							std::move(*socket));
						result_t result {
							.stream = byte_stream(tls_stream_model(
								std::move(connection), metadata)),
							.selected_protocol = *selected,
							.metadata = std::move(metadata)
						};
						std::move(handler)(error_code{}, std::move(result));
					});
			});
	}

	[[nodiscard]] bool track(const std::shared_ptr<tls_socket_t> &socket)
	{
		std::lock_guard lock(m_mutex);
		if( m_cancelled )
		{
			error_code ignored;
			socket->next_layer().close(ignored);
			return false;
		}
		m_handshakes.emplace_back(socket);
		return true;
	}

	void untrack(const std::shared_ptr<tls_socket_t> &socket)
	{
		std::lock_guard lock(m_mutex);
		std::erase_if(m_handshakes, [&](const auto &value) {
			auto current = value.lock();
			return not current or current == socket;
		});
	}

	void cancel() noexcept
	{
		error_code ignored;
		m_acceptor.acceptor().cancel(ignored);
		m_acceptor.acceptor().close(ignored);
		std::lock_guard lock(m_mutex);
		m_cancelled = true;
		for( const auto &value : m_handshakes )
			if( auto socket = value.lock() )
				socket->next_layer().close(ignored);
		m_handshakes.clear();
	}

	http_exp::acceptor_wrap m_acceptor;
	std::shared_ptr<server_tls_context> m_context;
	std::mutex m_mutex;
	std::vector<std::weak_ptr<tls_socket_t>> m_handshakes;
	bool m_cancelled = false;
};

tls_listener::tls_listener(http_exp::acceptor_wrap acceptor,
	std::shared_ptr<server_tls_context> context) :
	m_impl(std::make_shared<impl>(std::move(acceptor), std::move(context))) {}

tls_listener::~tls_listener() = default;

auto tls_listener::get_executor() const noexcept -> executor_t {
	return m_impl->m_acceptor.get_executor();
}

void tls_listener::accept(accept_handler_t handler) {
	m_impl->accept(std::move(handler));
}

void tls_listener::cancel() noexcept {
	m_impl->cancel();
}

http_exp::acceptor_wrap &tls_listener::acceptor_wrap() noexcept {
	return m_impl->m_acceptor;
}

std::shared_ptr<tls_listener> make_tls_listener(http_exp::acceptor_wrap acceptor,
	std::shared_ptr<server_tls_context> context)
{
	return std::make_shared<tls_listener>(std::move(acceptor),
		std::move(context));
}

} //namespace riwo::http_exp

#endif //RIWO_OPENSSL_SUPPORT
