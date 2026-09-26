// SPDX-FileCopyrightText: 2025-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_UTILS_DETAIL_SIGNAL_SLOT_H
#define RIWO_UTILS_DETAIL_SIGNAL_SLOT_H

#include <riwo/core/execution.h>
#include <mutex>

namespace riwo::utils { namespace detail
{

template <typename Tag, typename T>
[[nodiscard]] RIWO_UTILS_TAPI constexpr bool slot_arg_can_auto_cast(const T &arg) noexcept
{
	using target_t = std::remove_cvref_t<Tag>;
	using type = std::remove_cvref_t<T>;

	if constexpr( is_variant_v<type> )
		return std::holds_alternative<target_t>(arg);

	else if constexpr( std::is_same_v<type, std::any> )
	{
		if constexpr( std::is_same_v<target_t, std::any> )
			return true;
		else
			return arg.type() == typeid(target_t);
	}
	else
		return true;
}

template <concepts::function Slot, typename...Args>
[[nodiscard]] RIWO_UTILS_TAPI bool slot_args_can_auto_cast(Args&&...call_args) noexcept
{
	using slot_tr = function_traits<Slot>;
	using indices = std::make_index_sequence<slot_tr::arg_count>;

	// Validation only inspects the arguments. Keep references here so a large
	// value is not copied before every slot invocation.
	return [args = std::forward_as_tuple(std::forward<Args>(call_args)...)]
	<size_t...Is>(std::index_sequence<Is...>) mutable noexcept
	{
		return ([&]() mutable noexcept
		{
			using arg_t = slot_tr::template arg_type_t<Is>;
			return slot_arg_can_auto_cast<arg_t>(std::get<Is>(args));
		}
		() and ...);
	}
	(indices());
}

template <typename T>
using signal_arg_lvalue_t = std::conditional_t <
	std::is_lvalue_reference_v<T>, T, std::add_lvalue_reference_t <
		std::add_const_t<std::remove_reference_t<T>>
	>
>;

template <concepts::function Slot>
constexpr bool slot_args_borrowable_v = []<size_t...Is>(std::index_sequence<Is...>) consteval
{
	using slot_tr = function_traits<Slot>;
	return (not std::is_rvalue_reference_v<typename slot_tr::template arg_type_t<Is>> and ...);
}
(std::make_index_sequence<function_traits<Slot>::arg_count>{});

template <typename Tag, typename T>
[[nodiscard]] RIWO_UTILS_TAPI constexpr decltype(auto) slot_arg_auto_cast(T &&arg)
{
	using target_t = std::remove_cvref_t<Tag>;
	using type = std::remove_cvref_t<T>;

	if constexpr( is_variant_v<type> )
		return std::get<target_t>(std::forward<T>(arg));

	else if constexpr( std::is_same_v<type, std::any> )
	{
		if constexpr( std::is_same_v<target_t, std::any> )
			return std::forward<T>(arg);
		else
			return std::any_cast<Tag>(std::forward<T>(arg));
	}
	else
		return arg_converter<T,Tag>::convert(std::forward<T>(arg));
}

template <typename Source, typename Tag, typename T>
[[nodiscard]] RIWO_UTILS_TAPI constexpr decltype(auto) borrowed_slot_arg_auto_cast(T &&arg)
{
	using target_t = std::remove_cvref_t<Tag>;
	using type = std::remove_cvref_t<T>;

	if constexpr( is_variant_v<type> )
		return std::get<target_t>(std::forward<T>(arg));

	else if constexpr( std::is_same_v<type, std::any> )
	{
		if constexpr( std::is_same_v<target_t, std::any> )
			return std::forward<T>(arg);
		else
			return std::any_cast<Tag>(std::forward<T>(arg));
	}
	else
		return arg_converter<Source,Tag>::convert(std::forward<T>(arg));
}

template <concepts::function Signal>
class slot_adapter; // Type erasure

template <typename Ret, typename...Args>
class RIWO_UTILS_TAPI slot_adapter<Ret(Args...)> :
	public std::enable_shared_from_this<slot_adapter<Ret(Args...)>>
{
	RIWO_DISABLE_COPY_MOVE(slot_adapter)

public:
	using ptr_t = std::shared_ptr<slot_adapter>;
	using func_t = std::future<void>(Args...);
	slot_adapter() = default;

	template <slot_mode Mode, typename...Args0>
	[[nodiscard]] static ptr_t make(Args0&&...args) noexcept
	{
		auto obj = std::make_shared<slot_adapter>();
		obj->template emplace<Mode>(std::forward<Args0>(args)...);
		return obj;
	}

	template <typename...Args0>
	[[nodiscard]] std::future<void> operator()(Args0&&...args) noexcept {
		return m_func(std::forward<Args0>(args)...);
	}

	template <typename...Args0>
	void invoke(Args0&&...args) noexcept {
		m_invoke(std::forward<Args0>(args)...);
	}

	template <typename...Args0>
	void invoke_borrowed(Args0&&...args) noexcept {
		m_borrowed_invoke(std::forward<Args0>(args)...);
	}

	[[nodiscard]] bool is_valid() const noexcept {
		return not m_is_valid or m_is_valid();
	}

private:
	template <slot_mode Mode, concepts::function Slot>
	void emplace(Slot &&slot) noexcept {
		emplace<Mode>(io_context(), std::forward<Slot>(slot));
	}

	template <slot_mode Mode, concepts::sched Exec, concepts::function Slot>
	void emplace(Exec &&executor_arg, Slot &&slot_arg) noexcept
	{
		RIWO_UNUSED(executor_arg);
		if constexpr( Mode == slot_mode::sync )
			m_block = true;

		if constexpr( Mode == slot_mode::backpressure )
			m_backpressure = true;

		if constexpr( Mode == slot_mode::backpressure )
		{
			m_func =
			[exec = get_executor_helper(std::forward<Exec>(executor_arg)), slot = std::forward<Slot>(slot_arg)]
			(Args...args) mutable noexcept
			{
				if( not slot_args_can_auto_cast<Slot>(args...) )
					return std::future<void> {};
				return glob_backpressure_call(exec, slot, std::move(args)...);
			};
		}
		else if constexpr( Mode == slot_mode::sync and slot_args_borrowable_v<Slot> )
		{
			m_borrowed = true;
			m_borrowed_invoke = [slot = std::forward<Slot>(slot_arg)]
			(signal_arg_lvalue_t<Args>...args) mutable noexcept
			{
				if( not slot_args_can_auto_cast<Slot>(args...) )
					return ;
				glob_borrowed_call(slot, args...);
			};
		}
		else
		{
			m_invoke =
			[exec = get_executor_helper(std::forward<Exec>(executor_arg)), slot = std::forward<Slot>(slot_arg)]
			(Args...args) mutable noexcept
			{
				if( not slot_args_can_auto_cast<Slot>(args...) )
					return ;
				if constexpr( Mode == slot_mode::sync )
					(void) glob_sync_call(slot, std::move(args)...);
				else
					(void) glob_async_call(exec, slot, std::move(args)...);
			};
		}
	}

	template <typename Slot, typename...Args0>
	static void glob_borrowed_call(Slot &slot_fn, Args0&...call_args) noexcept
	{
		using slot_tr = function_traits<Slot>;
		using indices = std::make_index_sequence<slot_tr::arg_count>;
		auto args = std::forward_as_tuple(call_args...);

		[&slot_fn, &args]<size_t...Is>(std::index_sequence<Is...>) mutable noexcept {
			slot_fn(get_borrowed_slot_arg<slot_tr, Is>(args)...);
		}(indices());
	}

	template <typename Slot, typename...Args0>
	[[nodiscard]] static std::future<void> glob_sync_call(Slot &slot_fn, Args0&&...call_args) noexcept
	{
		using slot_tr = function_traits<Slot>;
		using indices = std::make_index_sequence<slot_tr::arg_count>;

		[&slot_fn, args = std::make_tuple(std::forward<Args0>(call_args)...)]
		<size_t...Is>(std::index_sequence<Is...>) mutable noexcept {
			slot_fn(get_slot_arg<slot_tr, Is>(args)...);
		} (indices());
		return {};
	}

	template <typename Slot, typename...Args0>
	[[nodiscard]] static std::future<void> glob_async_call(auto &exec, Slot slot_fn, Args0&&...call_args) noexcept
	{
		using slot_tr = function_traits<Slot>;
		using indices = std::make_index_sequence<slot_tr::arg_count>;
		using return_t = function_traits<Slot>::return_type;

		if constexpr( is_awaitable_v<return_t> )
		{
			riwo::post(exec, [slot = std::move(slot_fn), args = std::make_tuple(std::move(call_args)...)]
			() mutable noexcept -> awaitable<void>
			{
				co_await [&]<size_t...Is>(std::index_sequence<Is...>) mutable noexcept {
					return slot(get_slot_arg<slot_tr, Is>(args)...);
				} (indices());
				co_return ;
			});
		}
		else
		{
			riwo::post(exec,
			[slot = std::move(slot_fn), args = std::make_tuple(std::move(call_args)...)]() mutable noexcept
			{
				[&]<size_t...Is>(std::index_sequence<Is...>) mutable noexcept {
					slot(get_slot_arg<slot_tr, Is>(args)...);
				} (indices());
			});
		}
		return {};
	}

	template <typename Slot, typename...Args0>
	[[nodiscard]] static std::future<void> glob_backpressure_call
	(auto &exec, Slot slot_fn, Args0&&...call_args) noexcept
	{
		using slot_tr = function_traits<Slot>;
		using indices = std::make_index_sequence<slot_tr::arg_count>;
		using return_t = function_traits<Slot>::return_type;

		if constexpr( is_awaitable_v<return_t> )
		{
			return riwo::dispatch(exec, [
				slot = std::move(slot_fn), args = std::make_tuple(std::move(call_args)...)
			]() mutable noexcept -> awaitable<void>
			{
				co_await [&]<size_t...Is>(std::index_sequence<Is...>) mutable noexcept {
					return slot(get_slot_arg<slot_tr, Is>(args)...);
				} (indices());
				co_return ;
			},
			use_future);
		}
		else
		{
			return riwo::dispatch(exec,
			[slot = std::move(slot_fn), args = std::make_tuple(std::move(call_args)...)]() mutable noexcept
			{
				[&]<size_t...Is>(std::index_sequence<Is...>) mutable noexcept {
					slot(get_slot_arg<slot_tr, Is>(args)...);
				} (indices());
			},
			use_future);
		}
	}

	template <typename Slot, typename...Args0>
	static void obj_borrowed_call
	(auto object_ptr, Slot &slot_fn, Args0&...call_args) noexcept
	{
		using slot_tr = function_traits<Slot>;
		using indices = std::make_index_sequence<slot_tr::arg_count>;
		auto args = std::forward_as_tuple(call_args...);

		[&object_ptr, &slot_fn, &args]<size_t...Is>(std::index_sequence<Is...>) mutable noexcept {
			(object_ptr.get()->*slot_fn)(get_borrowed_slot_arg<slot_tr, Is>(args)...);
		}(indices());
	}

	template <typename Slot, typename...Args0>
	[[nodiscard]] static std::future<void> guarded_glob_async_call
	(auto &exec, auto weak_observer, Slot slot_fn, Args0&&...call_args) noexcept
	{
		using slot_tr = function_traits<Slot>;
		using indices = std::make_index_sequence<slot_tr::arg_count>;
		using return_t = function_traits<Slot>::return_type;

		if constexpr( is_awaitable_v<return_t> )
		{
			riwo::post(exec, [weak = std::move(weak_observer),
				slot = std::move(slot_fn), args = std::make_tuple(std::move(call_args)...)
			]() mutable noexcept -> awaitable<void>
			{
				auto guard = weak.lock();
				if( not guard )
					co_return ;

				co_await [&]<size_t...Is>(std::index_sequence<Is...>) mutable noexcept {
					return slot(get_slot_arg<slot_tr, Is>(args)...);
				} (indices());
				co_return ;
			});
		}
		else
		{
			riwo::post(exec, [weak = std::move(weak_observer),
				slot = std::move(slot_fn), args = std::make_tuple(std::move(call_args)...)
			]() mutable noexcept
			{
				auto guard = weak.lock();
				if( not guard )
					return ;

				[&]<size_t...Is>(std::index_sequence<Is...>) mutable noexcept {
					slot(get_slot_arg<slot_tr, Is>(args)...);
				} (indices());
			});
		}
		return {};
	}

private:
	template <slot_mode Mode, typename Obj, concepts::function Slot>
	void emplace(Obj &&obj, Slot &&slot) noexcept {
		emplace<Mode>(std::forward<Obj>(obj), io_context(), std::forward<Slot>(slot));
	}

	template <slot_mode Mode, typename Obj, concepts::sched Exec, concepts::function Slot>
	void emplace(Obj &&observer, Exec &&executor_arg, Slot &&slot_arg) noexcept
	{
		RIWO_UNUSED(executor_arg);
		m_obj = observer.get();

		if constexpr( Mode != slot_mode::async )
			m_block = true;

		if constexpr( Mode == slot_mode::backpressure )
			m_backpressure = true;

		using obj_t = std::remove_cvref_t<Obj>::element_type;
		auto weak = std::weak_ptr<obj_t>(observer);

		m_is_valid = [obj = weak] {
			return not obj.expired();
		};
		if constexpr( Mode == slot_mode::backpressure )
		{
			m_func = [obj = std::move(weak),
				exec = get_executor_helper(std::forward<Exec>(executor_arg)),
				slot = std::forward<Slot>(slot_arg)
			](Args...args) mutable noexcept
			{
				if( not slot_args_can_auto_cast<Slot>(args...) )
					return std::future<void> {};

				auto object = obj.lock();
				if( not object )
					return std::future<void> {};

				using slot_tr = function_traits<Slot>;
				if constexpr( slot_tr::is_member_func )
					return obj_backpressure_call(exec, std::move(object), slot, std::move(args)...);
				else
					return glob_backpressure_call(exec, slot, std::move(args)...);
			};
		}
		else if constexpr( Mode == slot_mode::sync and slot_args_borrowable_v<Slot> )
		{
			m_borrowed = true;
			m_borrowed_invoke =
			[obj = std::move(weak), slot = std::forward<Slot>(slot_arg)]
			(signal_arg_lvalue_t<Args>...args) mutable noexcept
			{
				if( not slot_args_can_auto_cast<Slot>(args...) )
					return ;

				auto object = obj.lock();
				if( not object )
					return ;

				using slot_tr = function_traits<Slot>;
				if constexpr( slot_tr::is_member_func )
					obj_borrowed_call(std::move(object), slot, args...);
				else
					glob_borrowed_call(slot, args...);
			};
		}
		else
		{
			m_invoke = [obj = std::move(weak),
				exec = get_executor_helper(std::forward<Exec>(executor_arg)),
				slot = std::forward<Slot>(slot_arg)
			](Args...args) mutable noexcept
			{
				if( not slot_args_can_auto_cast<Slot>(args...) )
					return ;

				using slot_tr = function_traits<Slot>;
				if constexpr( Mode == slot_mode::async )
				{
					if constexpr( slot_tr::is_member_func )
						(void) obj_async_call(exec, obj, slot, std::move(args)...);
					else
						(void) guarded_glob_async_call(exec, obj, slot, std::move(args)...);
				}
				else
				{
					auto object = obj.lock();
					if( not object )
						return ;

					if constexpr( slot_tr::is_member_func )
						(void) obj_sync_call(std::move(object), slot, std::move(args)...);
					else
						(void) glob_sync_call(slot, std::move(args)...);
				}
			};
		}
	}

	template <typename Slot, typename...Args0>
	[[nodiscard]] static std::future<void> obj_sync_call
	(auto object_ptr, Slot &slot_fn, Args0&&...call_args) noexcept
	{
		using slot_tr = function_traits<Slot>;
		using indices = std::make_index_sequence<slot_tr::arg_count>;

		[&object_ptr, &slot_fn, args = std::make_tuple(std::forward<Args0>(call_args)...)]
		<size_t...Is>(std::index_sequence<Is...>) mutable noexcept {
			(object_ptr.get()->*slot_fn)(get_slot_arg<slot_tr, Is>(args)...);
		} (indices());
		return {};
	}

	template <typename Slot, typename...Args0>
	[[nodiscard]] static std::future<void> obj_async_call
	(auto &exec, auto weak_observer, Slot slot_fn, Args0&&...call_args) noexcept
	{
		using slot_tr = function_traits<Slot>;
		using indices = std::make_index_sequence<slot_tr::arg_count>;
		using return_t = function_traits<Slot>::return_type;

		if constexpr( is_awaitable_v<return_t> )
		{
			riwo::post(exec, [weak = std::move(weak_observer),
				slot = std::move(slot_fn), args = std::make_tuple(std::move(call_args)...)
			]() mutable noexcept -> awaitable<void>
			{
				auto obj = weak.lock();
				if( not obj )
					co_return ;

				co_await [&]<size_t...Is>(std::index_sequence<Is...>) mutable noexcept {
					return (obj.get()->*slot)(get_slot_arg<slot_tr, Is>(args)...);
				} (indices());
				co_return ;
			});
		}
		else
		{
			riwo::post(exec, [weak = std::move(weak_observer),
				slot = std::move(slot_fn), args = std::make_tuple(std::move(call_args)...)
			]() mutable noexcept
			{
				auto obj = weak.lock();
				if( not obj )
					return ;

				[&]<size_t...Is>(std::index_sequence<Is...>) mutable noexcept {
					(obj.get()->*slot)(get_slot_arg<slot_tr, Is>(args)...);
				} (indices());
			});
		}
		return {};
	}

	template <typename Slot, typename...Args0>
	[[nodiscard]] static std::future<void> obj_backpressure_call
	(auto &exec, auto object_ptr, Slot slot_fn, Args0&&...call_args) noexcept
	{
		using slot_tr = function_traits<Slot>;
		using indices = std::make_index_sequence<slot_tr::arg_count>;
		using return_t = function_traits<Slot>::return_type;

		if constexpr( is_awaitable_v<return_t> )
		{
			auto future = riwo::dispatch(exec, [obj = std::move(object_ptr),
				slot = std::move(slot_fn), args = std::make_tuple(std::move(call_args)...)
			]() mutable noexcept -> awaitable<void>
			{
				co_await [&]<size_t...Is>(std::index_sequence<Is...>) mutable noexcept {
					return (obj.get()->*slot)(get_slot_arg<slot_tr, Is>(args)...);
				} (indices());
				co_return ;
			},
			use_future);
			future.wait();
			return future;
		}
		else
		{
			auto future = riwo::dispatch(exec, [obj = std::move(object_ptr),
				slot = std::move(slot_fn), args = std::make_tuple(std::move(call_args)...)
			]() mutable noexcept
			{
				[&]<size_t...Is>(std::index_sequence<Is...>) mutable noexcept {
					(obj.get()->*slot)(get_slot_arg<slot_tr, Is>(args)...);
				} (indices());
			},
			use_future);
			future.wait();
			return future;
		}
	}

private:
	template <typename SlotTr, size_t I>
	[[nodiscard]] static decltype(auto) get_borrowed_slot_arg(auto &args) noexcept
	{
		using arg_t = SlotTr::template arg_type_t<I>;
		using source_t = std::tuple_element_t<I,std::tuple<Args...>>;
		return borrowed_slot_arg_auto_cast<source_t,arg_t>(std::get<I>(args));
	}

	template <typename SlotTr, size_t I>
	[[nodiscard]] static decltype(auto) get_slot_arg(auto &args) noexcept
	{
		using arg_t = SlotTr::template arg_type_t<I>;
		return slot_arg_auto_cast<arg_t>(std::move(std::get<I>(args)));
	}

public:
	const void *m_obj = nullptr;
	std::function<bool()> m_is_valid {};

	bool m_block = false;
	bool m_backpressure = false;
	bool m_borrowed = false;

	std::function<void(signal_arg_lvalue_t<Args>...)> m_borrowed_invoke {};
	std::function<void(Args...)> m_invoke {};
	std::function<func_t> m_func {};
};

template <typename Ret, typename...Args>
class RIWO_UTILS_TAPI slot_adapter<awaitable<Ret>(Args...)> :
	public std::enable_shared_from_this<slot_adapter<awaitable<Ret>(Args...)>>
{
	RIWO_DISABLE_COPY_MOVE(slot_adapter)

public:
	using ptr_t = std::shared_ptr<slot_adapter>;
	using func_t = awaitable<void>(Args...);
	slot_adapter() = default;

	template <slot_mode Mode, typename...Args0>
	[[nodiscard]] static ptr_t make(Args0&&...args) noexcept
	{
		auto obj = std::make_shared<slot_adapter>();
		obj->template emplace<Mode>(std::forward<Args0>(args)...);
		return obj;
	}

	template <typename...Args0>
	[[nodiscard]] awaitable<void> operator()(Args0&&...args) noexcept {
		co_return co_await m_func(std::forward<Args0>(args)...);
	}

	template <typename...Args0>
	[[nodiscard]] awaitable<void> invoke_borrowed(Args0&&...args) noexcept {
		co_return co_await m_borrowed_func(std::forward<Args0>(args)...);
	}

	[[nodiscard]] bool is_valid() const noexcept {
		return not m_is_valid or m_is_valid();
	}

private:
	template <slot_mode Mode, concepts::function Slot>
	void emplace(Slot &&slot) noexcept {
		emplace<Mode>(io_context(), std::forward<Slot>(slot));
	}

	template <slot_mode Mode, concepts::sched Exec, concepts::function Slot>
	void emplace(Exec &&executor_arg, Slot &&slot_arg) noexcept
	{
		RIWO_UNUSED(executor_arg);
		if constexpr( Mode == slot_mode::sync and slot_args_borrowable_v<Slot> )
		{
			m_borrowed = true;
			m_borrowed_func = [slot = std::forward<Slot>(slot_arg)]
			(signal_arg_lvalue_t<Args>...args) mutable noexcept -> awaitable<void>
			{
				if( not slot_args_can_auto_cast<Slot>(args...) )
					co_return ;
				co_await glob_borrowed_call(slot, args...);
				co_return ;
			};
		}
		else
		{
			m_func = [exec = get_executor_helper(std::forward<Exec>(executor_arg)),
			slot = std::forward<Slot>(slot_arg)](Args...args) mutable noexcept -> awaitable<void>
			{
				if( not slot_args_can_auto_cast<Slot>(args...) )
					co_return ;

				if constexpr( Mode == slot_mode::sync )
					co_await glob_sync_call(slot, std::move(args)...);

				else if constexpr( Mode == slot_mode::async )
					glob_async_call(exec, slot, std::move(args)...);

				else /* if constexpr( Mode == slot_mode::backpressure ) */
					co_await glob_backpressure_call(exec, slot, std::move(args)...);
				co_return ;
			};
		}
	}

	template <typename Slot, typename...Args0>
	[[nodiscard]] static awaitable<void> glob_borrowed_call
	(Slot &slot_fn, Args0&...call_args) noexcept
	{
		using slot_tr = function_traits<Slot>;
		using indices = std::make_index_sequence<slot_tr::arg_count>;
		using return_t = function_traits<Slot>::return_type;
		auto args = std::forward_as_tuple(call_args...);

		if constexpr( is_awaitable_v<return_t> )
		{
			co_await [&slot_fn, &args]<size_t...Is>(std::index_sequence<Is...>) mutable noexcept {
				return slot_fn(get_borrowed_slot_arg<slot_tr, Is>(args)...);
			}(indices());
		}
		else
		{
			[&slot_fn, &args]<size_t...Is>(std::index_sequence<Is...>) mutable noexcept {
				slot_fn(get_borrowed_slot_arg<slot_tr, Is>(args)...);
			}(indices());
		}
		co_return ;
	}

	template <typename Slot, typename...Args0>
	[[nodiscard]] static awaitable<void> glob_sync_call(Slot &slot_fn, Args0&&...call_args) noexcept
	{
		using slot_tr = function_traits<Slot>;
		using indices = std::make_index_sequence<slot_tr::arg_count>;
		using return_t = function_traits<Slot>::return_type;

		if constexpr( is_awaitable_v<return_t> )
		{
			auto args = std::make_tuple(std::forward<Args0>(call_args)...);
			co_await [&slot_fn, &args]<size_t...Is>(std::index_sequence<Is...>) mutable noexcept {
				return slot_fn(get_slot_arg<slot_tr, Is>(args)...);
			} (indices());
		}
		else
		{
			[&slot_fn, args = std::make_tuple(std::forward<Args0>(call_args)...)]
			<size_t...Is>(std::index_sequence<Is...>) mutable noexcept {
				slot_fn(get_slot_arg<slot_tr, Is>(args)...);
			} (indices());
		}
		co_return ;
	}

	template <typename Slot, typename...Args0>
	static void glob_async_call(auto &exec, Slot slot_fn, Args0&&...call_args) noexcept
	{
		using slot_tr = function_traits<Slot>;
		using indices = std::make_index_sequence<slot_tr::arg_count>;
		using return_t = function_traits<Slot>::return_type;

		if constexpr( is_awaitable_v<return_t> )
		{
			riwo::post(exec, [slot = std::move(slot_fn), args = std::make_tuple(std::move(call_args)...)]
			() mutable noexcept -> awaitable<void>
			{
				co_await [&]<size_t...Is>(std::index_sequence<Is...>) mutable noexcept {
					return slot(get_slot_arg<slot_tr, Is>(args)...);
				} (indices());
				co_return ;
			});
		}
		else
		{
			riwo::post(exec,
			[slot = std::move(slot_fn), args = std::make_tuple(std::move(call_args)...)]() mutable noexcept
			{
				[&]<size_t...Is>(std::index_sequence<Is...>) mutable noexcept {
					slot(get_slot_arg<slot_tr, Is>(args)...);
				} (indices());
			});
		}
	}

	template <typename Slot, typename...Args0>
	[[nodiscard]] static awaitable<void> glob_backpressure_call(auto &exec, Slot slot_fn, Args0&&...call_args) noexcept
	{
		using slot_tr = function_traits<Slot>;
		using indices = std::make_index_sequence<slot_tr::arg_count>;
		using return_t = function_traits<Slot>::return_type;

		if constexpr( is_awaitable_v<return_t> )
		{
			co_await riwo::dispatch(exec, [slot = std::move(slot_fn), args = std::make_tuple(std::move(call_args)...)]
			() mutable noexcept -> awaitable<void>
			{
				co_await [&]<size_t...Is>(std::index_sequence<Is...>) mutable noexcept {
					return slot(get_slot_arg<slot_tr, Is>(args)...);
				} (indices());
				co_return ;
			},
			use_awaitable);
		}
		else
		{
			co_await riwo::dispatch(exec,
			[slot = std::move(slot_fn), args = std::make_tuple(std::move(call_args)...)]() mutable noexcept
			{
				[&]<size_t...Is>(std::index_sequence<Is...>) mutable noexcept {
					slot(get_slot_arg<slot_tr, Is>(args)...);
				} (indices());
			},
			use_awaitable);
		}
		co_return ;
	}

	template <typename Slot, typename...Args0>
	static void guarded_glob_async_call
	(auto &exec, auto weak_observer, Slot slot_fn, Args0&&...call_args) noexcept
	{
		using slot_tr = function_traits<Slot>;
		using indices = std::make_index_sequence<slot_tr::arg_count>;
		using return_t = function_traits<Slot>::return_type;

		if constexpr( is_awaitable_v<return_t> )
		{
			riwo::post(exec, [weak = std::move(weak_observer),
				slot = std::move(slot_fn), args = std::make_tuple(std::move(call_args)...)
			]() mutable noexcept -> awaitable<void>
			{
				auto guard = weak.lock();
				if( not guard )
					co_return ;

				co_await [&]<size_t...Is>(std::index_sequence<Is...>) mutable noexcept {
					return slot(get_slot_arg<slot_tr, Is>(args)...);
				} (indices());
				co_return ;
			});
		}
		else
		{
			riwo::post(exec, [weak = std::move(weak_observer),
				slot = std::move(slot_fn), args = std::make_tuple(std::move(call_args)...)
			]() mutable noexcept
			{
				auto guard = weak.lock();
				if( not guard )
					return ;

				[&]<size_t...Is>(std::index_sequence<Is...>) mutable noexcept {
					slot(get_slot_arg<slot_tr, Is>(args)...);
				} (indices());
			});
		}
	}

private:
	template <slot_mode Mode, typename Obj, concepts::function Slot>
	void emplace(Obj &&obj, Slot &&slot) noexcept {
		emplace<Mode>(std::forward<Obj>(obj), io_context(), std::forward<Slot>(slot));
	}

	template <slot_mode Mode, typename Obj, concepts::sched Exec, concepts::function Slot>
	void emplace(Obj &&observer, Exec &&executor_arg, Slot &&slot_arg) noexcept
	{
		RIWO_UNUSED(executor_arg);
		using obj_t = std::remove_cvref_t<Obj>::element_type;
		m_obj = observer.get();

		auto weak = std::weak_ptr<obj_t>(observer);
		m_is_valid = [obj = weak] {
			return not obj.expired();
		};
		if constexpr( Mode == slot_mode::sync and slot_args_borrowable_v<Slot> )
		{
			m_borrowed = true;
			m_borrowed_func =
			[obj = std::move(weak), slot = std::forward<Slot>(slot_arg)]
			(signal_arg_lvalue_t<Args>...args) mutable noexcept -> awaitable<void>
			{
				if( not slot_args_can_auto_cast<Slot>(args...) )
					co_return ;

				auto object = obj.lock();
				if( not object )
					co_return ;

				using slot_tr = function_traits<Slot>;
				if constexpr( slot_tr::is_member_func )
					co_await obj_borrowed_call(std::move(object), slot, args...);
				else
					co_await glob_borrowed_call(slot, args...);
				co_return ;
			};
		}
		else
		{
			m_func = [obj = std::move(weak),
			exec = get_executor_helper(std::forward<Exec>(executor_arg)),
			slot = std::forward<Slot>(slot_arg)](Args...args) mutable noexcept -> awaitable<void>
			{
				if( not slot_args_can_auto_cast<Slot>(args...) )
					co_return ;

				using slot_tr = function_traits<Slot>;
				if constexpr( Mode == slot_mode::async )
				{
					if constexpr( slot_tr::is_member_func )
						obj_async_call(exec, obj, slot, std::move(args)...);
					else
						guarded_glob_async_call(exec, obj, slot, std::move(args)...);
				}
				else
				{
					auto object = obj.lock();
					if( not object )
						co_return ;

					if constexpr( slot_tr::is_member_func )
					{
						if constexpr( Mode == slot_mode::sync )
							co_await obj_sync_call(std::move(object), slot, std::move(args)...);
						else
							co_await obj_backpressure_call(exec, std::move(object), slot, std::move(args)...);
					}
					else if constexpr( Mode == slot_mode::sync )
						co_await glob_sync_call(slot, std::move(args)...);
					else
						co_await glob_backpressure_call(exec, slot, std::move(args)...);
				}
				co_return ;
			};
		}
	}

	template <typename Slot, typename...Args0>
	[[nodiscard]] static awaitable<void> obj_borrowed_call
	(auto object_ptr, Slot &slot_fn, Args0&...call_args) noexcept
	{
		using slot_tr = function_traits<Slot>;
		using indices = std::make_index_sequence<slot_tr::arg_count>;
		using return_t = function_traits<Slot>::return_type;
		auto args = std::forward_as_tuple(call_args...);

		if constexpr( is_awaitable_v<return_t> )
		{
			co_await [&object_ptr, &slot_fn, &args]
			<size_t...Is>(std::index_sequence<Is...>) mutable noexcept
			{
				return (object_ptr.get()->*slot_fn)(
					get_borrowed_slot_arg<slot_tr, Is>(args)...
				);
			}
			(indices());
		}
		else
		{
			[&object_ptr, &slot_fn, &args]
			<size_t...Is>(std::index_sequence<Is...>) mutable noexcept
			{
				(object_ptr.get()->*slot_fn)(
					get_borrowed_slot_arg<slot_tr, Is>(args)...
				);
			}
			(indices());
		}
		co_return ;
	}

	template <typename Slot, typename...Args0>
	[[nodiscard]] static awaitable<void> obj_sync_call(auto object_ptr, Slot &slot_fn, Args0&&...call_args) noexcept
	{
		using slot_tr = function_traits<Slot>;
		using indices = std::make_index_sequence<slot_tr::arg_count>;
		using return_t = function_traits<Slot>::return_type;

		if constexpr( is_awaitable_v<return_t> )
		{
			auto args = std::make_tuple(std::forward<Args0>(call_args)...);
			co_await [&object_ptr, &slot_fn, &args]
			<size_t...Is>(std::index_sequence<Is...>) mutable noexcept {
				return (object_ptr.get()->*slot_fn)(get_slot_arg<slot_tr, Is>(args)...);
			} (indices());
		}
		else
		{
			[&object_ptr, &slot_fn, args = std::make_tuple(std::forward<Args0>(call_args)...)]
			<size_t...Is>(std::index_sequence<Is...>) mutable noexcept {
				(object_ptr.get()->*slot_fn)(get_slot_arg<slot_tr, Is>(args)...);
			} (indices());
		}
		co_return ;
	}

	template <typename Slot, typename...Args0>
	static void obj_async_call(auto &exec, auto weak_observer, Slot slot_fn, Args0&&...call_args) noexcept
	{
		using slot_tr = function_traits<Slot>;
		using indices = std::make_index_sequence<slot_tr::arg_count>;
		using return_t = function_traits<Slot>::return_type;

		if constexpr( is_awaitable_v<return_t> )
		{
			riwo::post(exec, [weak = std::move(weak_observer),
				slot = std::move(slot_fn), args = std::make_tuple(std::move(call_args)...)
			]() mutable noexcept -> awaitable<void>
			{
				auto obj = weak.lock();
				if( not obj )
					co_return ;

				co_await [&]<size_t...Is>(std::index_sequence<Is...>) mutable noexcept {
					return (obj.get()->*slot)(get_slot_arg<slot_tr, Is>(args)...);
				} (indices());
				co_return ;
			});
		}
		else
		{
			riwo::post(exec, [weak = std::move(weak_observer),
				slot = std::move(slot_fn), args = std::make_tuple(std::move(call_args)...)
			]() mutable noexcept
			{
				auto obj = weak.lock();
				if( not obj )
					return ;

				[&]<size_t...Is>(std::index_sequence<Is...>) mutable noexcept {
					(obj.get()->*slot)(get_slot_arg<slot_tr, Is>(args)...);
				} (indices());
			});
		}
	}

	template <typename Slot, typename...Args0>
	[[nodiscard]] static awaitable<void> obj_backpressure_call
	(auto &exec, auto object_ptr, Slot slot_fn, Args0&&...call_args) noexcept
	{
		using slot_tr = function_traits<Slot>;
		using indices = std::make_index_sequence<slot_tr::arg_count>;
		using return_t = function_traits<Slot>::return_type;

		if constexpr( is_awaitable_v<return_t> )
		{
			co_await riwo::dispatch(exec, [obj = std::move(object_ptr),
				slot = std::move(slot_fn), args = std::make_tuple(std::move(call_args)...)
			]() mutable noexcept -> awaitable<void>
			{
				co_await [&]<size_t...Is>(std::index_sequence<Is...>) mutable noexcept {
					return (obj.get()->*slot)(get_slot_arg<slot_tr, Is>(args)...);
				} (indices());
				co_return ;
			},
			use_awaitable);
		}
		else
		{
			co_await riwo::dispatch(exec, [obj = std::move(object_ptr),
				slot = std::move(slot_fn), args = std::make_tuple(std::move(call_args)...)
			]() mutable noexcept -> awaitable<void>
			{
				[&]<size_t...Is>(std::index_sequence<Is...>) mutable noexcept {
					(obj.get()->*slot)(get_slot_arg<slot_tr, Is>(args)...);
				} (indices());
				co_return ;
			},
			use_awaitable);
		}
		co_return ;
	}

private:
	template <typename SlotTr, size_t I>
	[[nodiscard]] static decltype(auto) get_borrowed_slot_arg(auto &args) noexcept
	{
		using arg_t = SlotTr::template arg_type_t<I>;
		using source_t = std::tuple_element_t<I,std::tuple<Args...>>;
		return borrowed_slot_arg_auto_cast<source_t,arg_t>(std::get<I>(args));
	}

	template <typename SlotTr, size_t I>
	[[nodiscard]] static decltype(auto) get_slot_arg(auto &args) noexcept
	{
		using arg_t = SlotTr::template arg_type_t<I>;
		return slot_arg_auto_cast<arg_t>(std::move(std::get<I>(args)));
	}

public:
	const void *m_obj = nullptr;
	std::function<bool()> m_is_valid {};

	bool m_borrowed = false;
	std::function<awaitable<void>(signal_arg_lvalue_t<Args>...)> m_borrowed_func {};
	std::function<func_t> m_func {};
};

} //namespace detail

template <typename T, typename Tag>
decltype(auto) arg_converter<T,Tag>::convert(const t_t &value) requires valid
{
	if constexpr( std::is_same_v<std::remove_cvref_t<T>, std::remove_cvref_t<Tag>> )
		return static_cast<const tag_t&>(value);
	else
		return static_cast<tag_t>(value);
}

template <typename T, typename Tag>
decltype(auto) arg_converter<T,Tag>::convert(t_t &&value) requires valid
{
	if constexpr( std::is_same_v<std::remove_cvref_t<T>, std::remove_cvref_t<Tag>> )
		return static_cast<tag_t&&>(value);
	else
		return static_cast<tag_t>(std::move(value));
}

template <typename T, typename Tag>
decltype(auto) arg_converter<T,Tag>::convert(t_t &value) requires valid
{
	if constexpr( std::is_same_v<std::remove_cvref_t<T>, std::remove_cvref_t<Tag>> )
		return static_cast<tag_t&>(value);
	else
		return static_cast<tag_t>(value);
}

template <typename Derived, concepts::std_func_temp Func>
class RIWO_UTILS_TAPI signal_base<Derived,Func>::impl
{
	RIWO_DISABLE_COPY_MOVE(impl)

public:
	using adapter = detail::slot_adapter<function_t>;
	using adapter_ptr = adapter::ptr_t;

