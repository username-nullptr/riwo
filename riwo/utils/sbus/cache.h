// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_UTILS_UTILS_SBUS_CACHE_H
#define RIWO_UTILS_UTILS_SBUS_CACHE_H

#include <riwo/utils/sbus/subscribe.h>
#include <riwo/utils/sbus/publish.h>
#include <riwo/utils/signal_slot.h>

namespace riwo::utils::sbus
{

template <concepts::subscriber Subscriber>
class RIWO_UTILS_TAPI cache
{
	RIWO_DISABLE_COPY_MOVE(cache)

public:
	using subscriber_t = Subscriber;
	using executor_type = subscriber_t::executor_type;
	using executor_t = executor_type;

	using interface_t = subscriber_t::interface_t;
	using payload_t = std::vector<std::byte>;

	template <typename...Args>
	using signal_t = signal<awaitable<void>(Args...)>;

public:
	template <typename Exec0 = io_context_t&>
	explicit cache(Exec0 &&exec = io_context()) requires
		riwo::concepts::match_sched<Exec0,executor_t>;
	~cache();

public:
	cache &set(std::string_view topic, const void *data, size_t size);

	template <riwo::concepts::any_string_p...Args>
	cache &set(std::string_view topic, Args&&...args)
		requires (sizeof...(Args) > 0);

	template <concepts::unregistered_type_p...Args>
	cache &set(std::string_view topic, Args&&...args)
		requires (sizeof...(Args) > 0);

	template <concepts::topic_type...Args>
	cache &set(Args&&...args)
		requires (sizeof...(Args) > 0);

public:
	template <concepts::topic_type T>
	[[nodiscard]] optional<T> get() const;

	template <typename T>
	[[nodiscard]] optional<T> get(std::string_view topic) const;
	[[nodiscard]] payload_t get(std::string_view topic) const;

	[[nodiscard]] std::map<std::string,payload_t> get() const noexcept;

public:
	[[nodiscard]] signal_t<payload_t,payload_t> &changed(std::string_view topic) noexcept;
	[[nodiscard]] signal_t<std::string_view,payload_t,payload_t> &changed() noexcept;

	template <concepts::topic_type T>
	[[nodiscard]] signal_t<payload_t,payload_t> &changed() noexcept;

public:
	template <typename T = payload_t>
	struct changed_result
	{
		using type = T;
		T current {};
		T previous {};
	};

	template <typename Token, typename T = payload_t>
	static constexpr bool is_token_v =
		riwo::concepts::dis_detached_tf_opt_token<
			Token,error_code,changed_result<T>
		>;

	// Edge-triggered: only the next change after initiation is observed. Earlier
	// changes are not replayed; use changed() for a persistent subscription.
	template <concepts::topic_type T, typename Token = use_sync_t>
	auto wait_changed(Token &&token = {})
		requires is_token_v<Token,T>;

	template <typename Token = use_sync_t>
	auto wait_changed(std::string_view topic, Token &&token = {})
		requires is_token_v<Token>;

	template <typename T, typename Token = use_sync_t>
	auto wait_changed(std::string_view topic, Token &&token = {})
		requires is_token_v<Token,T>;

public:
	cache &cancel() noexcept;
	[[nodiscard]] subscriber_t subscriber() noexcept;
	[[nodiscard]] executor_t get_executor() noexcept;

private:
	class impl;
	std::shared_ptr<impl> m_impl {};
};

using local_cache = cache<local_subscriber>;

#if RIWO_UTILS_SBUS_UDP_INTERFACE_SUPPORT
using udp_cache = cache<udp_subscriber>;
#endif //RIWO_UTILS_SBUS_UDP_INTERFACE_SUPPORT

} //namespace riwo::utils::sbus
#include <riwo/utils/sbus/detail/cache.h>


#endif //RIWO_UTILS_UTILS_SBUS_CACHE_H
