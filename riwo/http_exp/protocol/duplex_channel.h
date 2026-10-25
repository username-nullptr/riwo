// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_PROTOCOL_DUPLEX_CHANNEL_H
#define RIWO_HTTP_EXP_PROTOCOL_DUPLEX_CHANNEL_H

#include <riwo/http_exp/error.h>

namespace riwo::http_exp { namespace detail {
class duplex_state;
} //namespace detail
/**
 * A protocol-independent byte stream detached from one HTTP exchange.
 *
 * For HTTP/1 this owns the upgraded connection.  For HTTP/2 it owns only the
 * Extended CONNECT stream; other streams on the Session remain usable.
 */
template <core_concepts::exec Exec = asio::any_io_executor>
class RIWO_HTTP_EXP_TAPI basic_duplex_channel
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

	explicit basic_duplex_channel(std::shared_ptr<detail::duplex_state> state)
		requires core_concepts::match_sched<io_executor_t,executor_t>;

	template <typename Exec0>
	basic_duplex_channel(Exec0 &&executor,
		std::shared_ptr<detail::duplex_state> state)
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

private:
	duplex_state_ptr m_state {};
	executor_t m_exec {};
};

using duplex_channel = basic_duplex_channel<>;

} //namespace riwo::http_exp
#include <riwo/http_exp/protocol/detail/duplex_channel.h>


#endif //RIWO_HTTP_EXP_PROTOCOL_DUPLEX_CHANNEL_H
