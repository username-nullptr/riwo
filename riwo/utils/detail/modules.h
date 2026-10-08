// SPDX-FileCopyrightText: 2025-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_UTILS_DETAIL_MODULES_H
#define RIWO_UTILS_DETAIL_MODULES_H

#include <riwo/core/execution.h>

namespace riwo::utils { namespace detail
{

class RIWO_UTILS_API modules
{
	RIWO_DISABLE_COPY_MOVE(modules);

public:
	using func0_t = std::function<bool()>;
	using func1_t = std::function<bool(const string_vector&)>;

	using func2_t = std::function<void()>;
	using func3_t = std::function<void(const string_vector&)>;

	enum class state {
		finished, not_register
	};
	using func_obj_t = std::variant <
		func0_t, func1_t, func2_t, func3_t, state
	>;
	static void reg_init(std::string name,
		const utils::modules::dependency &depy, func_obj_t func
	);
	static void do_init(const string_vector &args,
		std::function<void(utils::modules::unexpected)> callback
	);
};

} //namespace detail

template <typename Func>
void modules::reg_init(std::string name, dependency depy, Func &&func)
	requires init_func_v<Func>
{
	if constexpr( concepts::callable_ret<Func,bool> )
	{
		detail::modules::reg_init(std::move(name), std::move(depy),
			detail::modules::func0_t(std::forward<Func>(func))
		);
	}
	else if constexpr( concepts::callable_ret<Func,bool,string_vector> )
	{
		detail::modules::reg_init(std::move(name), std::move(depy),
			detail::modules::func1_t(std::forward<Func>(func))
		);
	}
	else if constexpr( concepts::callable_void<Func> )
	{
		detail::modules::reg_init(std::move(name), std::move(depy),
			detail::modules::func2_t(std::forward<Func>(func))
		);
	}
	else if constexpr( concepts::callable_void<Func,string_vector> )
	{
		detail::modules::reg_init(std::move(name), std::move(depy),
			detail::modules::func3_t(std::forward<Func>(func))
		);
	}
}

template <typename Func>
void modules::reg_init(std::string name, Func &&func)
	requires init_func_v<Func>
{
	reg_init(std::move(name), {}, std::forward<Func>(func));
}

template <typename Token>
auto modules::do_init(int argc, const char *argv[], Token &&token)
	requires init_token_v<Token>
{
	return do_init({argv, argv + argc}, std::forward<Token>(token));
}

template <typename Token>
auto modules::do_init(const string_vector &args, Token &&token)
	requires init_token_v<Token>
{
	using token_t = std::remove_cvref_t<Token>;
	if constexpr( is_use_future_v<token_t> )
	{
		auto result_promise = std::make_shared<std::promise<unexpected>>();
		auto future = result_promise->get_future();

		detail::modules::do_init(args,
		[promise = std::move(result_promise)](const unexpected &result) mutable {
			promise->set_value(result);
		});
		return future;
	}
	else if constexpr( is_sync_opt_token_v<token_t> )
		return do_init(args, use_future).get();

	else if constexpr( is_detached_v<token_t> )
		detail::modules::do_init(args, []{});
	else
	{
		if constexpr( concepts::callable<Token,unexpected> )
			detail::modules::do_init(args, std::forward<Token>(token));
		else
		{
			detail::modules::do_init(args,
			[func = std::forward<Token>(token)](const unexpected&){
				func();
			});
		}
	}
}

template <typename Token>
auto modules::do_init(Token &&token)
	requires init_token_v<Token>
{
	return do_init({}, std::forward<Token>(token));
}

template <typename Func>
auto modules::do_init(int argc, const char **argv, concepts::sched auto &&exec, Func &&callback)
	requires init_callable_v<Func>
{
	using Exec = decltype(exec);
	return do_init({argv, argv + argc},
		std::forward<Exec>(exec), std::forward<Func>(callback)
	);
}

template <typename Func>
auto modules::do_init(const string_vector &args, concepts::sched auto &&executor_arg, Func &&callback)
	requires init_callable_v<Func>
{
	using Exec = decltype(executor_arg);
	if constexpr( concepts::callable<Func,unexpected> )
	{
		return do_init(args,
		[exec = get_executor_helper(std::forward<Exec>(executor_arg)), func = std::forward<Func>(callback)]
		(const unexpected &result)
		{
			riwo::dispatch(exec, [result, callback_fn = std::move(func)] {
				callback_fn(result);
			});
		});
	}
	else
	{
		return do_init(args,
		[exec = get_executor_helper(std::forward<Exec>(executor_arg)), func = std::forward<Func>(callback)]{
			riwo::dispatch(exec, std::move(func));
		});
	}
}

template <typename Func>
auto modules::do_init(concepts::sched auto &&exec, Func &&callback)
	requires init_callable_v<Func>
{
	using Exec = decltype(exec);
	return do_init({},
		std::forward<Exec>(exec), std::forward<Func>(callback)
	);
}

} //namespace riwo::utils


#endif //RIWO_UTILS_DETAIL_MODULES_H
