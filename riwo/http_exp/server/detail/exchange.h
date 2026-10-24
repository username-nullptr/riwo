// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_SERVER_DETAIL_EXCHANGE_H
#define RIWO_HTTP_EXP_SERVER_DETAIL_EXCHANGE_H

#include <riwo/http_exp/body/detail/encoder.h>
#include <riwo/http_exp/body/detail/source_transfer.h>

namespace riwo::http_exp::detail
{

class RIWO_HTTP_EXP_API server_exchange_state
{
	RIWO_DISABLE_COPY_MOVE(server_exchange_state)

public:
	using operation_handler_t = asio::any_completion_handler<void(error_code)>;
	using duplex_handler_t = asio::any_completion_handler<void(
		error_code,std::shared_ptr<duplex_state>)>;

	server_exchange_state(std::shared_ptr<stream> stream = {},
		request_head request = {}, parameter_view path_parameters = {},
		parameter_view query_parameters = {},
		std::shared_ptr<application_session_manager> sessions = {},
		std::filesystem::path resource_root = {},
		bool automatic_compression = false,
		uint64_t compression_minimum_size = 1024);
	~server_exchange_state();
	[[nodiscard]] protocol selected_protocol() const noexcept;
	[[nodiscard]] std::string remote_address() const;
	[[nodiscard]] uint16_t remote_port() const noexcept;
	[[nodiscard]] asio::ip::tcp::endpoint
	remote_endpoint(error_code &error) const noexcept;
	[[nodiscard]] asio::ip::tcp::endpoint
	local_endpoint(error_code &error) const noexcept;
	[[nodiscard]] stream_capabilities capabilities() const noexcept;
	[[nodiscard]] const request_head &request() const noexcept;
	[[nodiscard]] const parameter_view &path_parameters() const noexcept;
	[[nodiscard]] const parameter_view &query_parameters() const noexcept;
	[[nodiscard]] bool response_started() const noexcept;
	[[nodiscard]] application_session_ptr find_session();
	[[nodiscard]] application_session_ptr session();
	[[nodiscard]] const std::filesystem::path &resource_root() const noexcept;
	[[nodiscard]] bool should_compress(const response_head &head,
		optional<uint64_t> length) const noexcept;
	[[nodiscard]] bool prepare_compression(response_head &head,
		optional<uint64_t> length);
	[[nodiscard]] std::shared_ptr<body_reader_state> request_body() const noexcept;
	[[nodiscard]] std::shared_ptr<body_writer_state> response_body() const noexcept;
	[[nodiscard]] void *protocol_extension(
		const std::type_info &type) noexcept;
	void respond(response_head head, body_spec spec, error_code &error) noexcept;
	void respond(response_head head, body_spec spec, operation_handler_t handler);
	void inform(response_head head, error_code &error) noexcept;
	void inform(response_head head, operation_handler_t handler);
	[[nodiscard]] std::shared_ptr<duplex_state> accept_duplex(
		response_head head, error_code &error) noexcept;
	void accept_duplex(response_head head, duplex_handler_t handler);
	void cancel() noexcept;

private:
	class impl;
	std::shared_ptr<impl> m_impl;
};

[[nodiscard]] RIWO_HTTP_EXP_API
std::shared_ptr<server_exchange_state> make_server_exchange_state(
	std::shared_ptr<stream> stream = {}, request_head request = {},
	parameter_view path_parameters = {}, parameter_view query_parameters = {},
	std::shared_ptr<application_session_manager> sessions = {},
	std::filesystem::path resource_root = {},
	bool automatic_compression = false,
	uint64_t compression_minimum_size = 1024);

} //namespace riwo::http_exp::detail

