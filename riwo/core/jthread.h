// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_JTHREAD_H
#define RIWO_CORE_JTHREAD_H

#include <riwo/core/global.h>

#ifndef RIWO_HAS_STD_JTHREAD
# if defined(__cpp_lib_jthread) && __cpp_lib_jthread >= 201911L
#  define RIWO_HAS_STD_JTHREAD  1
# else //jthread
#  define RIWO_HAS_STD_JTHREAD  0
# endif //jthread
#endif //RIWO_HAS_STD_JTHREAD

#if RIWO_HAS_STD_JTHREAD
# include <stop_token>
#endif //RIWO_HAS_STD_JTHREAD

namespace riwo
{

#if RIWO_HAS_STD_JTHREAD

using std::jthread;
using std::nostopstate;
using std::nostopstate_t;
using std::stop_callback;
using std::stop_source;
using std::stop_token;

#else //RIWO_HAS_STD_JTHREAD

struct nostopstate_t {
	explicit nostopstate_t() = default;
};
constexpr nostopstate_t nostopstate {};

namespace detail
{

class stop_state;
struct stop_callback_base;

template <typename Callback>
struct stop_callback_node;

} //namespace detail

class RIWO_CORE_API stop_token
{
public:
	stop_token() noexcept = default;
	~stop_token() = default;

	stop_token(const stop_token&) noexcept = default;
	stop_token(stop_token&&) noexcept = default;

	stop_token &operator=(const stop_token&) noexcept = default;
	stop_token &operator=(stop_token&&) noexcept = default;

public:
	[[nodiscard]] bool stop_requested() const noexcept;
	[[nodiscard]] bool stop_possible() const noexcept;

	void swap(stop_token &other) noexcept;
	friend bool operator==(const stop_token&, const stop_token&) noexcept = default;

private:
	template <typename> friend class stop_callback;
	friend class stop_source;

	explicit stop_token(std::shared_ptr<detail::stop_state> state) noexcept;
	std::shared_ptr<detail::stop_state> m_state {};
};

class RIWO_CORE_API stop_source
{
public:
	stop_source();
	explicit stop_source(nostopstate_t) noexcept;

	stop_source(const stop_source &other) noexcept;
	stop_source(stop_source &&other) noexcept = default;
	~stop_source();

	stop_source &operator=(const stop_source &other) noexcept;
	stop_source &operator=(stop_source &&other) noexcept;

public:
	[[nodiscard]] stop_token get_token() const noexcept;
	[[nodiscard]] bool stop_possible() const noexcept;
	[[nodiscard]] bool stop_requested() const noexcept;

	bool request_stop() noexcept;
	void swap(stop_source &other) noexcept;

	friend bool operator==(const stop_source&, const stop_source&) noexcept = default;

private:
	void add_source() noexcept;
	void release_source() noexcept;
	std::shared_ptr<detail::stop_state> m_state {};
};

template <typename Callback>
class RIWO_CORE_TAPI stop_callback
{
	static_assert(std::invocable<Callback>);
	static_assert(std::destructible<Callback>);
	RIWO_DISABLE_COPY_MOVE(stop_callback)

public:
	using callback_type = Callback;

	template <typename C>
	explicit stop_callback(const stop_token &token, C &&callback)
		noexcept(std::is_nothrow_constructible_v<Callback,C>)
		requires std::constructible_from<Callback,C>;

	template <typename C>
	explicit stop_callback(stop_token &&token, C &&callback)
		noexcept(std::is_nothrow_constructible_v<Callback,C>)
		requires std::constructible_from<Callback,C>;

	~stop_callback();

private:
	void register_callback() noexcept;
	std::shared_ptr<detail::stop_state> m_state {};
	detail::stop_callback_node<Callback> m_node;
};

template <typename Callback>
stop_callback(stop_token, Callback) -> stop_callback<std::decay_t<Callback>>;

class RIWO_CORE_API jthread
{
	RIWO_DISABLE_COPY(jthread)

public:
	using id = std::thread::id;
	using native_handle_type = std::thread::native_handle_type;

	template <typename Function, typename...Args>
	explicit jthread(Function &&function, Args&&...args)
		requires (not std::same_as<std::remove_cvref_t<Function>,jthread>);

	jthread() noexcept = default;
	~jthread();

	jthread(jthread&&) noexcept = default;
	jthread &operator=(jthread&& other) noexcept;

public:
	void swap(jthread &other) noexcept;
	[[nodiscard]] bool joinable() const noexcept;

	void join();
	void detach();

	[[nodiscard]] id get_id() const noexcept;
	[[nodiscard]] native_handle_type native_handle();

	[[nodiscard]] stop_source get_stop_source() noexcept;
	[[nodiscard]] stop_token get_stop_token() const noexcept;

	bool request_stop() noexcept;
	[[nodiscard]] static unsigned int hardware_concurrency() noexcept;

private:
	template <typename Function, typename...Args>
	static std::thread create_thread (
		stop_source &source, Function &&function, Args&&...args
	);
	stop_source m_stop_source {nostopstate};
	std::thread m_thread {};
};

RIWO_CORE_API void swap(stop_token &left, stop_token &right) noexcept;
RIWO_CORE_API void swap(stop_source &left, stop_source &right) noexcept;
RIWO_CORE_API void swap(jthread &left, jthread &right) noexcept;

#endif //RIWO_HAS_STD_JTHREAD

} //namespace riwo
#include <riwo/core/detail/jthread.h>


#endif //RIWO_CORE_JTHREAD_H
