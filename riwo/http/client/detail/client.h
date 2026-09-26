// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_CLIENT_DETAIL_CLIENT_H
#define RIWO_HTTP_CLIENT_DETAIL_CLIENT_H

#if defined(__GNUC__) && !defined(__clang__)
# pragma GCC diagnostic push
# pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif

#include <riwo/core/async_expected.h>
#include <riwo/http/client/detail/proxy.h>

namespace riwo::http
{

template <core_concepts::exec Exec, version_enum Version>
class RIWO_HTTP_TAPI basic_client<Exec,Version>::impl :
	public std::enable_shared_from_this<impl>
{
	RIWO_DISABLE_COPY_MOVE(impl)

	template <method_enum Method>
	using result_t = sys_expected<context_ptr<Method>>;
	using target_t = connection_pool_t::target_t;

	struct request_target
	{
		target_t connection {};
		bool forward_proxy = false;
	};

public:
	explicit impl(config_t config) requires
		core_concepts::match_sched<io_executor_t,executor_t> :
		m_pool(executor_t(riwo::get_executor())),
		m_cookie_store(std::make_shared<cookie_jar>()),
		m_config(std::move(config)) {}

	explicit impl(executor_t exec, config_t config) :
		m_pool(std::move(exec)), m_cookie_store(std::make_shared<cookie_jar>()),
		m_config(std::move(config)) {}

	explicit impl(connection_pool_t &&pool, config_t config) :
		m_pool(std::move(pool)), m_cookie_store(std::make_shared<cookie_jar>()),
		m_config(std::move(config)) {}

private:
	[[nodiscard]] static sys_expected<target_t>
	target_from_url(const url_t &value, bool no_delay) noexcept
	{
		try {
			if( not value.is_valid() or value.host().empty() or value.has_fragment() )
			{
				return sys_unexpected (
					make_system_error_code(std::errc::invalid_argument)
				);
			}
			auto scheme = strtls::to_lower(value.protocol());
			security_mode security {};
			auto port = value.port();

			if( scheme == "http" )
			{
				security = security_mode::plain;
				if( port == 0 )
					port = 80;
			}
			else if( scheme == "https" )
			{
				security = security_mode::tls;
				if( port == 0 )
					port = 443;
			}
			else
			{
				return sys_unexpected (
					make_system_error_code(std::errc::protocol_not_supported)
				);
			}
			return target_t {
				strtls::to_lower(value.host()), port, security, no_delay
			};
		}
		catch(const std::bad_alloc&)
		{
			return sys_unexpected (
				make_system_error_code(std::errc::not_enough_memory)
			);
		}
		catch(...) {}
		return sys_unexpected(make_system_error_code(std::errc::io_error));
	}

	[[nodiscard]] sys_expected<request_target>
	resolve_request_target(req_info &info) const noexcept
	{
		try {
			auto target = target_from_url(info.url, m_config.no_delay);
			if( not target )
				return sys_unexpected(target.error());

			const proxy_t &setting = info.proxy ? *info.proxy : m_config.default_proxy;
			auto proxy = detail::resolve_proxy(info.url, setting);
			if( not proxy )
				return sys_unexpected(proxy.error());

			request_target result {
				.connection = std::move(*target),
				.forward_proxy = false
			};
			if( proxy->forward )
			{
				auto forward_target = target_from_url(*proxy->forward, m_config.no_delay);
				if( not forward_target )
					return sys_unexpected(forward_target.error());

				result.connection = std::move(*forward_target);
				result.forward_proxy = true;

				if( proxy->authorization and
					info.arg.headers().find(header::proxy_authorization) == info.arg.headers().end() )
					info.arg.set_header(header::proxy_authorization, *proxy->authorization);
			}
			else if( proxy->tunnel )
				result.connection.tunnel = std::move(proxy->tunnel);
			return result;
		}
		catch(const std::bad_alloc&) {
			return sys_unexpected(make_system_error_code(std::errc::not_enough_memory));
		}
		catch(...) {}
		return sys_unexpected(make_system_error_code(std::errc::io_error));
	}

	void add_request_cookies(req_info &info)
	{
		for(auto &[cookie_name, cookie_item] : m_cookie_store->cookies_for(info.url))
		{
			if( not info.arg.contains_cookie(cookie_name) )
				info.arg.set_cookie(cookie_name, std::move(cookie_item));
		}
	}

	[[nodiscard]] static bool redirect_status(status_enum value) noexcept
	{
		return value == status::moved_permanently or value == status::found or
			   value == status::see_other or value == status::temporary_redirect or
			   value == status::permanent_redirect;
	}

	[[nodiscard]] static bool same_origin(const url_t &lhs, const url_t &rhs) noexcept
	{
		try {
			auto lhs_scheme = strtls::to_lower(lhs.protocol());
			auto rhs_scheme = strtls::to_lower(rhs.protocol());

			const auto effective_port = [](std::string_view scheme, uint16_t port)
			{
				if( port != 0 )
					return port;

				if( scheme == "http" )
					return static_cast<uint16_t>(80);

				if( scheme == "https" )
					return static_cast<uint16_t>(443);

				return static_cast<uint16_t>(0);
			};
			return lhs_scheme == rhs_scheme and
				   strtls::to_lower(lhs.host()) == strtls::to_lower(rhs.host()) and
				   effective_port(lhs_scheme, lhs.port()) == effective_port(rhs_scheme, rhs.port());
		}
		catch(...) {}
		return false;
	}

	[[nodiscard]] static bool expects_continue(const req_info &info) noexcept
	{
		if constexpr( version_v <= version::v10 )
			return false;
		else
		{
			try {
				auto it = info.arg.headers().find(header::expect);
				return it != info.arg.headers().end() and
					strtls::to_lower(*it->second) == "100-continue";
			}
			catch(...) {}
			return false;
		}
	}

	template <method_enum Method>
	[[nodiscard]] static asio::awaitable<std::tuple<error_code,context_ptr<Method>>,Exec>
	co_make_context(std::shared_ptr<impl> self, req_info request_info)
	{
		auto target_expected = self->resolve_request_target(request_info);
		if( not target_expected )
		{
			co_return std::tuple<error_code,context_ptr<Method>> {
				target_expected.error(), {}
			};
		}
		try {
			self->add_request_cookies(request_info);
		}
		catch(const std::bad_alloc&)
		{
			co_return std::tuple<error_code,context_ptr<Method>> {
				make_system_error_code(std::errc::not_enough_memory), {}
			};
		}
		catch(...)
		{
			co_return std::tuple<error_code,context_ptr<Method>> {
				make_system_error_code(std::errc::io_error), {}
			};
		}
		auto lease_result = co_await self->m_pool.get (
			target_expected->connection, asio::as_tuple(asio::use_awaitable_t<Exec>{})
		);
		auto &[lease_error, lease] = lease_result;
		if( lease_error )
		{
			co_return std::tuple<error_code,context_ptr<Method>> {
				lease_error, {}
			};
		}
		try {
			auto context = std::make_shared<context_t<Method>>(
			std::move(lease), std::move(request_info.url), typename context_t<Method>::options
			{
				std::move(request_info.arg), self->m_cookie_store,
				target_expected->forward_proxy ?
					request_target_form::absolute : request_target_form::origin,
				request_info.auto_decompression
			});
			co_return std::tuple<error_code,context_ptr<Method>> {
				error_code{}, std::move(context)
			};
		}
		catch(const std::system_error &exception)
		{
			co_return std::tuple<error_code,context_ptr<Method>> {
				exception.code(), {}
			};
		}
		catch(const std::bad_alloc&)
		{
			co_return std::tuple<error_code,context_ptr<Method>> {
				make_system_error_code(std::errc::not_enough_memory), {}
			};
		}
		catch(...) {}
		co_return std::tuple<error_code,context_ptr<Method>> {
			make_system_error_code(std::errc::io_error), {}
		};
	}

	template <method_enum Method>
	[[nodiscard]] result_t<Method> follow_redirects(context_ptr<Method> current, req_info info) noexcept
	{
		static_assert (
			Method == method::get or Method == method::head
		);
		for(size_t followed=0; ; followed++)
		{
			error_code io_error {};
			auto reply_status = current->wait_reply(io_error);
			if( io_error )
				return sys_unexpected(io_error);

			while( current->reply()->parser().is_informational() )
			{
				reply_status = current->wait_reply(io_error);
				if( io_error )
					return sys_unexpected(io_error);
			}
			if( not redirect_status(reply_status) or followed == info.max_redirects )
				return current;

			auto location = current->reply()->header(header::location);
			if( not location )
				return current;

			if constexpr( Method == method::get )
			{
				std::array<char,8192> data {};
				for(;;)
				{
					ignore_unused(current->reply()->read(buffer(data), io_error));
					if( not io_error )
						continue;

					if( io_error != errc::eof )
						return sys_unexpected(io_error);
					break;
				}
			}
			try {
				auto next = url_t::resolve(info.url, **location);
				if( not same_origin(info.url, next) )
				{
					info.arg.unset_header(header::authorization);
					info.arg.cookies().clear();
				}
				info.url = std::move(next);
			}
			catch(...)
			{
				return sys_unexpected (
					make_system_error_code(std::errc::protocol_error)
				);
			}
			auto next = make_context<Method>(info);
			if( not next )
				return next;

			current = std::move(*next);
			ignore_unused(current->write(io_error));

			if( io_error )
				return sys_unexpected(io_error);
		}
		return {};
	}

	template <method_enum Method>
	[[nodiscard]] static asio::awaitable<std::tuple<error_code,context_ptr<Method>>,Exec>
	co_follow_redirects(std::shared_ptr<impl> self, context_ptr<Method> active_context, req_info request_info)
	{
		static_assert (
			Method == method::get or Method == method::head
		);
		for(size_t followed = 0; ; ++followed)
		{
			auto [wait_error, reply_status] =
				co_await detail::reply_access::wait(*active_context->reply());
			if( wait_error )
			{
				co_return std::tuple<error_code,context_ptr<Method>> {
					wait_error, {}
				};
			}
			while( active_context->reply()->parser().is_informational() )
			{
				auto [informational_error, informational_status] =
					co_await detail::reply_access::wait(*active_context->reply());

				if( informational_error )
				{
					co_return std::tuple<error_code,context_ptr<Method>> {
						informational_error, {}
					};
				}
				reply_status = informational_status;
			}
			if( not redirect_status(reply_status) or followed == request_info.max_redirects )
			{
				co_return std::tuple<error_code,context_ptr<Method>> {
					error_code{}, std::move(active_context)
				};
			}
			auto location = active_context->reply()->header(header::location);
			if( not location )
			{
				co_return std::tuple<error_code,context_ptr<Method>> {
					error_code{}, std::move(active_context)
				};
			}
			if constexpr( Method == method::get )
			{
				std::array<char,8192> redirect_body {};
				for(;;)
				{
					auto [read_error, bytes] = co_await active_context->reply()->read (
						buffer(redirect_body), asio::as_tuple(asio::use_awaitable_t<Exec>{})
					);
					ignore_unused(bytes);

					if( not read_error )
						continue;

					if( read_error != errc::eof )
					{
						co_return std::tuple<error_code,context_ptr<Method>> {
							read_error, {}
						};
					}
					break;
				}
			}
			try {
				auto resolved_url = url_t::resolve (
					request_info.url, **location
				);
				if( not same_origin(request_info.url, resolved_url) )
				{
					request_info.arg.unset_header(header::authorization);
					request_info.arg.cookies().clear();
				}
				request_info.url = std::move(resolved_url);
			}
			catch(...)
			{
				co_return std::tuple<error_code,context_ptr<Method>> {
					make_system_error_code(std::errc::protocol_error), {}
				};
			}
			auto context_result = co_await co_make_context<Method>(self, request_info);
			auto &[context_error, next_context] = context_result;

			if( context_error )
			{
				co_return std::tuple<error_code,context_ptr<Method>> {
					context_error, {}
				};
			}
			active_context = std::move(next_context);
			auto [write_error, bytes] = co_await active_context->write(
				asio::as_tuple(asio::use_awaitable_t<Exec>{})
			);
			ignore_unused(bytes);

			if( write_error )
			{
				co_return std::tuple<error_code,context_ptr<Method>> {
					write_error, {}
				};
			}
		}
		co_return std::tuple<error_code,context_ptr<Method>>{};
	}

	template <method_enum Method, typename Token>
	[[nodiscard]] auto async_follow_redirects(context_ptr<Method> current, req_info info, Token &&token)
	{
		using token_t = std::remove_cvref_t<Token>;
		token_t completion_token(std::forward<Token>(token));
		auto exec = m_pool.get_executor();

		return asio::async_initiate<token_t,void(error_code,context_ptr<Method>)>(
		[exec, self = this->shared_from_this(), current = std::move(current), info = std::move(info)]
		<typename Handler>(Handler completion_handler) mutable
		{
			riwo::detail::launch_awaitable(exec, co_follow_redirects<Method>(std::move(self),
				std::move(current), std::move(info)),
				riwo::detail::co_spawn_io_handler<context_ptr<Method>,Handler,decltype(exec)>(
					std::move(completion_handler), exec
				)
			);
		},
		completion_token);
	}

public:
	template <method_enum Method>
	[[nodiscard]] result_t<Method> request(req_info info) noexcept
	{
		const bool continue_100 = expects_continue(info);
		auto context_expected = make_context<Method>(info);

		if( not context_expected )
			return context_expected;

		error_code io_error {};
		ignore_unused((*context_expected)->write(io_error));

		if( io_error )
			return sys_unexpected(io_error);

		if( continue_100 )
		{
			for(;;)
			{
				auto reply = (*context_expected)->reply();
				auto reply_status = reply->status();

				if( reply_status == status::continue_upload or
					(reply_status != status::none and not reply->parser().is_informational()) )
					break;

				ignore_unused((*context_expected)->wait_reply(io_error));
				if( io_error )
					return sys_unexpected(io_error);
			}
		}
		if constexpr( Method == method::get or Method == method::head )
		{
			if( info.max_redirects > 0 )
			{
				return follow_redirects<Method>(
					std::move(*context_expected), std::move(info)
				);
			}
		}
		return context_expected;
	}

private:
	template <method_enum Method>
	[[nodiscard]] static asio::awaitable<std::tuple<error_code,context_ptr<Method>>,Exec>
	co_request(std::shared_ptr<impl> self, req_info request_info)
	{
		const bool wait_for_continue = expects_continue(request_info);
		auto context_result = co_await co_make_context<Method>(self, request_info);

		auto &[context_error, active_context] = context_result;
		if( context_error )
		{
			co_return std::tuple<error_code,context_ptr<Method>> {
				context_error, {}
			};
		}
		auto [write_error, bytes] = co_await active_context->write (
			asio::as_tuple(asio::use_awaitable_t<Exec>{})
		);
		ignore_unused(bytes);

		if( write_error )
		{
			co_return std::tuple<error_code,context_ptr<Method>> {
				write_error, {}
			};
		}
		if( wait_for_continue )
		{
			for(;;)
			{
				auto reply = active_context->reply();
				auto reply_status = reply->status();

				if( reply_status == status::continue_upload or
					(reply_status != status::none and not reply->parser().is_informational()) )
					break;

				auto [wait_error, received_status] =
					co_await detail::reply_access::wait(*reply);

				ignore_unused(received_status);
				if( wait_error )
				{
					co_return std::tuple<error_code,context_ptr<Method>> {
						wait_error, {}
					};
				}
			}
		}
		if constexpr( Method == method::get or Method == method::head )
		{
			if( request_info.max_redirects > 0 )
			{
				co_return co_await co_follow_redirects<Method>(std::move(self),
					std::move(active_context), std::move(request_info));
			}
		}
		co_return std::tuple<error_code,context_ptr<Method>> {
			error_code{}, std::move(active_context)
		};
	}

public:
	template <method_enum Method, typename Handler>
	void start_request(req_info info, Handler &&handler)
	{
		auto exec = m_pool.get_executor();
		using handler_t = std::remove_cvref_t<Handler>;

		riwo::detail::launch_awaitable(exec,
			co_request<Method>(this->shared_from_this(), std::move(info)),
			riwo::detail::co_spawn_io_handler<context_ptr<Method>,handler_t,decltype(exec)>(
				std::forward<Handler>(handler), exec
			)
		);
	}

	template <method_enum Method>
	[[nodiscard]] auto await_request(req_info info) {
		return co_request<Method>(this->shared_from_this(), std::move(info));
	}

public:
	[[nodiscard]] result_t<method::put>
	upload_file(req_info info, auto &&opt, auto &&progress) noexcept
	{
		auto pair = info.arg.set_header(std::forward<decltype(opt)>(opt));
		if( not pair )
			return sys_unexpected(pair.error());

		auto context_expected = request<method::put>(std::move(info));
		if( not context_expected )
			return context_expected;

		auto &context = *context_expected;
		if constexpr( version_v > version::v10 )
		{
			if( context->responded() and context->reply()->status() != status::continue_upload )
				return context_expected;
		}
		else
		{
			if( context->responded() )
				return context_expected;
		}
		error_code io_error {};
		ignore_unused(context->upload_file (
			std::move(pair->first), std::move(pair->second),
			std::forward<decltype(progress)>(progress), io_error
		));
		if( io_error )
			return sys_unexpected(io_error);

		ignore_unused(context->wait_reply(io_error));
		if( io_error )
			return sys_unexpected(io_error);
		return context_expected;
	}

private:
	template <typename AsyncOpt, typename AsyncProgress>
	[[nodiscard]] static asio::awaitable<std::tuple<error_code,context_ptr<method::put>>,Exec>
	co_upload_file(std::shared_ptr<impl> self, req_info request_info, AsyncOpt opt, AsyncProgress progress)
	{
		auto &upload_opt = unwrap_async_argument(opt);
		auto &progress_callback = unwrap_async_argument(progress);
		auto header_expected = request_info.arg.set_header(upload_opt);

		if( not header_expected )
		{
			co_return std::tuple<error_code,context_ptr<method::put>> {
				header_expected.error(), {}
			};
		}
		auto [request_error, active_context] =
			co_await co_request<method::put>(self, std::move(request_info));

		if( request_error )
		{
			co_return std::tuple<error_code,context_ptr<method::put>> {
				request_error, {}
			};
		}
		if constexpr( version_v > version::v10 )
		{
			if( active_context->responded() and
				active_context->reply()->status() != status::continue_upload )
			{
				co_return std::tuple<error_code,context_ptr<method::put>> {
					error_code{}, std::move(active_context)
				};
			}
		}
		else if( active_context->responded() )
		{
			co_return std::tuple<error_code,context_ptr<method::put>> {
				error_code{}, std::move(active_context)
			};
		}
		error_code upload_error {};
		auto bytes = co_await active_context->upload_file (
			std::move(header_expected->first), std::move(header_expected->second),
			progress_callback, asio::redirect_error(asio::use_awaitable_t<Exec>{}, upload_error)
		);
		ignore_unused(bytes);

		if( upload_error )
		{
			co_return std::tuple<error_code,context_ptr<method::put>> {
				upload_error, {}
			};
		}
		auto [wait_error, reply_status] =
			co_await detail::reply_access::wait(*active_context->reply());

		ignore_unused(reply_status);
		if( wait_error )
		{
			co_return std::tuple<error_code,context_ptr<method::put>> {
				wait_error, {}
			};
		}
		co_return std::tuple<error_code,context_ptr<method::put>> {
			error_code{}, std::move(active_context)
		};
	}

public:
	template <typename AsyncOpt, typename AsyncProgress, typename Token>
	[[nodiscard]] auto async_upload_file
	(req_info info, AsyncOpt async_opt, AsyncProgress async_progress, Token &&token)
	{
		using opt_t = std::remove_cvref_t<AsyncOpt>;
		using progress_t = std::remove_cvref_t<AsyncProgress>;
		using token_t = std::remove_cvref_t<Token>;

		token_t completion_token(std::forward<Token>(token));
		auto exec = m_pool.get_executor();

		return asio::async_initiate<token_t,void(error_code,context_ptr<method::put>)>([
			exec, self = this->shared_from_this(), info = std::move(info), opt = opt_t(std::move(async_opt)),
			progress = progress_t(std::move(async_progress))
		]<typename Handler>(Handler completion_handler) mutable
		{
			riwo::detail::launch_awaitable(exec, co_upload_file(std::move(self), std::move(info),
				std::move(opt), std::move(progress)),
				riwo::detail::co_spawn_io_handler<context_ptr<method::put>,Handler,decltype(exec)>(
					std::move(completion_handler), exec
				)
			);
		},
		completion_token);
	}

	[[nodiscard]] result_t<method::get>
	download_file(req_info info, auto &&opt, auto &&progress) noexcept
	{
		auto context_expected = request<method::get>(std::move(info));
		if( not context_expected )
			return context_expected;

		error_code io_error {};
		ignore_unused((*context_expected)->reply()->save_file (
			std::forward<decltype(opt)>(opt), std::forward<decltype(progress)>(progress),
			io_error
		));
		if( io_error )
			return sys_unexpected(io_error);
		return context_expected;
	}

private:
	template <typename AsyncOpt, typename AsyncProgress>
	[[nodiscard]] static asio::awaitable<std::tuple<error_code,context_ptr<method::get>>,Exec>
	co_download_file(std::shared_ptr<impl> self, req_info request_info, AsyncOpt opt, AsyncProgress progress)
	{
		auto [request_error, active_context] =
			co_await co_request<method::get>(self, std::move(request_info));

		if( request_error )
		{
			co_return std::tuple<error_code,context_ptr<method::get>> {
				request_error, {}
			};
		}
		auto &download_opt = unwrap_async_argument(opt);
		auto &progress_callback = unwrap_async_argument(progress);

		error_code save_error {};
		auto bytes = co_await active_context->reply()->save_file(download_opt,
			progress_callback, asio::redirect_error(asio::use_awaitable_t<Exec>{}, save_error)
		);
		ignore_unused(bytes);

		if( save_error )
		{
			co_return std::tuple<error_code,context_ptr<method::get>> {
				save_error, {}
			};
		}
		co_return std::tuple<error_code,context_ptr<method::get>> {
			error_code{}, std::move(active_context)
		};
	}

public:
	template <typename AsyncOpt, typename AsyncProgress, typename Token>
	[[nodiscard]] auto async_download_file
	(req_info info, AsyncOpt async_opt, AsyncProgress async_progress, Token &&token)
	{
		using opt_t = std::remove_cvref_t<AsyncOpt>;
		using progress_t = std::remove_cvref_t<AsyncProgress>;
		using token_t = std::remove_cvref_t<Token>;

		token_t completion_token(std::forward<Token>(token));
		auto exec = m_pool.get_executor();

		return asio::async_initiate<token_t,void(error_code,context_ptr<method::get>)>([
			exec, self = this->shared_from_this(), info = std::move(info),
			opt = opt_t(std::move(async_opt)), progress = progress_t(std::move(async_progress))
		]<typename Handler>(Handler completion_handler) mutable
		{
			riwo::detail::launch_awaitable(exec, co_download_file(std::move(self), std::move(info),
				std::move(opt), std::move(progress)),
				riwo::detail::co_spawn_io_handler<context_ptr<method::get>,Handler,decltype(exec)>(
					std::move(completion_handler), exec
				)
			);
		},
		completion_token);
	}

public:
	template <method_enum Method>
	[[nodiscard]] result_t<Method> make_context(req_info info) noexcept
	{
		try {
			auto target_expected = resolve_request_target(info);
			if( not target_expected )
				return sys_unexpected(target_expected.error());

			add_request_cookies(info);
			auto lease_expected = m_pool.get(target_expected->connection);
			if( not lease_expected )
				return sys_unexpected(lease_expected.error());

			auto context = std::make_shared<context_t<Method>>
			(
				std::move(*lease_expected), std::move(info.url),
				typename context_t<Method>::options {
					std::move(info.arg), m_cookie_store, target_expected->forward_proxy ?
						request_target_form::absolute : request_target_form::origin,
					info.auto_decompression
				}
			);
			return context;
		}
		catch(const std::system_error &ex) {
			return sys_unexpected(ex.code());
		}
		catch(const std::bad_alloc&)
		{
			return sys_unexpected (
				make_system_error_code(std::errc::not_enough_memory)
			);
		}
		catch(...) {}
		return sys_unexpected(make_system_error_code(std::errc::io_error));
	}

	template <method_enum Method, typename Handler>
	void start_make_context(req_info info, Handler &&handler)
	{
		auto exec = m_pool.get_executor();
		using handler_t = std::remove_cvref_t<Handler>;

		riwo::detail::launch_awaitable(exec,
			co_make_context<Method>(this->shared_from_this(), std::move(info)),
			riwo::detail::co_spawn_io_handler<context_ptr<Method>,handler_t,decltype(exec)>(
				std::forward<Handler>(handler), exec
			)
		);
	}

public:
	connection_pool_t m_pool {};
	std::shared_ptr<cookie_jar> m_cookie_store {};
	config_t m_config {};
};

namespace detail
{

struct RIWO_HTTP_TAPI client_access
{
	template <method_enum Method, core_concepts::exec Exec, version_enum Version>
	[[nodiscard]] static auto request
	(basic_client<Exec,Version> &client, basic_client<Exec,Version>::req_info info) {
		return client.m_impl->template await_request<Method>(std::move(info));
	}
};

} //namespace detail

template <core_concepts::exec Exec, version_enum Version>
basic_client<Exec,Version>::basic_client(config_t config) requires
	core_concepts::match_sched<io_executor_t,executor_t> :
	m_impl(std::make_shared<impl>(std::move(config)))
{

}

template <core_concepts::exec Exec, version_enum Version>
template <typename Exec0>
basic_client<Exec,Version>::basic_client(Exec0 &&exec, config_t config) requires
(not std::same_as<std::remove_cvref_t<Exec0>,basic_client> and core_concepts::match_sched<Exec0,executor_t>) :
	m_impl(std::make_shared<impl>(
		executor_t(get_executor_helper(std::forward<Exec0>(exec))),
		std::move(config)
	))
{

}

template <core_concepts::exec Exec, version_enum Version>
basic_client<Exec,Version>::basic_client(connection_pool_t &&pool, config_t config) :
	m_impl(std::make_shared<impl>(std::move(pool), std::move(config)))
{

}

template <core_concepts::exec Exec, version_enum Version>
basic_client<Exec,Version>::basic_client(basic_client &&other) noexcept :
	m_impl(std::move(other.m_impl))
{

}

template <core_concepts::exec Exec, version_enum Version>
basic_client<Exec,Version>&
basic_client<Exec,Version>::operator=(basic_client &&other) noexcept
{
	if( this != &other )
		m_impl = std::move(other.m_impl);
	return *this;
}

template <core_concepts::exec Exec, version_enum Version>
basic_client<Exec,Version>::~basic_client() = default;

template <core_concepts::exec Exec, version_enum Version>
template <method_enum Method, typename Token>
auto basic_client<Exec,Version>::request(req_info info, Token &&token)
	requires request_token_v<Method,Token>
{
	if constexpr( is_error_code_token_v<Token> )
	{
		return expected_value_or_error (
			m_impl->template request<Method>(std::move(info)), token
		);
	}
	else if constexpr( is_sync_opt_token_v<Token> )
	{
		return expected_value_or_throw (
			m_impl->template request<Method>(std::move(info))
		);
	}
	else
	{
		return initiate_io<context_ptr<Method>>(
		get_executor(), [impl = m_impl, request_info = std::move(info)]
		<typename T0>(T0 &&completion_token) mutable
		{
			impl->template start_request<Method>(
				std::move(request_info), std::forward<T0>(completion_token)
			);
		},
		std::forward<Token>(token));
	}
}

template <core_concepts::exec Exec, version_enum Version>
template <typename T, typename Token>
auto basic_client<Exec,Version>::upload_file(req_info info, T &&opt, Token &&token)
	requires upload_file_opt_token_v<T,Token>
{
	return upload_file(std::move(info), std::forward<T>(opt),
		[](size_t,size_t){}, std::forward<Token>(token)
	);
}

template <core_concepts::exec Exec, version_enum Version>
template <typename T, typename Progress, typename Token>
auto basic_client<Exec,Version>::upload_file(req_info info, T &&opt, Progress &&progress, Token &&token)
	requires upload_file_opt_token_v<T,Token> and concepts::progress_handler<Progress,Token>
{
	if constexpr( is_error_code_token_v<Token> )
	{
		return expected_value_or_error (
			m_impl->upload_file(std::move(info),
				std::forward<T>(opt), std::forward<Progress>(progress)
			), token
		);
	}
	else if constexpr( is_sync_opt_token_v<Token> )
	{
		return expected_value_or_throw (
			m_impl->upload_file(std::move(info),
				std::forward<T>(opt), std::forward<Progress>(progress)
			)
		);
	}
	else
	{
		return initiate_io<context_ptr<method::put>>(get_executor(), [
			impl = m_impl, request_info = std::move(info),
			async_opt = capture_async_argument(std::forward<T>(opt)),
			async_progress = capture_async_argument (
				std::forward<Progress>(progress)
			)
		]
		<typename T0>(T0 &&completion_token) mutable
		{
			return impl->async_upload_file (
				std::move(request_info), std::move(async_opt),
				std::move(async_progress), std::forward<T0>(completion_token)
			);
		},
		std::forward<Token>(token));
	}
}

template <core_concepts::exec Exec, version_enum Version>
template <typename T, typename Token>
auto basic_client<Exec,Version>::download_file(req_info info, T &&opt, Token &&token)
	requires download_file_opt_token_v<T,Token>
{
	return download_file(std::move(info), std::forward<T>(opt),
		[](size_t,size_t){}, std::forward<Token>(token)
	);
}

template <core_concepts::exec Exec, version_enum Version>
template <typename T, typename Progress, typename Token>
auto basic_client<Exec,Version>::download_file(req_info info, T &&opt, Progress &&progress, Token &&token)
	requires download_file_opt_token_v<T,Token> and concepts::progress_handler<Progress,Token>
{
	if constexpr( is_error_code_token_v<Token> )
	{
		return expected_value_or_error (
			m_impl->download_file(std::move(info),
				std::forward<T>(opt), std::forward<Progress>(progress)
			), token
		);
	}
	else if constexpr( is_sync_opt_token_v<Token> )
	{
		return expected_value_or_throw (
			m_impl->download_file(std::move(info),
				std::forward<T>(opt), std::forward<Progress>(progress)
			)
		);
	}
	else
	{
		return initiate_io<context_ptr<method::get>>(get_executor(), [
			impl = m_impl, request_info = std::move(info),
			async_opt = capture_async_argument(std::forward<T>(opt)),
			async_progress = capture_async_argument (
				std::forward<Progress>(progress)
			)
		]
		<typename T0>(T0 &&completion_token) mutable
		{
			return impl->async_download_file (
				std::move(request_info), std::move(async_opt),
				std::move(async_progress), std::forward<T0>(completion_token)
			);
		},
		std::forward<Token>(token));
	}
}

#define RIWO_HTTP_CLIENT_REQUEST_METHOD(Name, Method) \
	template <core_concepts::exec Exec, version_enum Version> \
	template <typename Token> \
	auto basic_client<Exec,Version>::request_##Name(req_info info, Token &&token) \
		requires request_token_v<method::Method,Token> { \
		return request<method::Method>( \
			std::move(info), std::forward<Token>(token) \
		); \
	}

RIWO_HTTP_CLIENT_REQUEST_METHOD(get    , get    )
RIWO_HTTP_CLIENT_REQUEST_METHOD(put    , put    )
RIWO_HTTP_CLIENT_REQUEST_METHOD(post   , post   )
RIWO_HTTP_CLIENT_REQUEST_METHOD(head   , head   )
RIWO_HTTP_CLIENT_REQUEST_METHOD(patch  , patch  )
RIWO_HTTP_CLIENT_REQUEST_METHOD(delete , delet  )
RIWO_HTTP_CLIENT_REQUEST_METHOD(options, options)
RIWO_HTTP_CLIENT_REQUEST_METHOD(trace  , trace  )
RIWO_HTTP_CLIENT_REQUEST_METHOD(connect, connect)

#undef RIWO_HTTP_CLIENT_REQUEST_METHOD

template <core_concepts::exec Exec, version_enum Version>
template <method_enum Method, typename Token>
auto basic_client<Exec,Version>::make_context(req_info info, Token &&token)
	requires request_token_v<Method,Token>
{
	if constexpr( is_error_code_token_v<Token> )
	{
		return expected_value_or_error (
			m_impl->template make_context<Method>(std::move(info)), token
		);
	}
	else if constexpr( is_sync_opt_token_v<Token> )
	{
		return expected_value_or_throw (
			m_impl->template make_context<Method>(std::move(info))
		);
	}
	else
	{
		return initiate_io<context_ptr<Method>>(get_executor(),
		[impl = m_impl, request_info = std::move(info)]<typename T0>(T0 &&completion_token) mutable
		{
			impl->template start_make_context<Method>(
				std::move(request_info), std::forward<T0>(completion_token)
			);
		},
		std::forward<Token>(token));
	}
}

#define RIWO_HTTP_CLIENT_MAKE_METHOD(Name, Method) \
	template <core_concepts::exec Exec, version_enum Version> \
	template <typename Token> \
	auto basic_client<Exec,Version>::make_##Name(req_info info, Token &&token) \
		requires request_token_v<method::Method,Token> { \
		return make_context<method::Method>( \
			std::move(info), std::forward<Token>(token) \
		); \
	}

RIWO_HTTP_CLIENT_MAKE_METHOD(get    , get    )
RIWO_HTTP_CLIENT_MAKE_METHOD(put    , put    )
RIWO_HTTP_CLIENT_MAKE_METHOD(post   , post   )
RIWO_HTTP_CLIENT_MAKE_METHOD(head   , head   )
RIWO_HTTP_CLIENT_MAKE_METHOD(patch  , patch  )
RIWO_HTTP_CLIENT_MAKE_METHOD(delete , delet  )
RIWO_HTTP_CLIENT_MAKE_METHOD(options, options)
RIWO_HTTP_CLIENT_MAKE_METHOD(trace  , trace  )
RIWO_HTTP_CLIENT_MAKE_METHOD(connect, connect)

#undef RIWO_HTTP_CLIENT_MAKE_METHOD

template <core_concepts::exec Exec, version_enum Version>
std::shared_ptr<cookie_jar> basic_client<Exec,Version>::cookie_store() noexcept
{
	return m_impl->m_cookie_store;
}

template <core_concepts::exec Exec, version_enum Version>
auto basic_client<Exec,Version>::config() const -> config_t
{
	return m_impl->m_config;
}

template <core_concepts::exec Exec, version_enum Version>
consteval version_enum basic_client<Exec,Version>::version() noexcept
{
	return version_v;
}

template <core_concepts::exec Exec, version_enum Version>
basic_client<Exec,Version>::executor_t basic_client<Exec,Version>::get_executor() noexcept
{
	return m_impl->m_pool.get_executor();
}

} //namespace riwo::http

#if defined(__GNUC__) && !defined(__clang__)
# pragma GCC diagnostic pop
#endif

#endif //RIWO_HTTP_CLIENT_DETAIL_CLIENT_H