	struct slot_info
	{
		const void *func = nullptr;
		adapter_ptr slot {};
	};

	struct slot_snapshot
	{
		std::vector<adapter_ptr> slots {};
		std::vector<adapter_ptr> nonblock_slots {};
		std::vector<adapter_ptr> block_slots {};
	};

	class snapshot_guard
	{
		RIWO_DISABLE_COPY_MOVE(snapshot_guard)

	public:
		explicit snapshot_guard(const impl &owner) noexcept : m_owner(owner)
		{
			m_owner.m_snapshot_readers.fetch_add(1, std::memory_order_seq_cst);
			m_snapshot = m_owner.m_snapshot.load(std::memory_order_seq_cst);
		}

		~snapshot_guard() {
			m_owner.m_snapshot_readers.fetch_sub(1, std::memory_order_seq_cst);
		}

		[[nodiscard]] const slot_snapshot *operator->() const noexcept {
			return m_snapshot;
		}

	private:
		const impl &m_owner;
		const slot_snapshot *m_snapshot = nullptr;
	};

public:
	impl()
	{
		m_snapshot.store(&m_empty_snapshot, std::memory_order_relaxed);
	}

	void rebuild_snapshot_locked() const
	{
		if( m_slots.empty() )
		{
			m_snapshot.store(&m_empty_snapshot, std::memory_order_seq_cst);
			if( m_snapshot_readers.load(std::memory_order_seq_cst) == 0 )
				m_snapshot_storage.clear();
			return ;
		}

		auto snapshot = std::make_unique<slot_snapshot>();
		if constexpr( is_awaitable_v<typename func_traits_t::return_type> )
		{
			snapshot->slots.reserve(m_slots.size());
			for(const auto &info : m_slots)
				snapshot->slots.emplace_back(info.slot);
		}
		else
		{
			snapshot->nonblock_slots.reserve(m_slots.size());
			snapshot->block_slots.reserve(m_slots.size());
			for(const auto &info : m_slots)
			{
				if( info.slot->m_block )
					snapshot->block_slots.emplace_back(info.slot);
				else
					snapshot->nonblock_slots.emplace_back(info.slot);
			}
		}
		auto snapshot_ptr = snapshot.get();

		m_snapshot_storage.emplace_back(std::move(snapshot));
		m_snapshot.store(snapshot_ptr, std::memory_order_seq_cst);

		if( m_snapshot_readers.load(std::memory_order_seq_cst) == 0 )
			m_snapshot_storage.erase(m_snapshot_storage.begin(), m_snapshot_storage.end() - 1);
	}

