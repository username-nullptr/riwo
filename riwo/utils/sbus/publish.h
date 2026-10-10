// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_UTILS_UTILS_SBUS_PUBLISH_H
#define RIWO_UTILS_UTILS_SBUS_PUBLISH_H

#include <riwo/utils/sbus/interface.h>

namespace riwo::utils::sbus { namespace concepts
{

template <typename T>
concept unregistered_type =
	not std::is_pointer_v<std::remove_cvref_t<T>> and
	not riwo::concepts::any_string<T> and
	not topic_type<T>;

template <typename T>
concept unregistered_type_p = unregistered_type<std::remove_cvref_t<T>>;

} //namespace concepts

template <concepts::interface Interface>
RIWO_UTILS_TAPI void publish (
	std::string_view topic, const void *buffer, size_t size
);

template <concepts::interface Interface, riwo::concepts::any_string_p...Args>
RIWO_UTILS_TAPI void publish(std::string_view topic, Args&&...args)
	requires (sizeof...(Args) > 0);

template <concepts::interface Interface, concepts::unregistered_type_p...Args>
RIWO_UTILS_TAPI void publish(std::string_view topic, Args&&...args)
	requires (sizeof...(Args) > 0);

template <concepts::interface Interface, concepts::topic_type...Args>
RIWO_UTILS_TAPI void publish(Args&&...args)
	requires (sizeof...(Args) > 0);

RIWO_UTILS_API void publish (
	std::string_view topic, const void *buffer, size_t size
);

template <riwo::concepts::any_string_p...Args>
RIWO_UTILS_TAPI void publish(std::string_view topic, Args&&...args)
	requires (sizeof...(Args) > 0);

template <concepts::unregistered_type_p...Args>
RIWO_UTILS_TAPI void publish(std::string_view topic, Args&&...args)
	requires (sizeof...(Args) > 0);

template <concepts::topic_type...Args>
RIWO_UTILS_TAPI void publish(Args&&...args)
	requires (sizeof...(Args) > 0);

} //namespace riwo::utils::sbus
#include <riwo/utils/sbus/detail/publish.h>


#endif //RIWO_UTILS_UTILS_SBUS_PUBLISH_H
