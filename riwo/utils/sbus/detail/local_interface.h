// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_UTILS_UTILS_SBUS_DETAIL_LOCAL_INTERFACE_H
#define RIWO_UTILS_UTILS_SBUS_DETAIL_LOCAL_INTERFACE_H

#include <riwo/utils/sbus/interface.h>

namespace riwo::utils::sbus
{

class RIWO_UTILS_API local_interface : public interface
{
	RIWO_DISABLE_COPY_MOVE(local_interface)

public:
	local_interface();
	~local_interface() override;

	void publish(const msg_path &path, const void *buffer, size_t size) override;
	uint64_t subscribe(const msg_path &path, std::function<void(const void*, size_t)> func) override;
	uint64_t subscribe(std::function<void(msg_path path, const void*, size_t)> func) override;

	void cancel(const msg_path &path) override;
	void cancel_sid(uint64_t sid) override;
	void cancel() override;

private:
	class impl;
	std::unique_ptr<impl> m_impl {};
};

} //namespace riwo::utils::sbus


#endif //RIWO_UTILS_UTILS_SBUS_DETAIL_LOCAL_INTERFACE_H