	void cleanup_expired() const
	{
		std::lock_guard lock(m_mutex);
		const auto old_size = m_slots.size();

		std::erase_if(m_slots, [](const auto &info) {
			return not info.slot->is_valid();
		});
		if( m_slots.size() != old_size )
			rebuild_snapshot_locked();
	}

public:
	template <slot_mode Mode, typename Func0>
	void connect(Func0 &&func) noexcept requires is_global_slot_v<Mode,Func0> {
		connect<Mode>(io_context(), std::forward<Func0>(func));
	}

	template <slot_mode Mode, concepts::sched Exec0, typename Func0>
	void connect(Exec0 &&exec, Func0 &&func) noexcept
		requires is_global_slot_v<Mode,Func0>
	{
		auto it = m_slots.end();
		const void *identity = nullptr;

		if constexpr( std::is_lvalue_reference_v<Func0> )
		{
			identity = reinterpret_cast<const void*>(std::addressof(func));
			it = std::ranges::find_if(m_slots, [identity](const auto &info) {
				return info.func == identity and info.slot->m_obj == nullptr;
			});
		}
		if( it == m_slots.end() )
			it = m_slots.emplace(m_slots.end());

		it->func = identity;
		it->slot = adapter::template make<Mode>(
			std::forward<Exec0>(exec), std::forward<Func0>(func)
		);
	}

