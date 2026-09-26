// SPDX-FileCopyrightText: 2025 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_UTILS_OBSERVER_H
#define RIWO_UTILS_OBSERVER_H

#include <riwo/core/execution.h>
#include <riwo/utils/global.h>

namespace riwo::utils
{

template <typename Derived, concepts::exec Exec, concepts::std_func_temp...Funcs> requires (sizeof...(Funcs) > 0)
class RIWO_UTILS_TAPI basic_observer_base : public std::enable_shared_from_this<
	crtp_derived_t<Derived, basic_observer_base<Derived,Exec,Funcs...>>>
{
	RIWO_DISABLE_COPY_MOVE(basic_observer_base)
	using derived_t = crtp_derived_t<Derived,basic_observer_base>;

public:
	using ptr_t = std::shared_ptr<derived_t>;
	using callbacks_t = std::tuple<std::vector<std::function<Funcs>>...>;
	using callbacks_tuple_t = std::tuple<std::function<Funcs>...>;

	using executor_type = Exec;
	using executor_t = executor_type;

	template <size_t Idx>
	using callback_t = std::tuple_element_t<Idx,callbacks_tuple_t>;

	template <size_t Idx>
	static constexpr bool idx_valid_v = Idx < sizeof...(Funcs);

public:
	template <concepts::match_sched<Exec> Exec0 = io_context_t&>
	explicit basic_observer_base(uint64_t id, Exec0 &&exec = io_context());
	virtual ~basic_observer_base();

public:
	template <typename...Args0>
	[[nodiscard]] static ptr_t make(Args0&&...args) requires
		concepts::constructible<derived_t,Args0...>;

	template <size_t Idx>
	ptr_t on_triggered(callback_t<Idx> func)
		requires idx_valid_v<Idx>;

	template <size_t Idx, typename...Args0>
	static void trigger(uint64_t id, Args0&&...args) requires
		idx_valid_v<Idx> and concepts::callable<callback_t<Idx>,Args0...>;

	[[nodiscard]] executor_t get_executor() noexcept;

private:
	class impl;
	impl *m_impl = nullptr;
};

template <concepts::exec Exec, concepts::std_func_temp...Funcs> requires (sizeof...(Funcs) > 0)
using basic_observer = basic_observer_base<void, Exec, Funcs...>;

template <typename Derived, concepts::std_func_temp...Funcs> requires (sizeof...(Funcs) > 0)
using observer_base = basic_observer_base<Derived, asio::any_io_executor, Funcs...>;

template <concepts::std_func_temp...Funcs> requires (sizeof...(Funcs) > 0)
using observer = basic_observer<asio::any_io_executor, Funcs...>;

} //namespace riwo::utils
#include <riwo/utils/detail/observer.h>


#endif //RIWO_UTILS_OBSERVER_H
