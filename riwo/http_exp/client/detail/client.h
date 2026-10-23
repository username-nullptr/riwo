// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_CLIENT_DETAIL_CLIENT_H
#define RIWO_HTTP_EXP_CLIENT_DETAIL_CLIENT_H

#include <riwo/http_exp/client/detail/connector_adapter.h>

namespace riwo::http_exp::detail
{

class stream;

struct send_result
{
	protocol selected_protocol = protocol::v1_1;
	protocol reply_protocol = protocol::v1_1;
	reply_head head {};
	std::shared_ptr<body_reader_state> body;
	std::shared_ptr<void> owner;
	uint64_t max_buffered_body = 8U * 1024U * 1024U;
};

class RIWO_HTTP_EXP_API client_state :
	public std::enable_shared_from_this<client_state>
{
	RIWO_DISABLE_COPY_MOVE(client_state)

public:
	using open_handler_t = asio::any_completion_handler<void(
		error_code,std::shared_ptr<stream>)>;
	using send_handler_t = asio::any_completion_handler<void(
		error_code,send_result)>;

	client_state(client_config config,
		std::shared_ptr<client_session> session = {},
		std::shared_ptr<connector> connector = {});
	client_state(asio::any_io_executor executor, client_config config,
		std::shared_ptr<client_session> session = {},
		std::shared_ptr<connector> connector = {});
	~client_state();
	[[nodiscard]] std::shared_ptr<stream> open(request request_value,
		body_spec spec, error_code &error) noexcept;
	void open(request request_value, body_spec spec, open_handler_t handler);
	[[nodiscard]] send_result send(request request_value,
		error_code &error) noexcept;
	void send(request request_value, send_handler_t handler);
	[[nodiscard]] std::shared_ptr<cookie_jar> cookie_store() const noexcept;
	void capture_cookies(const riwo::url &origin,
		const reply_head &head) noexcept;

private:
	class impl;
	std::shared_ptr<impl> m_impl;
};

[[nodiscard]] RIWO_HTTP_EXP_API
std::shared_ptr<client_state> make_client_state(
	client_config config,
	std::shared_ptr<client_session> session = {},
	std::shared_ptr<connector> connector = {});

[[nodiscard]] RIWO_HTTP_EXP_API
std::shared_ptr<client_state> make_client_state(
	asio::any_io_executor executor, client_config config,
	std::shared_ptr<client_session> session = {},
	std::shared_ptr<connector> connector = {});

} //namespace riwo::http_exp::detail

namespace riwo::http_exp
{

template <core_concepts::exec Exec>
class basic_client<Exec>::impl
{
public:
	impl(executor_t executor_value, config_t config_value,
		std::shared_ptr<detail::client_session> session,
		detail::connector_ptr_t connector = {}) :
		m_executor(std::move(executor_value)), m_session(std::move(session)),
		m_connector(std::move(connector))
	{
		set_config(std::move(config_value));
	}

	void set_config(config_t config)
	{
		m_config = std::move(config);
		if( m_config.cookies.enabled and not m_config.cookies.jar )
			m_config.cookies.jar = std::make_shared<cookie_jar>();
		else if( not m_config.cookies.enabled )
			m_config.cookies.jar.reset();
		m_state = detail::make_client_state(asio::any_io_executor(m_executor),
			m_config, m_session, m_connector);
	}