namespace riwo::http_exp
{

template <core_concepts::exec Exec>
class basic_server_exchange<Exec>::impl
{
public:
	impl(executor_t executor_value, std::shared_ptr<detail::stream> stream,
		request_t request, parameter_view path_parameters = {},
		parameter_view query_parameters = {},
		std::shared_ptr<application_session_manager> sessions = {},
		std::filesystem::path resource_root = {},
		bool automatic_compression = false,
		uint64_t compression_minimum_size = 1024) :
		m_executor(std::move(executor_value)),
		m_state(detail::make_server_exchange_state(
			std::move(stream), std::move(request), std::move(path_parameters),
			std::move(query_parameters), std::move(sessions),
			std::move(resource_root), automatic_compression,
			compression_minimum_size)),
		m_request_body(std::make_unique<body_reader_t>(
			m_executor, m_state->request_body())),
		m_response_body(std::make_unique<body_writer_t>(
			m_executor, m_state->response_body()))
	{
	}

	executor_t m_executor;
	std::shared_ptr<detail::server_exchange_state> m_state;
	std::unique_ptr<body_reader_t> m_request_body;
	std::unique_ptr<body_writer_t> m_response_body;
};

template <core_concepts::exec Exec>
basic_server_exchange<Exec>::basic_server_exchange()
	requires core_concepts::match_sched<io_executor_t,executor_t> :
	basic_server_exchange(executor_t(riwo::get_executor()))
{
}

template <core_concepts::exec Exec>
basic_server_exchange<Exec>::basic_server_exchange(
	std::shared_ptr<detail::stream> stream, request_t request_value)
	requires core_concepts::match_sched<io_executor_t,executor_t> :
	basic_server_exchange(executor_t(riwo::get_executor()),
		std::move(stream), std::move(request_value))
{
}

template <core_concepts::exec Exec>
template <typename Exec0>
basic_server_exchange<Exec>::basic_server_exchange(Exec0 &&executor)
	requires core_concepts::match_sched<Exec0,executor_t> :
	basic_server_exchange(std::forward<Exec0>(executor), {}, {})
{
}

template <core_concepts::exec Exec>
basic_server_exchange<Exec>::~basic_server_exchange() {
	if( m_impl->m_state )
		m_impl->m_state->cancel();
}

template <core_concepts::exec Exec>
template <typename Exec0>
basic_server_exchange<Exec>::basic_server_exchange(Exec0 &&executor,
	std::shared_ptr<detail::stream> stream, request_t request_value)
	requires core_concepts::match_sched<Exec0,executor_t> :
	m_impl(std::make_shared<impl>(
		get_executor_helper(std::forward<Exec0>(executor)),
		std::move(stream), std::move(request_value)))
{
}

template <core_concepts::exec Exec>
template <typename Exec0>
basic_server_exchange<Exec>::basic_server_exchange(Exec0 &&executor,
	std::shared_ptr<detail::stream> stream, request_t request_value,
	parameter_view path_parameters, parameter_view query_parameters,
	std::shared_ptr<application_session_manager> sessions,
	std::filesystem::path resource_root, bool automatic_compression,
	uint64_t compression_minimum_size)
	requires core_concepts::match_sched<Exec0,executor_t> :
	m_impl(std::make_shared<impl>(
		get_executor_helper(std::forward<Exec0>(executor)),
		std::move(stream), std::move(request_value),
		std::move(path_parameters), std::move(query_parameters),
		std::move(sessions), std::move(resource_root), automatic_compression,
		compression_minimum_size))
{
}

template <core_concepts::exec Exec>
auto basic_server_exchange<Exec>::get_executor() const noexcept -> executor_t {
	return m_impl->m_executor;
}

template <core_concepts::exec Exec>
protocol basic_server_exchange<Exec>::selected_protocol() const noexcept {
	return m_impl->m_state->selected_protocol();
}

template <core_concepts::exec Exec>
std::string basic_server_exchange<Exec>::remote_address() const {
	return m_impl->m_state->remote_address();
}

template <core_concepts::exec Exec>
uint16_t basic_server_exchange<Exec>::remote_port() const noexcept {
	return m_impl->m_state->remote_port();
}

template <core_concepts::exec Exec>
asio::ip::tcp::endpoint basic_server_exchange<Exec>::remote_endpoint(
	error_code &error) const noexcept {
	return m_impl->m_state->remote_endpoint(error);
}

template <core_concepts::exec Exec>
asio::ip::tcp::endpoint basic_server_exchange<Exec>::local_endpoint(
	error_code &error) const noexcept {
	return m_impl->m_state->local_endpoint(error);
}

template <core_concepts::exec Exec>
auto basic_server_exchange<Exec>::request() const noexcept -> const request_t& {
	return m_impl->m_state->request();
}

template <core_concepts::exec Exec>
const parameter_view &basic_server_exchange<Exec>::path_parameters()
	const noexcept { return m_impl->m_state->path_parameters(); }

template <core_concepts::exec Exec>
const parameter_view &basic_server_exchange<Exec>::query_parameters()
	const noexcept { return m_impl->m_state->query_parameters(); }

template <core_concepts::exec Exec>
optional<std::string_view> basic_server_exchange<Exec>::path_parameter(
	std::string_view name) const noexcept {
	return path_parameters().find_first(name);
}

template <core_concepts::exec Exec>
optional<std::string_view> basic_server_exchange<Exec>::query_parameter(
	std::string_view name) const noexcept {
	return query_parameters().find_first(name);
}

template <core_concepts::exec Exec>
bool basic_server_exchange<Exec>::response_started() const noexcept {
	return m_impl->m_state->response_started();
}

template <core_concepts::exec Exec>
application_session_ptr basic_server_exchange<Exec>::find_session() {
	return m_impl->m_state->find_session();
}

template <core_concepts::exec Exec>
application_session_ptr basic_server_exchange<Exec>::session() {
	return m_impl->m_state->session();
}

template <core_concepts::exec Exec>
const std::filesystem::path &basic_server_exchange<Exec>::resource_root()
	const noexcept { return m_impl->m_state->resource_root(); }

template <core_concepts::exec Exec>
auto basic_server_exchange<Exec>::request_body() noexcept -> body_reader_t& {
	return *m_impl->m_request_body;
}

template <core_concepts::exec Exec>
auto basic_server_exchange<Exec>::response_body() noexcept -> body_writer_t& {
	return *m_impl->m_response_body;
}

template <core_concepts::exec Exec>
template <typename Token>
auto basic_server_exchange<Exec>::save_file(std::filesystem::path path,
	Token &&token)
	requires core_concepts::dis_detached_tf_opt_token<
		Token,error_code,uint64_t>
{
	return detail::pipe_body(request_body(), file_sink(std::move(path)), m_impl,
		std::forward<Token>(token));
}

template <core_concepts::exec Exec>
template <typename File, typename Token>
auto basic_server_exchange<Exec>::save_file(File &&file, Token &&token)
	requires (not std::same_as<std::remove_cvref_t<File>,
		std::filesystem::path>)
		and std::constructible_from<file_sink,File>
		and core_concepts::dis_detached_tf_opt_token<
			Token,error_code,uint64_t>
{
	return detail::pipe_body(request_body(),
		file_sink(std::forward<File>(file)), m_impl,
		std::forward<Token>(token));
}

template <core_concepts::exec Exec>
template <typename Token>
auto basic_server_exchange<Exec>::respond(body_spec spec, Token &&token)
	requires core_concepts::tf_opt_token<Token,error_code>
{
	return respond(response_head{}, std::move(spec),
		std::forward<Token>(token));
}

template <core_concepts::exec Exec>
template <typename Token>
auto basic_server_exchange<Exec>::respond(response_head head, body_spec spec,
	Token &&token)
	requires core_concepts::tf_opt_token<Token,error_code>
{
	if constexpr( is_error_code_token_v<Token> )
		m_impl->m_state->respond(std::move(head), std::move(spec), token);
	else if constexpr( is_sync_opt_token_v<Token> )
	{
		error_code error;
		m_impl->m_state->respond(std::move(head), std::move(spec), error);
		if( error )
			(void)expected_value_or_throw(sys_expected<std::monostate>(
				sys_unexpected(error)));
	}
	else
	{
		auto state = m_impl->m_state;
		return initiate_io_void(m_impl->m_executor,
		[state = std::move(state), head = std::move(head), spec = std::move(spec)]
		(auto handler) mutable {
			state->respond(std::move(head), std::move(spec),
				typename detail::server_exchange_state::operation_handler_t(
					std::move(handler)));
		}, std::forward<Token>(token));
	}
}

template <core_concepts::exec Exec>
template <typename Token>
auto basic_server_exchange<Exec>::respond(const const_buffer &body,
	Token &&token)
	requires core_concepts::tf_opt_token<Token,error_code>
{
	return respond(response_head{}, body, std::forward<Token>(token));
}

template <core_concepts::exec Exec>
template <typename Token>
auto basic_server_exchange<Exec>::respond(response_head head,
	const const_buffer &body, Token &&token)
	requires core_concepts::tf_opt_token<Token,error_code>
{
	if( m_impl->m_state->should_compress(head, body.size()) )
		return respond(std::move(head), memory_body_source(body),
			std::forward<Token>(token));
	const bool head_only = head.status.allows_body() and
		m_impl->m_state->request().method.known() == method::head;
	auto spec = head_only ? body_spec::head(body.size()) :
		body_spec::known(body.size());
	if constexpr( is_error_code_token_v<Token> )
	{
		respond(std::move(head), std::move(spec), token);
		if( token or head_only )
			return ;
		(void)response_body().write(body, token);
		if( token )
			return ;
		response_body().finish({}, token);
	}
	else if constexpr( is_sync_opt_token_v<Token> )
	{
		respond(std::move(head), std::move(spec));
		if( head_only )
			return ;
		(void)response_body().write(body);
		response_body().finish();
	}
	else
	{
		using token_t = std::remove_cvref_t<Token>;
		if constexpr( is_detached_v<token_unbound_t<token_t>> )
		{
			auto owned = std::make_shared<std::vector<std::byte>>(body.size());
			if( body.size() != 0 )
				std::memcpy(owned->data(), body.data(), body.size());
			return initiate_io_void(m_impl->m_executor,
			[this, head = std::move(head), owned, head_only](auto handler) mutable
			{
				auto owned_spec = head_only ? body_spec::head(owned->size()) :
					body_spec::known(owned->size());
				respond(std::move(head), std::move(owned_spec),
				[this, owned, head_only, handler = std::move(handler)]
				(error_code error) mutable
				{
					if( error )
					{
						std::move(handler)(error);
						return ;
					}
					if( head_only )
					{
						std::move(handler)(error_code{});
						return ;
					}
					if( owned->empty() )
					{
						response_body().finish({}, std::move(handler));
						return ;
					}
					response_body().write(riwo::buffer(*owned),
					[this, owned, handler = std::move(handler)]
					(error_code write_error, size_t) mutable
					{
						if( write_error )
						{
							std::move(handler)(write_error);
							return ;
						}
						response_body().finish({}, std::move(handler));
					});
				});
			}, std::forward<Token>(token));
		}
		else
		{
			return initiate_io_void(m_impl->m_executor,
			[this, head = std::move(head), body, head_only](auto handler) mutable
			{
				auto borrowed_spec = head_only ? body_spec::head(body.size()) :
					body_spec::known(body.size());
				respond(std::move(head), std::move(borrowed_spec),
				[this, body, head_only, handler = std::move(handler)]
				(error_code error) mutable
				{
					if( error )
					{
						std::move(handler)(error);
						return ;
					}
					if( head_only )
					{
						std::move(handler)(error_code{});
						return ;
					}
					if( body.size() == 0 )
					{
						response_body().finish({}, std::move(handler));
						return ;
					}
					response_body().write(body,
					[this, handler = std::move(handler)]
					(error_code write_error, size_t) mutable
					{
						if( write_error )
						{
							std::move(handler)(write_error);
							return ;
						}
						response_body().finish({}, std::move(handler));
					});
				});
			}, std::forward<Token>(token));
		}
	}
}

template <core_concepts::exec Exec>
template <typename Source, typename Token>
auto basic_server_exchange<Exec>::respond(Source &&source, Token &&token)
	requires concepts::body_source_model<std::remove_cvref_t<Source>>
		and core_concepts::tf_opt_token<Token,error_code>
{
	return respond(response_head{}, std::forward<Source>(source),
		std::forward<Token>(token));
}

template <core_concepts::exec Exec>
template <typename Source, typename Token>
auto basic_server_exchange<Exec>::respond(response_head head,
	Source &&source_value, Token &&token)
	requires concepts::body_source_model<std::remove_cvref_t<Source>>
		and core_concepts::tf_opt_token<Token,error_code>
{
	auto source = detail::make_body_source(std::forward<Source>(source_value));
	auto length = source->size();
	if( m_impl->m_state->prepare_compression(head, length) )
	{
		source = detail::make_gzip_body_source(std::move(source));
		length.reset();
	}
	const bool head_only = head.status.allows_body() and
		m_impl->m_state->request().method.known() == method::head;
	auto spec = head_only ?
		(length ? body_spec::head(*length) : body_spec::empty()) :
		(length ? body_spec::known(*length) : body_spec::streaming());
	if constexpr( is_error_code_token_v<Token> or is_sync_opt_token_v<Token> )
	{
		error_code error;
		m_impl->m_state->respond(std::move(head), spec, error);
		if( not error and head_only )
			source->finish_sync(error);
		else if( not error )
		{
			detail::source_transfer_options options {
				.length = length, .trailers = {}, .progress = {}
			};
			(void)detail::transfer_source_sync(*source,
				*m_impl->m_state->response_body(),
				std::move(options), error);
		}
		if( error )
		{
			source->cancel();
			m_impl->m_state->cancel();
		}
		if constexpr( is_error_code_token_v<Token> )
			token = error;
		else if( error )
			(void)expected_value_or_throw(sys_expected<std::monostate>(
				sys_unexpected(error)));
	}
	else
	{
		struct operation : std::enable_shared_from_this<operation>
		{
			std::shared_ptr<impl> owner;
			std::shared_ptr<detail::body_source> source;
			response_head head;
			body_spec spec = body_spec::empty();
			optional<uint64_t> length;
			bool head_only = false;
			asio::any_completion_handler<void(error_code)> handler;

			void start()
			{
				auto self = this->shared_from_this();
				owner->m_state->respond(std::move(head), spec,
				[self = std::move(self)](error_code error) mutable {
					if( error )
						self->fail(error);
					else if( self->head_only )
						self->finish_head_source();
					else
						self->transfer_body_source();
				});
			}

			void finish_head_source()
			{
				auto self = this->shared_from_this();
				source->finish(
				[self = std::move(self)](error_code error) mutable {
					if( error )
						self->fail(error);
					else
						self->complete({});
				});
			}

			void transfer_body_source()
			{
				auto self = this->shared_from_this();
				detail::source_transfer_options options {
					.length = length, .trailers = {}, .progress = {}
				};
				detail::transfer_source(source, owner->m_state->response_body(),
					std::move(options),
				[self = std::move(self)](error_code error, uint64_t) mutable {
					if( error )
						self->fail(error);
					else
						self->complete({});
				});
			}

			void fail(error_code error)
			{
				source->cancel();
				owner->m_state->cancel();
				complete(error);
			}

			void complete(error_code error) {
				std::move(handler)(error);
			}
		};

		return initiate_io_void(m_impl->m_executor,
		[owner = m_impl, source = std::move(source), head = std::move(head),
		 spec = std::move(spec), length, head_only](auto handler) mutable {
			auto value = std::make_shared<operation>();
			value->owner = std::move(owner);
			value->source = std::move(source);
			value->head = std::move(head);
			value->spec = std::move(spec);
			value->length = length;
			value->head_only = head_only;
			value->handler = decltype(value->handler)(std::move(handler));
			value->start();
		}, std::forward<Token>(token));
	}
}

template <core_concepts::exec Exec>
template <typename Token>
auto basic_server_exchange<Exec>::respond_file(std::filesystem::path path,
	Token &&token)
	requires core_concepts::tf_opt_token<Token,error_code>
{
	return respond_file(std::move(path), file_response_options{},
		std::forward<Token>(token));
}

template <core_concepts::exec Exec>
template <typename Token>
auto basic_server_exchange<Exec>::respond_file(std::filesystem::path path,
	file_response_options options, Token &&token)
	requires core_concepts::tf_opt_token<Token,error_code>
{
	if( path.is_relative() )
	{
		auto resolved = resolve_resource_path(resource_root(), path);
		if( not resolved )
			throw std::system_error(std::error_code(resolved.error()));
		path = std::move(*resolved);
	}
	file_response_source source(std::move(path), request(), std::move(options));
	auto head = source.response();
	if( not source.has_body() )
		return respond(std::move(head), body_spec::empty(),
			std::forward<Token>(token));
	return respond(std::move(head), std::move(source),
		std::forward<Token>(token));
}

template <core_concepts::exec Exec>
template <typename Token>
auto basic_server_exchange<Exec>::inform(response_head head, Token &&token)
	requires core_concepts::tf_opt_token<Token,error_code>
{
	if constexpr( is_error_code_token_v<Token> )
		m_impl->m_state->inform(std::move(head), token);
	else if constexpr( is_sync_opt_token_v<Token> )
	{
		error_code error;
		m_impl->m_state->inform(std::move(head), error);
		if( error )
			(void)expected_value_or_throw(sys_expected<std::monostate>(
				sys_unexpected(error)));
	}
	else
	{
		auto state = m_impl->m_state;
		return initiate_io_void(m_impl->m_executor,
		[state = std::move(state), head = std::move(head)]
		(auto handler) mutable {
			state->inform(std::move(head),
				typename detail::server_exchange_state::operation_handler_t(
					std::move(handler)));
		}, std::forward<Token>(token));
	}
}

template <core_concepts::exec Exec>
template <typename Token>
auto basic_server_exchange<Exec>::accept_duplex(response_head head,
	Token &&token)
	requires core_concepts::dis_detached_tf_opt_token<
		Token,error_code,basic_duplex_channel<executor_t>>
{
	using channel_t = basic_duplex_channel<executor_t>;
	if constexpr( is_error_code_token_v<Token> )
	{
		auto state = m_impl->m_state->accept_duplex(std::move(head), token);
		if( not token )
		{
			m_impl->m_request_body->detach();
			m_impl->m_response_body->detach();
		}
		return channel_t(m_impl->m_executor, std::move(state));
	}
	else if constexpr( is_sync_opt_token_v<Token> )
	{
		error_code error;
		auto state = m_impl->m_state->accept_duplex(std::move(head), error);
		if( error )
			return expected_value_or_throw(sys_expected<channel_t>(
				sys_unexpected(error)));
		m_impl->m_request_body->detach();
		m_impl->m_response_body->detach();
		return channel_t(m_impl->m_executor, std::move(state));
	}
	else
	{
		auto owner = m_impl;
		auto executor = m_impl->m_executor;
		return initiate_io<channel_t>(executor,
		[owner = std::move(owner), executor,
		 head = std::move(head)](auto handler) mutable {
			owner->m_state->accept_duplex(std::move(head),
				[owner = std::move(owner), executor,
				 handler = std::move(handler)]
				(error_code error,
				 std::shared_ptr<detail::duplex_state> channel) mutable {
					if( not error )
					{
						owner->m_request_body->detach();
						owner->m_response_body->detach();
					}
					std::move(handler)(error,
						channel_t(executor, std::move(channel)));
				});
		}, std::forward<Token>(token));
	}
}

template <core_concepts::exec Exec>
void basic_server_exchange<Exec>::cancel() noexcept {
	m_impl->m_state->cancel();
}

template <core_concepts::exec Exec>
stream_capabilities basic_server_exchange<Exec>::capabilities() const noexcept {
	return m_impl->m_state->capabilities();
}

template <core_concepts::exec Exec>
template <typename Extension>
Extension *basic_server_exchange<Exec>::extension() noexcept {
	return static_cast<Extension *>(
		m_impl->m_state->protocol_extension(typeid(Extension)));
}

} //namespace riwo::http_exp

#endif //RIWO_HTTP_EXP_SERVER_DETAIL_EXCHANGE_H
