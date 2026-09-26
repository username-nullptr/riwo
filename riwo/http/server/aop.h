// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_SERVER_AOP_H
#define RIWO_HTTP_SERVER_AOP_H

#include <riwo/http/server/service_context.h>

namespace riwo::http
{

template <core_concepts::exec Exec = asio::any_io_executor>
class basic_aop
{
	RIWO_DISABLE_COPY_MOVE(basic_aop)

public:
	using executor_type = Exec;
	using executor_t = executor_type;

	using ptr_t = std::shared_ptr<basic_aop>;
	using context_t = basic_service_context<executor_t>;

	using connection_t = basic_connection<executor_t>;
	using connection_ptr = connection_t::ptr_t;

	basic_aop() = default;
	virtual ~basic_aop() = 0;

public:
	[[nodiscard]] virtual awaitable<bool> before(context_t &context);
	[[nodiscard]] virtual awaitable<bool> after(context_t &context);
	[[nodiscard]] virtual bool exception(context_t &context, const std::exception &ex);
};

using aop = basic_aop<>;
using aop_ptr = basic_aop<>::ptr_t;

template <core_concepts::exec Exec = asio::any_io_executor>
class basic_ctrlr_aop : public basic_aop<Exec>
{
public:
	using executor_type = Exec;
	using executor_t = executor_type;
	using ptr_t = std::shared_ptr<basic_ctrlr_aop>;

	using context_t = basic_service_context<executor_t>;
	[[nodiscard]] virtual awaitable<void> service(context_t &context) = 0;
};

using ctrlr_aop = basic_ctrlr_aop<>;
using ctrlr_aop_ptr = basic_ctrlr_aop<>::ptr_t;

namespace concepts
{

template <typename Exec, typename...Args>
concept aop_ptr_list = requires(Args&&...args) {
	std::vector<typename basic_aop<Exec>::ptr_t> {
		typename basic_aop<Exec>::ptr_t(std::forward<Args>(args))...
	};
};

template <typename Exec, typename...Args>
concept ctrlr_aop_ptr_list = requires(Args&&...args) {
	std::vector<typename basic_ctrlr_aop<Exec>::ptr_t> {
		typename basic_ctrlr_aop<Exec>::ptr_t(std::forward<Args>(args))...
	};
};

template <typename Func, typename Exec>
concept request_handler = requires(Func &&func, basic_service_context<Exec> &context) {
	func(context);
} and std::same_as <
	awaitable_ret_t<decltype(
		std::declval<Func>()(std::declval<basic_service_context<Exec>&>())
	)>, void
>;

}} //namespace riwo::http
#include <riwo/http/server/detail/aop.h>


#endif //RIWO_HTTP_SERVER_AOP_H
