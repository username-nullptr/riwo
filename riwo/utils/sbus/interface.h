// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_UTILS_UTILS_SBUS_INTERFACE_H
#define RIWO_UTILS_UTILS_SBUS_INTERFACE_H

#include <riwo/utils/global.h>

namespace riwo::utils::sbus::concepts
{

template <typename Interface>
concept interface = riwo::concepts::constructible<Interface> and requires
	(Interface &interface, std::string_view topic, uint64_t sid, const char *buffer, size_t size)
	{
		Interface::publish(topic, buffer, size);
		sid = interface.subscribe (
			[](std::string_view, const void*, size_t) {}
		);
		interface.cancel_topic(topic);
		interface.cancel_sid(sid);
		interface.cancel();
	};

template <typename T>
concept topic_type = requires(std::string_view topic) {
	std::string_view(std::remove_cvref_t<T>::riwo_sbus_topic_v);
	topic == std::remove_cvref_t<T>::riwo_sbus_topic_v;
};

#define RIWO_UTILS_SBUS_TYPE_IMPL(value) \
	static constexpr std::string_view riwo_sbus_topic_v = value;

#define RIWO_UTILS_SBUS_TOPIC(value) \
	"riwo.utils.sbus.topic." #value

#define RIWO_UTILS_SBUS_TYPE(value) \
	RIWO_UTILS_SBUS_TYPE_IMPL(RIWO_UTILS_SBUS_TOPIC(value))

#define RIWO_UTILS_SBUS_META_TYPE(value, ...) \
	RIWO_UTILS_SBUS_TYPE(value) RIWO_META_FIELDS(__VA_ARGS__)

#define RIWO_UTILS_SBUS_AUTO_TOPIC \
	"riwo.utils.sbus.topic." __FILE__ RIWO_SHARP(:RIWO_AUTO_XX_NAME())

#define RIWO_UTILS_SBUS_AUTO_TYPE \
	RIWO_UTILS_SBUS_TYPE_IMPL(RIWO_UTILS_SBUS_AUTO_TOPIC)

#define RIWO_UTILS_SBUS_AUTO_META_TYPE(...) \
	RIWO_UTILS_SBUS_AUTO_TYPE RIWO_META_FIELDS(__VA_ARGS__)

} //namespace riwo::utils::sbus::concepts

#include <riwo/utils/sbus/detail/local_interface.h>
#include <riwo/utils/sbus/detail/udp_interface.h>

namespace riwo::utils::sbus
{

#if RIWO_UTILS_SBUS_DEFAULT_INTERFACE_UDP
using default_interface = udp_interface;
#else //local
using default_interface = local_interface;
#endif //

} //namespace riwo::utils::sbus


#endif //RIWO_UTILS_UTILS_SBUS_INTERFACE_H
