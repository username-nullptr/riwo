// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_UTILS_UTILS_SBUS_SUBSCRIBE_H
#define RIWO_UTILS_UTILS_SBUS_SUBSCRIBE_H

#include <riwo/utils/sbus/interface.h>
#include <riwo/core/execution.h>

namespace riwo::utils::sbus { namespace concepts
{

template <typename Func, size_t Count>
concept subscribe_func = []() consteval -> bool
{
	using func_t = std::remove_cvref_t<Func>;
	if constexpr( riwo::is_function_v<func_t> )
	{
		using func_tr_t = function_traits<func_t>;
		if constexpr( Count == 1 and func_tr_t::arg_count == Count )
		{
			using arg_t = std::remove_cvref_t<typename func_tr_t::template arg_type_t<0>>;
			return not std::is_pointer_v<arg_t>;
		}
		else if constexpr( Count == 2 and func_tr_t::arg_count == Count )
		{
			using arg0_t = std::remove_cvref_t<typename func_tr_t::template arg_type_t<0>>;
			using arg1_t = std::remove_cvref_t<typename func_tr_t::template arg_type_t<1>>;

			return riwo::concepts::constructible<arg0_t,std::string_view> and
				not topic_type<arg1_t> and not std::is_pointer_v<arg1_t>;
		}
		else
			return false;
	}
	else
		return false;
}();

template <typename Func>
concept subscribe_type_func = []() consteval -> bool
{
	using func_t = std::remove_cvref_t<Func>;
	if constexpr( riwo::is_function_v<func_t> )
	{
		using func_tr_t = function_traits<func_t>;
		if constexpr( func_tr_t::arg_count == 1 )
		{
			using arg_t = std::remove_cvref_t<typename func_tr_t::template arg_type_t<0>>;
			return topic_type<arg_t>;
		}
		else
			return false;
	}
	else
		return false;
}();

} //namespace concepts

template <concepts::interface Interface, riwo::concepts::exec Exec = asio::any_io_executor>
class RIWO_UTILS_TAPI basic_subscriber
{
public:
	using interface_t = Interface;
	using interface_ptr = std::shared_ptr<Interface>;

	using executor_type = Exec;
	using executor_t = executor_type;

public:
	template <riwo::concepts::match_sched<Exec> Exec0 = io_context_t&>
	explicit basic_subscriber(Exec0 &&exec = io_context());
	~basic_subscriber();

public:
	uint64_t subscribe(std::string_view topic, concepts::subscribe_func<1> auto &&callback);
	uint64_t subscribe(concepts::subscribe_func<2> auto &&callback);

	uint64_t subscribe(std::string_view topic, riwo::concepts::callable<const void*,size_t> auto &&callback);
	uint64_t subscribe(riwo::concepts::callable<std::string_view,const void*,size_t> auto &&callback);

	uint64_t subscribe(concepts::subscribe_type_func auto &&callback);

	basic_subscriber &cancel_topic(const std::string_view &topic);
	basic_subscriber &cancel_sid(uint64_t sid);
	basic_subscriber &cancel();

public:
	template <typename...Args>
	basic_subscriber(Args&&...args) requires (
		(not std::is_same_v<std::remove_cvref_t<Args>,basic_subscriber> and ...) and
		requires(basic_subscriber &obj) { obj.subscribe(std::forward<Args>(args)...); }
	){ subscribe(std::forward<Args>(args)...); }

	[[nodiscard]] interface_ptr interface() noexcept;
	[[nodiscard]] executor_t get_executor() noexcept;

private:
	interface_ptr m_interface {};
	executor_t m_exec {};
};

template <concepts::interface Interface>
using subscriber = basic_subscriber<Interface>;

template <riwo::concepts::exec Exec = asio::any_io_executor>
using basic_local_subscriber = basic_subscriber<local_interface,Exec>;

using local_subscriber = basic_local_subscriber<>;

#if RIWO_UTILS_SBUS_UDP_INTERFACE_SUPPORT
template <riwo::concepts::exec Exec = asio::any_io_executor>
using basic_udp_subscriber = basic_subscriber<udp_interface,Exec>;

using udp_subscriber = basic_udp_subscriber<>;
#endif //RIWO_UTILS_SBUS_UDP_INTERFACE_SUPPORT

#if RIWO_UTILS_SBUS_DEFAULT_INTERFACE_UDP
using default_subscriber = udp_subscriber;
#else //local
using default_subscriber = local_subscriber;
#endif //

namespace concepts
{

template <typename>
struct is_subscriber {};

template <interface Interface, riwo::concepts::exec Exec>
struct is_subscriber<basic_subscriber<Interface,Exec>> : std::true_type {};

template <typename T>
constexpr bool is_subscriber_v = is_subscriber<T>::value;

template <typename T>
concept subscriber = is_subscriber_v<T>;

} //namespace concepts

template <concepts::subscriber Subscriber,
	riwo::concepts::match_sched<typename Subscriber::executor_t> Exec0, typename...Args>
RIWO_UTILS_TAPI std::pair<Subscriber,uint64_t> subscribe(Exec0 &&exec, Args&&...args) requires requires {
	Subscriber(std::forward<Exec0>(exec)).subscribe(std::forward<Args>(args)...);
};

template <concepts::subscriber Subscriber, typename...Args>
RIWO_UTILS_TAPI std::pair<Subscriber,uint64_t> subscribe(Args&&...args) requires requires {
	Subscriber().subscribe(std::forward<Args>(args)...);
};

template <riwo::concepts::match_sched<default_subscriber::executor_t> Exec0, typename...Args>
RIWO_UTILS_TAPI std::pair<default_subscriber,uint64_t> subscribe(Exec0 &&exec, Args&&...args) requires requires {
	default_subscriber(std::forward<Exec0>(exec)).subscribe(std::forward<Args>(args)...);
};

template <typename...Args>
RIWO_UTILS_TAPI std::pair<default_subscriber,uint64_t> subscribe(Args&&...args) requires requires {
	default_subscriber().subscribe(std::forward<Args>(args)...);
};

} //namespace riwo::utils::sbus
#include <riwo/utils/sbus/detail/subscribe.h>


#endif //RIWO_UTILS_UTILS_SBUS_SUBSCRIBE_H