	template <slot_mode Mode, typename Obj, typename Func0>
	void connect(Obj &&observer, Func0 &&func) noexcept
		requires is_obj_slot_v<Mode,Obj,Func0>
	{
		auto it = m_slots.end();
		const void *identity = nullptr;

		if constexpr( std::is_lvalue_reference_v<Func0> )
		{
			identity = reinterpret_cast<const void*>(std::addressof(func));
			it = std::ranges::find_if(m_slots, [&observer, identity](const auto &info) {
				return info.slot->m_obj == observer.get() and info.func == identity;
			});
		}
		if( it == m_slots.end() )
			it = m_slots.emplace(m_slots.end());

		it->func = identity;
		it->slot = adapter::template make<Mode>(
			std::forward<Obj>(observer), std::forward<Func0>(func)
		);
	}

	template <slot_mode Mode, typename Obj, concepts::sched Exec0, typename Func0>
	void connect(Obj &&observer, Exec0 &&exec, Func0 &&func) noexcept
		requires is_obj_slot_v<Mode,Obj,Func0>
	{
		auto it = m_slots.end();
		const void *identity = nullptr;

		if constexpr( std::is_lvalue_reference_v<Func0> )
		{
			identity = reinterpret_cast<const void*>(std::addressof(func));
			it = std::ranges::find_if(m_slots, [&observer, identity](const auto &info) {
				return info.slot->m_obj == observer.get() and info.func == identity;
			});
		}
		if( it == m_slots.end() )
			it = m_slots.emplace(m_slots.end());

		it->func = identity;
		it->slot = adapter::template make<Mode>(std::forward<Obj>(observer),
			std::forward<Exec0>(exec), std::forward<Func0>(func)
		);
	}

public:
	template <typename Func0>
	void disconnect(Func0 &&func) noexcept
		requires is_global_slot_v<slot_mode::async,Func0>
	{
		auto it = std::ranges::find_if(m_slots, [&func](const auto &info){
			return info.func == &func and info.slot->m_obj == nullptr;
		});
		if( it != m_slots.end() )
			m_slots.erase(it);
	}

