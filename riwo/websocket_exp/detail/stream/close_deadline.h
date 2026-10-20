// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_WEBSOCKET_EXP_DETAIL_STREAM_CLOSE_DEADLINE_H
#define RIWO_WEBSOCKET_EXP_DETAIL_STREAM_CLOSE_DEADLINE_H

#include <riwo/websocket_exp/global.h>

namespace riwo::websocket_exp::detail
{

class RIWO_WEBSOCKET_EXP_API close_deadline final
{
	RIWO_DISABLE_COPY_MOVE(close_deadline)
	struct operation;

public:
	using callback_t = void (*)(void*) noexcept;

	close_deadline() noexcept;
	~close_deadline();

	[[nodiscard]] bool active() const noexcept;

	[[nodiscard]] sys_expected<> start(const asio::any_io_executor& exec,
		std::weak_ptr<void> owner, std::chrono::milliseconds timeout, callback_t callback
	) noexcept;

	void stop() noexcept;

private:
	std::shared_ptr<operation> m_operation {};
};

} //namespace riwo::websocket_exp::detail


#endif //RIWO_WEBSOCKET_EXP_DETAIL_STREAM_CLOSE_DEADLINE_H
