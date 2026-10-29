// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_SERVER_MIDDLEWARE_H
#define RIWO_HTTP_EXP_SERVER_MIDDLEWARE_H

#include <riwo/http_exp/server/exchange.h>

namespace riwo::http_exp
{
// `before() == true` means the middleware completed the exchange and the
// remaining chain/controller must not run. This retains stable riwo.http AOP
// semantics while operating only on the protocol-neutral exchange.
template <core_concepts::exec Exec = asio::any_io_executor>
class RIWO_HTTP_EXP_TAPI basic_middleware
{
	RIWO_DISABLE_COPY_MOVE(basic_middleware)

public:
	using executor_type = Exec;
	using executor_t = executor_type;

	using exchange_t = basic_server_exchange<executor_t>;
	using ptr_t = std::shared_ptr<basic_middleware>;

	basic_middleware() = default;
	virtual ~basic_middleware() = default;

	[[nodiscard]] virtual awaitable<bool> before(exchange_t&);
	[[nodiscard]] virtual awaitable<bool> after(exchange_t&);
	[[nodiscard]] virtual bool exception(exchange_t&, const std::exception&);
};

template <core_concepts::exec Exec = asio::any_io_executor>
class RIWO_HTTP_EXP_TAPI basic_controller : public basic_middleware<Exec>
{
public:
	using exchange_t = basic_server_exchange<Exec>;
	using ptr_t = std::shared_ptr<basic_controller>;
	[[nodiscard]] virtual awaitable<void> service(exchange_t&) = 0;
};

template <core_concepts::exec Exec = asio::any_io_executor>
using basic_aop = basic_middleware<Exec>;

template <core_concepts::exec Exec = asio::any_io_executor>
using basic_ctrlr_aop = basic_controller<Exec>;

using middleware = basic_middleware<>;
using middleware_ptr_t = middleware::ptr_t;

using controller = basic_controller<>;
using controller_ptr_t = controller::ptr_t;

using aop = middleware;
using aop_ptr_t = middleware_ptr_t;

using ctrlr_aop = controller;
using ctrlr_aop_ptr_t = controller_ptr_t;

namespace concepts
{

template <typename Exec, typename... Values>
concept middleware_list = requires(Values&&... values)
{
	std::vector<typename basic_middleware<Exec>::ptr_t> {
		typename basic_middleware<Exec>::ptr_t(std::forward<Values>(values))...
	};
};

}} //namespace riwo::http_exp::concepts
#include <riwo/http_exp/server/detail/middleware.h>


#endif //RIWO_HTTP_EXP_SERVER_MIDDLEWARE_H