	template <typename Obj, typename Func0>
	void disconnect(const Obj &observer, Func0 &&func) noexcept
		requires is_obj_slot_v<slot_mode::async,Obj,Func0>
	{
		auto it = std::ranges::find_if(m_slots, [&observer, &func](const auto &info) {
			return info.slot->m_obj == observer.get() and info.func == &func;
		});
		if( it != m_slots.end() )
			m_slots.erase(it);
	}

public:
	template <typename...Args>
	[[nodiscard]] awaitable<void> emit(std::shared_ptr<impl> self, Args&&...args) const noexcept
		requires is_awaitable_v<typename func_traits_t::return_type>
	{
		return emit_with_signature(std::move(self),
			std::make_index_sequence<func_traits_t::arg_count>{},
			std::forward<Args>(args)...
		);
	}

private:
	template <size_t...Is, typename...Args>
	[[nodiscard]] static awaitable<void> emit_with_signature
	(std::shared_ptr<impl> self, std::index_sequence<Is...>, Args&&...args)
	{
		return co_emit<typename func_traits_t::template arg_type_t<Is>...>(
			std::move(self), std::forward<Args>(args)...
		);
	}

	template <typename...Args>
	[[nodiscard]] static awaitable<void> co_emit(std::shared_ptr<impl> self, Args...args)
	{
		if( self->m_block )
			co_return ;

		auto exec = co_await asio::this_coro::executor;
		snapshot_guard snapshot(*self);

		using co_spawn_t = decltype (
			asio::co_spawn(exec, std::declval<awaitable<void>>(), deferred)
		);
		std::vector<co_spawn_t> slots {};
		slots.reserve(snapshot->slots.size());

		bool has_expired = false;
		for(auto &slot : snapshot->slots)
		{
			if( slot->is_valid() )
			{
				if( slot->m_borrowed )
				{
					slots.emplace_back (
						asio::co_spawn(exec, slot->invoke_borrowed(args...), deferred)
					);
				}
				else
				{
					slots.emplace_back (
						asio::co_spawn(exec, (*slot)(args...), deferred)
					);
				}
			}
			else
				has_expired = true;
		}
		if( has_expired )
			self->cleanup_expired();

		if( slots.empty() )
			co_return ;

		auto completion = co_await asio::experimental::make_parallel_group(std::move(slots))
			.async_wait(asio::experimental::wait_for_all(), use_awaitable);

		auto &exs = std::get<1>(completion);
		std::exception_ptr first_ex {};

		for(auto &ex : exs)
		{
			if( not ex )
				continue;
			else if( first_ex )
				throw asio::multiple_exceptions(first_ex);
			first_ex = ex;
		}
		if( first_ex )
			std::rethrow_exception(first_ex);
		co_return ;
	}

public:
	template <typename...Args>
	void emit(Args&&...args) const noexcept
		requires (not is_awaitable_v<typename func_traits_t::return_type>)
	{
		if( m_block	)
			return ;

		snapshot_guard snapshot(*this);
		std::vector<std::future<void>> futures {};

		bool has_expired = false;
		auto invoke = [&](const auto &slots)
		{
			for(auto &slot : slots)
			{
				if( not slot->is_valid() )
				{
					has_expired = true;
					continue;
				}
				if( slot->m_backpressure )
				{
					if( auto future = (*slot)(args...); future.valid() )
						futures.emplace_back(std::move(future));
				}
				else if( slot->m_borrowed )
					slot->invoke_borrowed(args...);
				else
					slot->invoke(args...);
			}
		};
		invoke(snapshot->nonblock_slots);
		invoke(snapshot->block_slots);

		for(auto &future : futures)
			future.wait();

		if( has_expired )
			cleanup_expired();
	}

public:
	std::atomic_bool m_block { false };
	mutable std::vector<slot_info> m_slots;

