// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_DETAIL_JTHREAD_H
#define RIWO_CORE_DETAIL_JTHREAD_H

#if !RIWO_HAS_STD_JTHREAD

namespace riwo { namespace detail
{

struct RIWO_CORE_API stop_callback_base
{
	using execute_t = void(*)(stop_callback_base*) noexcept;
	explicit stop_callback_base(execute_t execute_callback) noexcept;

	stop_callback_base *previous = nullptr;
	stop_callback_base *next = nullptr;

	bool registered = false;
	execute_t execute = nullptr;
};

template <typename Callback>
struct RIWO_CORE_TAPI stop_callback_node final : stop_callback_base
{
	RIWO_DISABLE_COPY_MOVE(stop_callback_node)

	template <typename C>
	explicit stop_callback_node(C &&completion_callback)
		noexcept(std::is_nothrow_constructible_v<Callback,C>) :
		stop_callback_base(&invoke),
		callback(std::forward<C>(completion_callback)) {}

	static void invoke(stop_callback_base *base) noexcept
	{
		auto &stored_callback = static_cast<stop_callback_node*>(base)->callback;
		std::invoke(std::forward<Callback>(stored_callback));
	}
	Callback callback;
};

class RIWO_CORE_API stop_state
{
	RIWO_DISABLE_COPY_MOVE(stop_state)

public:
	stop_state();
	~stop_state();

	[[nodiscard]] bool stop_requested() const noexcept;
	[[nodiscard]] bool stop_possible() const noexcept;

	void add_source() noexcept;
	void release_source() noexcept;
	bool request_stop() noexcept;

	void register_callback(stop_callback_base &callback) noexcept;
	void unregister_callback(stop_callback_base &callback) noexcept;

private:
	class impl;
	impl *m_impl;
};

} //namespace detail

template <typename Callback>
template <typename C>
stop_callback<Callback>::stop_callback(const stop_token &token, C &&callback)
	noexcept(std::is_nothrow_constructible_v<Callback,C>)
	requires std::constructible_from<Callback,C> :
	m_state(token.m_state),
	m_node(std::forward<C>(callback))
{
	register_callback();
}

template <typename Callback>
template <typename C>
stop_callback<Callback>::stop_callback(stop_token &&token, C &&callback)
	noexcept(std::is_nothrow_constructible_v<Callback,C>)
	requires std::constructible_from<Callback,C> :
	m_state(std::move(token.m_state)),
	m_node(std::forward<C>(callback))
{
	register_callback();
}

template <typename Callback>
stop_callback<Callback>::~stop_callback()
{
	if( m_state )
		m_state->unregister_callback(m_node);
}

template <typename Callback>
void stop_callback<Callback>::register_callback() noexcept
{
	if( m_state )
		m_state->register_callback(m_node);
}

template <typename Function, typename...Args>
jthread::jthread(Function &&function, Args&&...args)
	requires (not std::same_as<std::remove_cvref_t<Function>,jthread>) :
	m_stop_source(),
	m_thread(create_thread (
		m_stop_source, std::forward<Function>(function),
		std::forward<Args>(args)...
	))
{

}

template <typename Function, typename...Args>
std::thread jthread::create_thread(stop_source &source, Function &&function, Args&&...args)
{
	using function_t = std::decay_t<Function>;
	if constexpr( std::is_invocable_v<function_t, stop_token, std::decay_t<Args>...> )
	{
		return std::thread (
			std::forward<Function>(function), source.get_token(),
			std::forward<Args>(args)...
		);
	}
	else
	{
		static_assert(std::is_invocable_v<function_t,std::decay_t<Args>...>);
		return std::thread (
			std::forward<Function>(function), std::forward<Args>(args)...
		);
	}
}

} //namespace riwo

#endif //RIWO_HAS_STD_JTHREAD
#endif //RIWO_CORE_DETAIL_JTHREAD_H
