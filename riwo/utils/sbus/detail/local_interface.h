// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_UTILS_UTILS_SBUS_DETAIL_LOCAL_INTERFACE_H
#define RIWO_UTILS_UTILS_SBUS_DETAIL_LOCAL_INTERFACE_H

#include <riwo/utils/global.h>

namespace riwo::utils::sbus
{

class RIWO_UTILS_API local_interface final :
	public std::enable_shared_from_this<local_interface>
{
	RIWO_DISABLE_COPY_MOVE(local_interface)

public:
	local_interface();
	~local_interface();

	static void publish(std::string_view topic, const void *buffer, size_t size);

	uint64_t subscribe(std::string_view topic, std::function<void(const void*, size_t)> func);
	uint64_t subscribe(std::function<void(std::string_view topic, const void*, size_t)> func);

	void cancel_topic(std::string_view topic);
	void cancel_sid(uint64_t sid);
	void cancel();

private:
	class impl;
	std::unique_ptr<impl> m_impl {};
};

} //namespace riwo::utils::sbus


#endif //RIWO_UTILS_UTILS_SBUS_DETAIL_LOCAL_INTERFACE_H