	// Connection changes are rare. Immutable snapshot generations keep emission
	// lock-free; old generations are reclaimed immediately when no emitter uses one.
	mutable slot_snapshot m_empty_snapshot {};
	mutable std::vector<std::unique_ptr<const slot_snapshot>> m_snapshot_storage;
	mutable std::atomic<const slot_snapshot*> m_snapshot;

	mutable std::atomic_size_t m_snapshot_readers { 0 };
	// Mutation is exclusive and rebuilds allocating snapshot vectors.  Emission
	// is already lock-free, so a reader/writer lock brings no benefit here.
	mutable std::mutex m_mutex;
};

template <typename Derived, concepts::std_func_temp Func>
signal_base<Derived,Func>::signal_base() :
	m_impl(std::make_shared<impl>())
{

}

template <typename Derived, concepts::std_func_temp Func>
signal_base<Derived,Func>::~signal_base() = default;

template <typename Derived, concepts::std_func_temp Func>
template <slot_mode Mode, typename...Slots>
auto signal_base<Derived,Func>::connect(Slots&&...funcs) noexcept -> derived_t&
	requires is_global_slots_v<Mode,Slots...>
{
	auto state = m_impl;
	std::lock_guard lock(state->m_mutex);

	(void) std::initializer_list<int> {(
		state->template connect<Mode>(std::forward<Slots>(funcs)),
	0)...};

	state->rebuild_snapshot_locked();
	return static_cast<derived_t&>(*this);
}

template <typename Derived, concepts::std_func_temp Func>
template <slot_mode Mode, typename Obj, typename...Slots>
auto signal_base<Derived,Func>::connect(Obj &&observer, Slots&&...funcs) -> derived_t&
	requires is_obj_slots_v<Mode,Obj,Slots...>
{
	if( not observer )
		invalid_argument::loc_throw("riwo::utils::signal::connect: observer is nullptr");

	auto state = m_impl;
	std::lock_guard lock(state->m_mutex);

	(void) std::initializer_list<int> {(
		state->template connect<Mode>(observer, std::forward<Slots>(funcs)),
	0)...};

	state->rebuild_snapshot_locked();
	return static_cast<derived_t&>(*this);
}

template <typename Derived, concepts::std_func_temp Func>
template <slot_mode Mode, concepts::sched Exec0, typename...Slots>
auto signal_base<Derived,Func>::connect(Exec0 &&exec, Slots&&...funcs) noexcept -> derived_t&
	requires (Mode != slot_mode::sync) and is_global_slots_v<Mode,Slots...>
{
	auto state = m_impl;
	std::lock_guard lock(state->m_mutex);

	(void) std::initializer_list<int> {(
		state->template connect<Mode>(exec, std::forward<Slots>(funcs)),
	0)...};

	state->rebuild_snapshot_locked();
	return static_cast<derived_t&>(*this);
}

template <typename Derived, concepts::std_func_temp Func>
template <slot_mode Mode, typename Obj, concepts::sched Exec0, typename...Slots>
auto signal_base<Derived,Func>::connect(Obj &&observer, Exec0 &&exec, Slots&&...funcs) -> derived_t&
	requires (Mode != slot_mode::sync) and is_obj_slots_v<Mode,Obj,Slots...>
{
	if( not observer )
		invalid_argument::loc_throw("riwo::utils::signal::connect: observer is nullptr");

	auto state = m_impl;
	std::lock_guard lock(state->m_mutex);

	(void) std::initializer_list<int> {(
		state->template connect<Mode>(observer, exec, std::forward<Slots>(funcs)),
	0)...};

	state->rebuild_snapshot_locked();
	return static_cast<derived_t&>(*this);
}

template <typename Derived, concepts::std_func_temp Func>
template <typename...Slots>
auto signal_base<Derived,Func>::connect(Slots&&...funcs) noexcept -> derived_t&
	requires is_global_slots_def_v<Slots...>
{
	auto state = m_impl;
	std::lock_guard lock(state->m_mutex);

	auto connect_slot = [&state]<typename Slot>(Slot &&func) noexcept
	{
		using slot_t = std::remove_cvref_t<Slot>;
		using slot_ret = function_traits<slot_t>::return_type;
		using sig_ret = func_traits_t::return_type;

		if constexpr( is_awaitable_v<slot_ret> )
		{
			if constexpr( is_awaitable_v<sig_ret> )
				state->template connect<slot_mode::sync>(std::forward<Slot>(func));
			else
				state->template connect<slot_mode::async>(std::forward<Slot>(func));
		}
		else
			state->template connect<slot_mode::sync>(std::forward<Slot>(func));
	};
	(connect_slot(std::forward<Slots>(funcs)), ...);

	state->rebuild_snapshot_locked();
	return static_cast<derived_t&>(*this);
}

template <typename Derived, concepts::std_func_temp Func>
template <typename Obj, typename...Slots>
auto signal_base<Derived,Func>::connect(Obj &&observer_arg, Slots&&...slots) -> derived_t&
	requires is_obj_slots_def_v<Obj,Slots...>
{
	if( not observer_arg )
		invalid_argument::loc_throw("riwo::utils::signal::connect: observer is nullptr");

	auto observer = std::forward<Obj>(observer_arg);
	auto state = m_impl;

	std::lock_guard lock(state->m_mutex);
	auto connect_slot = [&state, &observer]<typename Slot>(Slot &&func) noexcept
	{
		using slot_t = std::remove_cvref_t<Slot>;
		using slot_ret = function_traits<slot_t>::return_type;
		using sig_ret = func_traits_t::return_type;

		if constexpr( is_awaitable_v<slot_ret> )
		{
			if constexpr( is_awaitable_v<sig_ret> )
				state->template connect<slot_mode::sync>(observer, std::forward<Slot>(func));
			else
				state->template connect<slot_mode::async>(observer, std::forward<Slot>(func));
		}
		else
			state->template connect<slot_mode::sync>(observer, std::forward<Slot>(func));
	};
	(connect_slot(std::forward<Slots>(slots)), ...);

	state->rebuild_snapshot_locked();
	return static_cast<derived_t&>(*this);
}

template <typename Derived, concepts::std_func_temp Func>
template <concepts::sched Exec0, typename...Slots>
auto signal_base<Derived,Func>::connect(Exec0 &&exec, Slots&&...funcs) noexcept -> derived_t&
	requires is_global_slots_v<slot_mode::async,Slots...>
{
	return connect<slot_mode::async>(
		std::forward<Exec0>(exec), std::forward<Slots>(funcs)...
	);
}

template <typename Derived, concepts::std_func_temp Func>
template <typename Obj, concepts::sched Exec0, typename...Slots>
auto signal_base<Derived,Func>::connect(Obj &&observer, Exec0 &&exec, Slots&&...funcs) -> derived_t&
	requires is_obj_slots_v<slot_mode::async,Obj,Slots...>
{
	return connect<slot_mode::async>(std::forward<Obj>(observer),
		std::forward<Exec0>(exec), std::forward<Slots>(funcs)...
	);
}

template <typename Derived, concepts::std_func_temp Func>
template <typename...Slots>
auto signal_base<Derived,Func>::disconnect(Slots&&...funcs) noexcept -> derived_t&
	requires is_global_slots_def_v<Slots...>
{
	auto state = m_impl;
	std::lock_guard lock(state->m_mutex);

	(void) std::initializer_list<int> {(
		state->disconnect(std::forward<Slots>(funcs)),
	0)...};

	state->rebuild_snapshot_locked();
	return static_cast<derived_t&>(*this);
}

template <typename Derived, concepts::std_func_temp Func>
template <typename Obj, typename...Func0>
auto signal_base<Derived,Func>::disconnect(const Obj &observer, Func0&&...funcs) -> derived_t&
	requires is_obj_slots_def_v<Obj,Func0...>
{
	if( not observer )
		invalid_argument::loc_throw("riwo::utils::signal::disconnect: observer is nullptr");

	auto state = m_impl;
	std::lock_guard lock(state->m_mutex);

	(void) std::initializer_list<int> {(
		state->disconnect(observer, std::forward<Func0>(funcs)),
	0)...};

	state->rebuild_snapshot_locked();
	return static_cast<derived_t&>(*this);
}

template <typename Derived, concepts::std_func_temp Func>
auto signal_base<Derived,Func>::disconnect() noexcept -> derived_t&
{
	auto state = m_impl;
	std::lock_guard lock(state->m_mutex);
	state->m_slots.clear();
	state->rebuild_snapshot_locked();
	return static_cast<derived_t&>(*this);
}

template <typename Derived, concepts::std_func_temp Func>
template <typename Obj>
auto signal_base<Derived,Func>::disconnect(const Obj &observer) -> derived_t&
	requires is_observer_v<Obj>
{
	if( not observer )
		invalid_argument::loc_throw("riwo::utils::signal::disconnect: observer is nullptr");

	auto state = m_impl;
	std::lock_guard lock(state->m_mutex);

	std::erase_if(state->m_slots, [&observer](const auto &item) {
		return item.slot->m_obj == observer.get();
	});
	state->rebuild_snapshot_locked();
	return static_cast<derived_t&>(*this);
}

template <typename Derived, concepts::std_func_temp Func>
template <typename...Args>
auto signal_base<Derived,Func>::emit(Args&&...args) const noexcept
	requires is_callable_v<Args...>
{
	if constexpr( is_awaitable_v<typename func_traits_t::return_type> )
	{
		auto state = m_impl;
		return state->emit(state, std::forward<Args>(args)...);
	}
	else
		return m_impl->emit(std::forward<Args>(args)...);
}

template <typename Derived, concepts::std_func_temp Func>
template <typename...Args>
auto signal_base<Derived,Func>::operator()(Args&&...args) const noexcept
	requires is_callable_v<Args...>
{
	return emit(std::forward<Args>(args)...);
}

template <typename Derived, concepts::std_func_temp Func>
void signal_base<Derived,Func>::block(bool block) noexcept
{
	auto state = m_impl;
	state->m_block = block;
}

template <typename Derived, concepts::std_func_temp Func>
bool signal_base<Derived,Func>::is_blocked() const noexcept
{
	auto state = m_impl;
	return state->m_block;
}

} //namespace riwo::utils

// Fuck MicroSoft ...
#endif //RIWO_UTILS_DETAIL_SIGNAL_SLOT_H
