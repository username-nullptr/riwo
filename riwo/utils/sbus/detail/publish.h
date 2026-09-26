// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_UTILS_UTILS_SBUS_DETAIL_PUBLISH_H
#define RIWO_UTILS_UTILS_SBUS_DETAIL_PUBLISH_H

namespace riwo::utils::sbus { namespace detail
{

template <concepts::interface Interface, riwo::concepts::any_string_p Str>
void publish(std::string_view topic, Str &&str)
{
	auto view = strtls::to_view(std::forward<Str>(str));
	Interface::publish(topic, view.data(), view.size());
}

template <concepts::interface Interface, concepts::unregistered_type_p T>
void publish(std::string_view topic, const T &value)
{
	using value_t = std::remove_cvref_t<T>;
	if constexpr( riwo::concepts::streamer_type<value_t> )
	{
		auto buffer = streamer<value_t>::encode(value);
		Interface::publish(topic, buffer.data(), buffer.size());
	}
	else if constexpr( std::is_same_v<value_t, const_buffer> or
		std::is_same_v<value_t, asio::const_buffer> )
		Interface::publish(topic, value.data(), value.size());
	else
		Interface::publish(topic, &value, sizeof(value_t));
}

template <concepts::interface Interface>
void publish(concepts::topic_type auto &&value)
{
	using Value = decltype(value);
	using value_t = std::remove_cvref_t<Value>;

	if constexpr( riwo::concepts::streamer_type<value_t> )
	{
		auto buffer = streamer<value_t>::encode(value);
		Interface::publish(value_t::riwo_sbus_topic_v, buffer.data(), buffer.size());
	}
	else
		Interface::publish(value_t::riwo_sbus_topic_v, &value, sizeof(value_t));
}

} //namespace detail

template <concepts::interface Interface>
void publish(std::string_view topic, const void *buffer, size_t size)
{
	Interface::publish(topic, buffer, size);
}

template <concepts::interface Interface, riwo::concepts::any_string_p...Args>
void publish(std::string_view topic, Args&&...args) requires (sizeof...(Args) > 0)
{
	(void) std::initializer_list<int> {
		(detail::publish<Interface>(topic, std::forward<Args>(args)), 0) ...
	};
}

template <concepts::interface Interface, concepts::unregistered_type_p...Args>
void publish(std::string_view topic, Args&&...args) requires (sizeof...(Args) > 0)
{
	(void) std::initializer_list<int> {
		(detail::publish<Interface>(topic, std::forward<Args>(args)), 0) ...
	};
}

template <concepts::interface Interface, concepts::topic_type...Args>
void publish(Args&&...args) requires (sizeof...(Args) > 0)
{
	(void) std::initializer_list<int> {
		(detail::publish<Interface>(std::forward<Args>(args)), 0) ...
	};
}

template <typename T>
void publish(std::string_view topic, T &&value) requires
(not std::is_pointer_v<std::remove_cvref_t<T>> and not concepts::topic_type<T>)
{
	publish<default_interface>(topic, std::forward<T>(value));
}

template <riwo::concepts::any_string_p...Args>
void publish(std::string_view topic, Args&&...args) requires (sizeof...(Args) > 0)
{
	publish<default_interface>(topic, std::forward<Args>(args)...);
}

template <concepts::unregistered_type_p...Args>
void publish(std::string_view topic, Args&&...args) requires (sizeof...(Args) > 0)
{
	publish<default_interface>(topic, std::forward<Args>(args)...);
}

template <concepts::topic_type...Args>
void publish(Args&&...args) requires (sizeof...(Args) > 0)
{
	publish<default_interface>(std::forward<Args>(args)...);
}

} //namespace riwo::utils::sbus


#endif //RIWO_UTILS_UTILS_SBUS_DETAIL_PUBLISH_H
