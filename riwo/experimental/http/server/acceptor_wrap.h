// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_EXP_HTTP_SERVER_ACCEPTOR_WRAP_H
#define RIWO_EXP_HTTP_SERVER_ACCEPTOR_WRAP_H

#include <riwo/experimental/http/transport/negotiated_transport.h>
#include <variant>

namespace riwo::experimental::http
{

template <core_concepts::exec Exec = asio::any_io_executor>
class RIWO_EXP_HTTP_TAPI basic_acceptor_wrap
{
	RIWO_DISABLE_COPY(basic_acceptor_wrap)

public:
	using executor_type = Exec;
	using executor_t = executor_type;

	using protocol_t = asio::ip::tcp;
	using socket_t = asio::basic_stream_socket<protocol_t,executor_t>;
	using transport_t = basic_negotiated_transport<executor_t>;

	using acceptor_t = asio::basic_socket_acceptor<protocol_t,executor_t>;
	using accepted_t = std::variant<std::monostate,socket_t,transport_t>;

	using accept_handler_t = asio::any_completion_handler <
		void(error_code,accepted_t)
	>;

public:
	basic_acceptor_wrap() requires
		core_concepts::match_sched<io_executor_t,executor_t>;

	// Intentionally implicit: `server(std::move(tcp_acceptor))` remains valid.
	basic_acceptor_wrap(acceptor_t &&acceptor);

	template <typename Acceptor>
	explicit basic_acceptor_wrap(Acceptor acceptor) requires
	(
		not std::same_as<std::remove_cvref_t<Acceptor>,basic_acceptor_wrap> and
		requires(Acceptor &value, const Acceptor &const_value, accept_handler_t handler)
		{
			typename Acceptor::executor_type;
			{ value.get_executor() } -> std::convertible_to<executor_t>;
			{ value.acceptor() } -> std::same_as<acceptor_t&>;
			{ const_value.acceptor() } -> std::same_as<const acceptor_t&>;
			value.accept(std::move(handler));
			{ value.cancel() } noexcept;
		}
	);
	~basic_acceptor_wrap();

	basic_acceptor_wrap(basic_acceptor_wrap &&other) noexcept;
	basic_acceptor_wrap &operator=(basic_acceptor_wrap &&other) noexcept;

public:
	void accept(accept_handler_t handler);
	void cancel() noexcept;

	[[nodiscard]] const acceptor_t &acceptor() const noexcept;
	[[nodiscard]] acceptor_t &acceptor() noexcept;

	[[nodiscard]] executor_t get_executor() noexcept;

private:
	template <typename Acceptor>
	class model;
	class tcp_acceptor;

	class interface;
	std::unique_ptr<interface> m_interface {};
};

using acceptor_wrap = basic_acceptor_wrap<>;

} //namespace riwo::experimental::http
#include <riwo/experimental/http/server/detail/acceptor_wrap.h>


#endif //RIWO_EXP_HTTP_SERVER_ACCEPTOR_WRAP_H
