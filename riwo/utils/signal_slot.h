// SPDX-FileCopyrightText: 2025-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_UTILS_SIGNAL_SLOT_H
#define RIWO_UTILS_SIGNAL_SLOT_H

#include <riwo/utils/global.h>

namespace riwo::utils
{

enum class slot_mode
{
	sync,         // Direct blocking call. (thread unsafe)
	async,        // Non-blocking call. (depends on the executor)
	backpressure  // Thread-safe blocking call. (depends on the executor; otherwise, it will cause a deadlock)
};

template <typename T, typename Tag>
struct arg_converter
{
	static constexpr bool valid = std::is_convertible_v<T,Tag>;
	using tag_t = std::remove_reference_t<Tag>;
	using t_t = std::remove_cvref_t<T>;

	[[nodiscard]] static decltype(auto) convert(const t_t &value) requires valid;
	[[nodiscard]] static decltype(auto) convert(t_t &&value) requires valid;
	[[nodiscard]] static decltype(auto) convert(t_t &value) requires valid;
};

template <typename T0, typename T1>
constexpr bool arg_convertible_v = arg_converter<T0,T1>::valid;

template <typename Derived, concepts::std_func_temp Func>
class RIWO_UTILS_TAPI signal_base
{
	RIWO_DISABLE_COPY_MOVE(signal_base)

public:
	using derived_t = crtp_derived_t<Derived,signal_base>;
	using function_t = Func;
	using func_traits_t = function_traits<function_t>;

	signal_base();
	~signal_base();

public:
	template <slot_mode Mode, concepts::function Slot>
	static constexpr bool is_slot_v = []() consteval
	{
		using slot_tr = function_traits<Slot>;
		using sig_tr  = func_traits_t;

		using slot_ret = slot_tr::return_type;
		using sig_ret  = sig_tr::return_type;

		if constexpr( Mode == slot_mode::sync and
			not is_awaitable_v<sig_ret> and is_awaitable_v<slot_ret> )
			return false;
		else
		{
			return slot_tr::arg_count <= sig_tr::arg_count and
			[]<size_t...Is>(std::index_sequence<Is...>) consteval
			{
				return ([]<size_t I>() consteval
				{
					using sig_at  = sig_tr ::template arg_type_t<I>;
					using slot_at = slot_tr::template arg_type_t<I>;

					using r_sig_at  = std::remove_cvref_t<sig_at>;
					using r_slot_at = std::remove_cvref_t<slot_at>;

					if constexpr( is_variant_v<r_sig_at> )
						return is_contained_in_v<r_sig_at, r_slot_at>;

					else if constexpr( std::is_same_v<r_sig_at, std::any> )
					{
						if constexpr( std::is_same_v<r_slot_at, std::any> )
							return true;
						else
						{
							return requires(slot_at arg) {
								std::any_cast<r_slot_at>(arg);
							};
						}
					}
					else
					{
						return arg_convertible_v <
							typename sig_tr ::template arg_type_t<I>,
							typename slot_tr::template arg_type_t<I>
						>;
					}
				}
				.template operator()<Is>() and ...);
			}
			(std::make_index_sequence<slot_tr::arg_count>{});
		}
	}();

	template <slot_mode Mode, concepts::function Slot>
	static constexpr bool is_global_slot_v =
		not function_traits<Slot>::is_member_func and
		is_slot_v<Mode, Slot>;

	template <typename Obj>
	static constexpr bool is_observer_v =
		is_shared_ptr_v<std::remove_cvref_t<Obj>>;

	template <slot_mode Mode, typename Obj, concepts::function Slot>
	requires is_observer_v<Obj>
	static constexpr bool is_obj_slot_v = []() consteval
	{
		if constexpr( is_observer_v<Obj> )
		{
			using slot_tr = function_traits<Slot>;
			if constexpr( slot_tr::is_member_func )
			{
				using obj_t = std::remove_cvref_t<Obj>::element_type;
				if constexpr( std::is_same_v<typename slot_tr::class_t, obj_t> )
					return is_slot_v<Mode,Slot>;
				else
					return false;
			}
			else
				return is_global_slot_v<Mode,Slot>;
		}
		else
			return false;
	}();

public:
	template <slot_mode Mode, concepts::function...Slots>
	requires (sizeof...(Slots) > 0)
	static constexpr bool is_global_slots_v = (is_global_slot_v<Mode,Slots> and ...);

