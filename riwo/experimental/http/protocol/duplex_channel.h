// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_EXP_HTTP_PROTOCOL_DUPLEX_CHANNEL_H
#define RIWO_EXP_HTTP_PROTOCOL_DUPLEX_CHANNEL_H

#include <riwo/experimental/http/error.h>

namespace riwo::experimental::http { namespace detail {
class duplex_state;
} //namespace detail

class duplex_channel_io;
using duplex_channel_io_ptr = std::shared_ptr<duplex_channel_io>;
/**
 * A protocol-independent byte stream detached from one HTTP exchange.
 *
 * For HTTP/1 this owns the upgraded connection.  For HTTP/2 it owns only the
 * Extended CONNECT stream; other streams on the Session remain usable.
 */
template <core_concepts::exec Exec = asio::any_io_executor>
class RIWO_EXP_HTTP_TAPI basic_duplex_channel
{
	RIWO_DISABLE_COPY(basic_duplex_channel)

public:
	using executor_type = Exec;
	using executor_t = executor_type;

	using endpoint_t = asio::ip::tcp::endpoint;
	using duplex_state_ptr = std::shared_ptr<detail::duplex_state>;

public:
	basic_duplex_channel()
		requires core_concepts::match_sched<io_executor_t,executor_t>;

	template <typename Exec0>
	explicit basic_duplex_channel(Exec0 &&executor)
		requires core_concepts::match_sched<Exec0,executor_t>;

	explicit basic_duplex_channel(duplex_state_ptr state)
		requires core_concepts::match_sched<io_executor_t,executor_t>;

	template <typename Exec0>
	basic_duplex_channel(Exec0 &&executor, duplex_state_ptr state)
		requires core_concepts::match_sched<Exec0,executor_t>;

	basic_duplex_channel(basic_duplex_channel &&other) noexcept;
	basic_duplex_channel &operator=(basic_duplex_channel &&other) noexcept;

	~basic_duplex_channel();

public:
	template <core_concepts::dis_detached_tf_opt_token<error_code,size_t> Token = use_sync_t>
	[[nodiscard]] auto read_some(mutable_buffer buffer, Token &&token = {});

	template <core_concepts::tf_opt_token<error_code,size_t> Token = use_sync_t>
	auto write(const const_buffer &buffer, Token &&token = {});

	template <core_concepts::tf_opt_token<error_code,size_t> Token = use_sync_t>
	auto write(std::span<const const_buffer> buffers, Token &&token = {});

	template <core_concepts::tf_opt_token<error_code> Token = use_sync_t>
	auto close(Token &&token = {});

	void cancel() noexcept;

public:
	[[nodiscard]] executor_t get_executor() const noexcept;
	[[nodiscard]] protocol selected_protocol() const noexcept;
	[[nodiscard]] bool is_open() const noexcept;

	[[nodiscard]] endpoint_t remote_endpoint(error_code &error) const noexcept;
	[[nodiscard]] endpoint_t local_endpoint(error_code &error) const noexcept;

	[[nodiscard]] duplex_channel_io_ptr next_layer() noexcept;

private:
	friend class io_access;
	duplex_state_ptr m_state {};
	executor_t m_exec {};
};

using duplex_channel = basic_duplex_channel<>;

class RIWO_EXP_HTTP_API duplex_channel_io
{
	RIWO_DISABLE_COPY_MOVE(duplex_channel_io)

public:
	using io_handler_t =
		asio::any_completion_handler<void(error_code, size_t)>;

	duplex_channel_io() = default;
	virtual ~duplex_channel_io() = default;

	virtual void read_some(mutable_buffer buffer, io_handler_t handler) = 0;
	virtual void write(std::span<const const_buffer> buffers, io_handler_t handler) = 0;
};

} //namespace riwo::experimental::http
#include <riwo/experimental/http/protocol/detail/duplex_channel.h>


#endif //RIWO_EXP_HTTP_PROTOCOL_DUPLEX_CHANNEL_H