	executor_t m_executor;
	config_t m_config;
	std::shared_ptr<detail::client_session> m_session;
	detail::connector_ptr_t m_connector;
	std::shared_ptr<detail::client_state> m_state;
};

template <core_concepts::exec Exec>
basic_client<Exec>::basic_client(config_t config)
	requires core_concepts::match_sched<io_executor_t,executor_t> :
	basic_client(executor_t(riwo::get_executor()), std::move(config))
{
}

template <core_concepts::exec Exec>
basic_client<Exec>::basic_client(
	std::shared_ptr<detail::client_session> session, config_t config)
	requires core_concepts::match_sched<io_executor_t,executor_t> :
	basic_client(executor_t(riwo::get_executor()), std::move(session),
		std::move(config))
{
}

template <core_concepts::exec Exec>
basic_client<Exec>::basic_client(
	std::shared_ptr<basic_transport_connector<executor_t>> connector,
	config_t config)
	requires core_concepts::match_sched<io_executor_t,executor_t> :
	basic_client(executor_t(riwo::get_executor()), std::move(connector),
		std::move(config))
{
}

template <core_concepts::exec Exec>
template <typename Exec0>
basic_client<Exec>::basic_client(Exec0 &&executor, config_t config)
	requires (
		not std::same_as<std::remove_cvref_t<Exec0>,basic_client> and
		core_concepts::match_sched<Exec0,executor_t>) :
	basic_client(std::forward<Exec0>(executor),
		std::shared_ptr<detail::client_session>{}, std::move(config))
{
}

template <core_concepts::exec Exec>
template <typename Exec0>
basic_client<Exec>::basic_client(Exec0 &&executor,
	std::shared_ptr<detail::client_session> session, config_t config)
	requires (
		not std::same_as<std::remove_cvref_t<Exec0>,basic_client> and
		core_concepts::match_sched<Exec0,executor_t>) :
	m_impl(std::make_shared<impl>(
		get_executor_helper(std::forward<Exec0>(executor)),
		std::move(config), std::move(session)))
{
}

template <core_concepts::exec Exec>
template <typename Exec0>
basic_client<Exec>::basic_client(Exec0 &&executor,
	std::shared_ptr<basic_transport_connector<executor_t>> connector,
	config_t config)
	requires (
		not std::same_as<std::remove_cvref_t<Exec0>,basic_client> and
		core_concepts::match_sched<Exec0,executor_t>)
{
	if( not connector )
		throw std::invalid_argument(
		"riwo::http_exp::basic_client: connector is null");
	auto selected_executor = get_executor_helper(
		std::forward<Exec0>(executor));
	auto adapter = detail::make_connector_adapter(
		asio::any_io_executor(selected_executor), std::move(connector));
	m_impl = std::make_shared<impl>(std::move(selected_executor),
		std::move(config), std::shared_ptr<detail::client_session>{},
		std::move(adapter));
}

template <core_concepts::exec Exec>
auto basic_client<Exec>::get_executor() const noexcept -> executor_t {
	return m_impl->m_executor;
}

template <core_concepts::exec Exec>
auto basic_client<Exec>::set_config(config_t config) -> basic_client& {
	m_impl->set_config(std::move(config));
	return *this;
}

template <core_concepts::exec Exec>
auto basic_client<Exec>::config() const -> config_t {
	return m_impl->m_config;
}

template <core_concepts::exec Exec>
std::shared_ptr<cookie_jar> basic_client<Exec>::cookie_store() const noexcept {
	return m_impl->m_state->cookie_store();
}

template <core_concepts::exec Exec>
template <typename Token>
auto basic_client<Exec>::send(request request_value, Token &&token)
	requires core_concepts::dis_detached_tf_opt_token<
		Token,error_code,reply_t>
{
	if constexpr( is_error_code_token_v<Token> )
	{
		auto result = m_impl->m_state->send(std::move(request_value), token);
		return token ? reply_t(m_impl->m_executor) : reply_t(m_impl->m_executor,
			result.selected_protocol, std::move(result.head), std::move(result.body),
			{}, std::move(result.owner), result.max_buffered_body,
			result.reply_protocol);
	}
	else if constexpr( is_sync_opt_token_v<Token> )
	{
		error_code error;
		auto result = m_impl->m_state->send(std::move(request_value), error);
		if( error )
			return expected_value_or_throw(sys_expected<reply_t>(
				sys_unexpected(error)));
		return reply_t(m_impl->m_executor, result.selected_protocol,
			std::move(result.head), std::move(result.body), {},
			std::move(result.owner), result.max_buffered_body,
			result.reply_protocol);
	}
	else
	{
		auto state = m_impl->m_state;
		auto executor = m_impl->m_executor;
		return initiate_io<reply_t>(m_impl->m_executor,
		[state = std::move(state), executor,
		 request_value = std::move(request_value)](auto handler) mutable
		{
			state->send(std::move(request_value),
			[handler = std::move(handler), executor]
			(error_code error, detail::send_result result) mutable
			{
				if( error )
				{
					std::move(handler)(error, reply_t(executor));
					return ;
				}
				std::move(handler)(error_code{}, reply_t(executor,
					result.selected_protocol, std::move(result.head),
					std::move(result.body), {}, std::move(result.owner),
					result.max_buffered_body, result.reply_protocol));
			});
		}, std::forward<Token>(token));
	}
}

template <core_concepts::exec Exec>
template <typename Token>
auto basic_client<Exec>::fetch(request request_value, Token &&token)
	requires core_concepts::dis_detached_tf_opt_token<
		Token,error_code,reply_t>
{
	if constexpr( is_error_code_token_v<Token> )
	{
		auto result = m_impl->m_state->send(std::move(request_value), token);
		if( token )
			return reply_t(m_impl->m_executor);
		reply_t active(m_impl->m_executor, result.selected_protocol,
			result.head, result.body, {}, result.owner,
			result.max_buffered_body, result.reply_protocol);
		auto bytes = active.read_all(token);
		if( token )
			return reply_t(m_impl->m_executor);
		return reply_t(m_impl->m_executor, result.selected_protocol,
			std::move(result.head), std::move(result.body), std::move(bytes),
			std::move(result.owner), result.max_buffered_body,
			result.reply_protocol);
	}
	else if constexpr( is_sync_opt_token_v<Token> )
	{
		error_code error;
		auto result = m_impl->m_state->send(std::move(request_value), error);
		if( error )
			return expected_value_or_throw(sys_expected<reply_t>(
				sys_unexpected(error)));
		reply_t active(m_impl->m_executor, result.selected_protocol,
			result.head, result.body, {}, result.owner,
			result.max_buffered_body, result.reply_protocol);
		auto bytes = active.read_all();
		return reply_t(m_impl->m_executor, result.selected_protocol,
			std::move(result.head), std::move(result.body), std::move(bytes),
			std::move(result.owner), result.max_buffered_body,
			result.reply_protocol);
	}
	else
	{
		auto state = m_impl->m_state;
		auto executor = m_impl->m_executor;
		return initiate_io<reply_t>(m_impl->m_executor,
		[state = std::move(state), executor,
		 request_value = std::move(request_value)](auto handler) mutable
		{
			state->send(std::move(request_value),
			[handler = std::move(handler), executor]
			(error_code error, detail::send_result result) mutable
			{
				if( error )
				{
					std::move(handler)(error, reply_t(executor));
					return ;
				}
				auto active = std::make_shared<reply_t>(executor,
					result.selected_protocol, result.head, result.body,
					std::vector<std::byte>{}, result.owner,
					result.max_buffered_body, result.reply_protocol);
				active->read_all(
				[handler = std::move(handler), executor, active,
				 result = std::move(result)]
				(error_code read_error, std::vector<std::byte> bytes) mutable
				{
					if( read_error )
					{
						std::move(handler)(read_error, reply_t(executor));
						return ;
					}
					std::move(handler)(error_code{}, reply_t(executor,
						result.selected_protocol, std::move(result.head),
						std::move(result.body), std::move(bytes),
						std::move(result.owner), result.max_buffered_body,
						result.reply_protocol));
				});
			});
		}, std::forward<Token>(token));
	}
}

template <core_concepts::exec Exec>
template <typename Token>
auto basic_client<Exec>::open(request request_value, body_spec spec,
	Token &&token)
	requires core_concepts::dis_detached_tf_opt_token<
		Token,error_code,exchange_t>
{
	auto origin = request_value.url();
	auto cookies = m_impl->m_state->cookie_store();
	if constexpr( is_error_code_token_v<Token> )
	{
		auto stream = m_impl->m_state->open(
			std::move(request_value), std::move(spec), token);
		return exchange_t(m_impl->m_executor, std::move(stream),
			std::move(cookies), std::move(origin));
	}
	else if constexpr( is_sync_opt_token_v<Token> )
	{
		error_code error;
		auto stream = m_impl->m_state->open(
			std::move(request_value), std::move(spec), error);
		if( error )
			return expected_value_or_throw(sys_expected<exchange_t>(
				sys_unexpected(error)));
		return exchange_t(m_impl->m_executor, std::move(stream),
			std::move(cookies), std::move(origin));
	}
	else
	{
		auto state = m_impl->m_state;
		auto executor = m_impl->m_executor;
		return initiate_io<exchange_t>(m_impl->m_executor,
		[state = std::move(state), executor, cookies = std::move(cookies),
		 origin = std::move(origin),
		 request_value = std::move(request_value), spec = std::move(spec)]
		(auto handler) mutable
		{
			state->open(std::move(request_value), std::move(spec),
			[handler = std::move(handler), executor,
			 cookies = std::move(cookies), origin = std::move(origin)]
			(error_code error, std::shared_ptr<detail::stream> stream) mutable
			{
				try {
					std::move(handler)(error,
						exchange_t(executor, std::move(stream),
							std::move(cookies), std::move(origin)));
				}
				catch(...) {
					std::move(handler)(exception_error(
						std::current_exception()), exchange_t(executor));
				}
			});
		}, std::forward<Token>(token));
	}
}

template <core_concepts::exec Exec>
template <typename Url, typename Token>
auto basic_client<Exec>::open(http_exp::method request_method, Url &&url,
	body_spec spec, Token &&token)
	requires core_concepts::dis_detached_tf_opt_token<
		Token,error_code,exchange_t>
{
	return open(request(std::move(request_method),
		riwo::url(std::forward<Url>(url))), std::move(spec),
		std::forward<Token>(token));
}

template <core_concepts::exec Exec>
template <typename Url, typename Token>
auto basic_client<Exec>::get(Url &&url, Token &&token)
	requires core_concepts::dis_detached_tf_opt_token<
		Token,error_code,reply_t>
{
	return send(request(method::get, riwo::url(std::forward<Url>(url))),
		std::forward<Token>(token));
}

template <core_concepts::exec Exec>
template <typename Url, typename Token>
auto basic_client<Exec>::post(Url &&url, const const_buffer &body,
	Token &&token)
	requires core_concepts::dis_detached_tf_opt_token<
		Token,error_code,reply_t>
{
	request value(method::post, riwo::url(std::forward<Url>(url)));
	value.body(body);
	return send(std::move(value), std::forward<Token>(token));
}

} //namespace riwo::http_exp

#endif //RIWO_HTTP_EXP_CLIENT_DETAIL_CLIENT_H
