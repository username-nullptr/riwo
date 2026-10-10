// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_UTILS_UTILS_SBUS_INTERFACE_H
#define RIWO_UTILS_UTILS_SBUS_INTERFACE_H

#include <riwo/utils/global.h>

namespace riwo::utils::sbus { namespace concepts
{

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

} //namespace concepts

struct msg_path
{
	std::string domain {};
	std::string topic {};
};

class RIWO_UTILS_API interface
{
	RIWO_DISABLE_COPY_MOVE(interface)

public:
	using sid_t = uint64_t;

	interface() = default;
	virtual ~interface() = 0;

	virtual void publish(const msg_path &path, const void *buffer, size_t size) = 0;
	virtual sid_t subscribe(const msg_path &path, std::function<void(const void*, size_t)> func) = 0;
	virtual sid_t subscribe(std::function<void(msg_path path, const void*, size_t)> func) = 0;

	virtual void cancel(const msg_path &path) = 0;
	virtual void cancel_sid(sid_t sid) = 0;
	virtual void cancel() = 0;

public:
	static void set_default_domain();
};

} //namespace riwo::utils::sbus


#endif //RIWO_UTILS_UTILS_SBUS_INTERFACE_H
