// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_UTILS_CONNECTION_H
#define RIWO_HTTP_UTILS_CONNECTION_H

#include <riwo/http/utils/opt_token.h>
#include <riwo/core/execution.h>
#include <span>

namespace riwo::http
{

struct RIWO_HTTP_API endpoint
{
	asio::ip::address address {};
	uint16_t port = 0;

	bool from_string(std::string_view text);
	[[nodiscard]] std::string to_string() const;
};

struct tcp_socket_options
{
	optional<bool> no_delay {};
	optional<bool> keep_alive {};

	optional<size_t> send_buffer_size {};
	optional<size_t> receive_buffer_size {};

	optional<asio::socket_base::linger> linger {};
};

struct tcp_socket_state
{
	bool no_delay = false;
	bool keep_alive = false;

	size_t send_buffer_size = 0;
	size_t receive_buffer_size = 0;

	asio::socket_base::linger linger {};
};

enum class connection_probe_state {
	no_event, data_pending, peer_closed, indeterminate
};
/**
 * @par Thread Safety
 * @e Distinct @e objects: Safe.@n
 * @e Shared @e objects: Unsafe.
 *
 * The program may keep one read and one write outstanding concurrently, but
 * must not overlap two reads or two writes on the same connection.
 */
template <core_concepts::exec Exec = asio::any_io_executor>
class RIWO_HTTP_TAPI basic_connection
{
	RIWO_DISABLE_COPY_MOVE(basic_connection)

public:
	using executor_type = Exec;
	using executor_t = executor_type;
	using probe_state_t = connection_probe_state;
	using ptr_t = std::shared_ptr<basic_connection>;

	basic_connection() = default;
	virtual ~basic_connection() = 0;

public:
	template <typename Token, typename...Value>
	static constexpr bool task_token_v =
		core_concepts::dis_detached_tf_opt_token<Token,error_code,Value...>;

	template <typename Token = use_sync_t>
	auto read(const mutable_buffer &buf, Token &&token = {})
		noexcept requires task_token_v<Token,size_t>;

	// With an error_code token, the return value is the number of bytes actually
	// transferred even when error is set. Asynchronous completions provide the
	// same guarantee through their size_t argument.
	template <core_concepts::tf_opt_token<error_code,size_t> Token = use_sync_t>
	auto write(const const_buffer &body, Token &&token = {}) noexcept;

	template <core_concepts::tf_opt_token<error_code,size_t> Token = use_sync_t>
	auto write(std::span<const const_buffer> buffers, Token &&token = {}) noexcept;

	virtual sys_expected<> cancel() noexcept = 0;
	// close() is a non-waiting transport close. In particular, TLS
	// implementations must not wait for the peer's close_notify.
	virtual sys_expected<> close() noexcept = 0;

public:
	virtual sys_expected<> set_options(const tcp_socket_options &options) noexcept = 0;
	[[nodiscard]] virtual sys_expected<tcp_socket_state> options() const noexcept = 0;

	[[nodiscard]] virtual bool is_open() const noexcept = 0;
	// probe() must only inspect immediately available transport state; it must
	// not wait for network input. Unknown/error states are not reusable by a pool.
	[[nodiscard]] virtual sys_expected<probe_state_t> probe() noexcept = 0;

	[[nodiscard]] virtual endpoint remote_endpoint() const noexcept = 0;
	[[nodiscard]] virtual endpoint local_endpoint() const noexcept = 0;

	[[nodiscard]] virtual executor_t get_executor() noexcept = 0;

protected:
	// read() deliberately preserves read_some semantics: one successful stream
	// read completes the operation even when the buffer still has free space.
	// The size return is independent of error so a transport can report partial
	// progress instead of losing it when an operation fails.
	[[nodiscard]] virtual size_t read_some(mutable_buffer buffer, error_code &error) noexcept = 0;
	[[nodiscard]] virtual size_t write_all(const const_buffer &buffer, error_code &error) noexcept = 0;
	[[nodiscard]] virtual size_t write_all(std::span<const const_buffer> buffers, error_code &error) noexcept;

protected:
	using io_handler_t = asio::any_completion_handler<void(error_code,size_t)>;
	virtual void co_read_some(mutable_buffer buffer, io_handler_t handler) noexcept = 0;
	virtual void co_write_all(const_buffer buffer, io_handler_t handler) noexcept = 0;
	virtual void co_write_all(std::span<const const_buffer> buffers, io_handler_t handler) noexcept;
};

using connection = basic_connection<>;
using connection_ptr = basic_connection<>::ptr_t;

} //namespace riwo::http
#include <riwo/http/utils/detail/connection.h>


#endif //RIWO_HTTP_UTILS_CONNECTION_H
