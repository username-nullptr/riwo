// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_CLIENT_DETAIL_SESSION_POOL_H
#define RIWO_HTTP_EXP_CLIENT_DETAIL_SESSION_POOL_H

#include <riwo/http_exp/client/detail/factory.h>

namespace riwo::http_exp::detail
{

// A capacity pool: callers receive a stream, never a transport lease. HTTP/1
// Sessions expose capacity one while idle and zero while their single stream
// is active; HTTP/2 Sessions will expose their negotiated concurrent capacity.
class RIWO_HTTP_EXP_API session_pool :
	public std::enable_shared_from_this<session_pool>
{
	RIWO_DISABLE_COPY_MOVE(session_pool)

public:
	using executor_type = asio::any_io_executor;
	using open_handler = asio::any_completion_handler<void(
		error_code, std::shared_ptr<stream>)>;

	session_pool(executor_type executor, client_config config,
		connector_ptr connector = {},
		session_factory_ptr factory = {});
	~session_pool();

	[[nodiscard]] executor_type get_executor() const noexcept;
	[[nodiscard]] const client_config &config() const noexcept;

	[[nodiscard]] std::shared_ptr<stream> open_sync(const session_key &key,
		request_head head, body_spec spec, error_code &error) noexcept;
	[[nodiscard]] std::shared_ptr<stream> open_sync(const origin &target,
		request_head head, body_spec spec, error_code &error) noexcept;

	void open(const session_key &key, request_head head, body_spec spec,
		open_handler handler);
	void open(const origin &target, request_head head, body_spec spec,
		open_handler handler);

	void cancel() noexcept;
	void shutdown() noexcept;

	[[nodiscard]] size_t session_count() const noexcept;
	[[nodiscard]] size_t idle_session_count() const noexcept;
	[[nodiscard]] size_t waiter_count() const noexcept;
	[[nodiscard]] size_t count() const noexcept {
		return session_count();
	}

private:
	class impl;
	std::shared_ptr<impl> m_impl;
};

using session_pool_ptr = std::shared_ptr<session_pool>;

} //namespace riwo::http_exp::detail

#endif //RIWO_HTTP_EXP_CLIENT_DETAIL_SESSION_POOL_H