	template <concepts::function...Slots>
	requires (sizeof...(Slots) > 0)
	static constexpr bool is_global_slots_def_v =
		is_global_slots_v<slot_mode::sync, Slots...> or
		is_global_slots_v<slot_mode::async, Slots...>;

	template <slot_mode Mode, typename Obj, concepts::function...Slots>
	requires (is_observer_v<Obj> and sizeof...(Slots) > 0)
	static constexpr bool is_obj_slots_v = (is_obj_slot_v<Mode,Obj,Slots> and ...);

	template <typename Obj, concepts::function...Slots>
	requires (is_observer_v<Obj> and sizeof...(Slots) > 0)
	static constexpr bool is_obj_slots_def_v =
		is_obj_slots_v<slot_mode::sync, Obj, Slots...> or
		is_obj_slots_v<slot_mode::async, Obj, Slots...>;

	template <typename...Args>
	static constexpr bool is_callable_v =
		requires(std::function<function_t> sig, Args&&...args) {
			sig(std::forward<Args>(args)...);
		};

public:
	template <slot_mode Mode, typename...Slots>
	derived_t &connect(Slots&&...funcs) noexcept
		requires is_global_slots_v<Mode,Slots...>;

	template <slot_mode Mode, typename Obj, typename...Slots>
	derived_t &connect(Obj &&observer, Slots&&...funcs)
		requires is_obj_slots_v<Mode,Obj,Slots...>;

	template <slot_mode Mode, concepts::sched Exec0, typename...Slots>
	derived_t &connect(Exec0 &&exec, Slots&&...funcs) noexcept
		requires (Mode != slot_mode::sync) and is_global_slots_v<Mode,Slots...>;

	template <slot_mode Mode, typename Obj, concepts::sched Exec0, typename...Slots>
	derived_t &connect(Obj &&observer, Exec0 &&exec, Slots&&...funcs)
		requires (Mode != slot_mode::sync) and is_obj_slots_v<Mode,Obj,Slots...>;

public:
	template <typename...Slots>
	derived_t &connect(Slots&&...funcs) noexcept
		requires is_global_slots_def_v<Slots...>;

	template <typename Obj, typename...Slots>
	derived_t &connect(Obj &&observer, Slots&&...funcs)
		requires is_obj_slots_def_v<Obj,Slots...>;

	template <concepts::sched Exec0, typename...Slots>
	derived_t &connect(Exec0 &&exec, Slots&&...funcs) noexcept
		requires is_global_slots_v<slot_mode::async,Slots...>;

	template <typename Obj, concepts::sched Exec0, typename...Slots>
	derived_t &connect(Obj &&observer, Exec0 &&exec, Slots&&...funcs)
		requires is_obj_slots_v<slot_mode::async,Obj,Slots...>;

public:
	template <typename...Slots>
	derived_t &disconnect(Slots&&...funcs) noexcept
		requires is_global_slots_def_v<Slots...>;

	template <typename Obj, typename...Slots>
	derived_t &disconnect(const Obj &observer, Slots&&...funcs)
		requires is_obj_slots_def_v<Obj,Slots...>;

	derived_t &disconnect() noexcept;

	template <typename Obj>
	derived_t &disconnect(const Obj &observer)
		requires is_observer_v<Obj>;

public:
	template <typename...Args>
	[[nodiscard]] auto emit(Args&&...args) const noexcept
		requires is_callable_v<Args...>;

	template <typename...Args>
	[[nodiscard]] auto operator()(Args&&...args) const noexcept
		requires is_callable_v<Args...>;

	void block(bool block = true) noexcept;
	[[nodiscard]] bool is_blocked() const noexcept;

private:
	class impl;
	std::shared_ptr<impl> m_impl;
};

template <concepts::std_func_temp Func>
using signal = signal_base<void,Func>;

} //namespace riwo::utils
#include <riwo/utils/detail/signal_slot.h>


#endif //RIWO_UTILS_SIGNAL_SLOT_H
