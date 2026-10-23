// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_SERVER_DETAIL_ACCEPTOR_WRAP_H
#define RIWO_HTTP_EXP_SERVER_DETAIL_ACCEPTOR_WRAP_H

namespace riwo::http_exp
{

template <core_concepts::exec Exec>
class basic_acceptor_wrap<Exec>::interface
{
public:
	virtual ~interface() = default;
	[[nodiscard]] virtual executor_t get_executor() noexcept = 0;
	virtual void accept(accept_handler_t) = 0;
	virtual void cancel() noexcept = 0;
	[[nodiscard]] virtual const acceptor_t &acceptor() const noexcept = 0;
	[[nodiscard]] virtual acceptor_t &acceptor() noexcept = 0;
};

template <core_concepts::exec Exec>
template <typename Acceptor>
class basic_acceptor_wrap<Exec>::model final : public interface
{
public:
	explicit model(Acceptor value) : m_acceptor(std::move(value)) {}

	[[nodiscard]] executor_t get_executor() noexcept override {
		return executor_t(m_acceptor.get_executor());
	}
	void accept(accept_handler_t handler) override {
		m_acceptor.accept(std::move(handler));
	}
	void cancel() noexcept override { m_acceptor.cancel(); }
	[[nodiscard]] const acceptor_t &acceptor() const noexcept override {
		return m_acceptor.acceptor();
	}
	[[nodiscard]] acceptor_t &acceptor() noexcept override {
		return m_acceptor.acceptor();
	}

private:
	Acceptor m_acceptor;
};

template <core_concepts::exec Exec>
class basic_acceptor_wrap<Exec>::tcp_acceptor
{
public:
	using executor_type = executor_t;

	explicit tcp_acceptor(acceptor_t value) : m_acceptor(std::move(value)) {}

	[[nodiscard]] executor_type get_executor() noexcept {
		return m_acceptor.get_executor();
	}
	void accept(accept_handler_t handler)
	{
		auto socket = std::make_shared<socket_t>(m_acceptor.get_executor());
		m_acceptor.async_accept(*socket,
		[socket, handler = std::move(handler)](error_code error) mutable
		{
			if( error )
				return std::move(handler)(error, accepted_t{});
			std::move(handler)(error_code{}, accepted_t(
				std::in_place_type<socket_t>, std::move(*socket)));
		});
	}
	void cancel() noexcept
	{
		error_code ignored;
		m_acceptor.cancel(ignored);
		m_acceptor.close(ignored);
	}
	[[nodiscard]] const acceptor_t &acceptor() const noexcept {
		return m_acceptor;
	}
	[[nodiscard]] acceptor_t &acceptor() noexcept { return m_acceptor; }

private:
	acceptor_t m_acceptor;
};

template <core_concepts::exec Exec>
basic_acceptor_wrap<Exec>::basic_acceptor_wrap()
	requires core_concepts::match_sched<io_executor_t,executor_t> :
	basic_acceptor_wrap(acceptor_t(riwo::get_executor()))
{
}

template <core_concepts::exec Exec>
basic_acceptor_wrap<Exec>::basic_acceptor_wrap(acceptor_t &&acceptor) :
	m_impl(std::make_unique<model<tcp_acceptor>>(
		tcp_acceptor(std::move(acceptor))))
{
}

template <core_concepts::exec Exec>
template <typename Acceptor>
basic_acceptor_wrap<Exec>::basic_acceptor_wrap(Acceptor acceptor)
	requires (
		not std::same_as<std::remove_cvref_t<Acceptor>,basic_acceptor_wrap> and
		requires(Acceptor &value, const Acceptor &const_value,
			accept_handler_t handler) {
			typename Acceptor::executor_type;
			{ value.get_executor() } -> std::convertible_to<executor_t>;
			{ value.acceptor() } -> std::same_as<acceptor_t&>;
			{ const_value.acceptor() } -> std::same_as<const acceptor_t&>;
			value.accept(std::move(handler));
			{ value.cancel() } noexcept;
		}) :
	m_impl(std::make_unique<model<Acceptor>>(std::move(acceptor)))
{
}

template <core_concepts::exec Exec>
basic_acceptor_wrap<Exec>::basic_acceptor_wrap(
	basic_acceptor_wrap &&other) noexcept : m_impl(std::move(other.m_impl))
{
	other.m_impl = std::make_unique<model<tcp_acceptor>>(
		tcp_acceptor(acceptor_t(m_impl->get_executor())));
}

template <core_concepts::exec Exec>
basic_acceptor_wrap<Exec> &basic_acceptor_wrap<Exec>::operator=(
	basic_acceptor_wrap &&other) noexcept
{
	if( this == &other )
		return *this;
	auto replacement = std::make_unique<model<tcp_acceptor>>(
		tcp_acceptor(acceptor_t(other.m_impl->get_executor())));
	m_impl = std::move(other.m_impl);
	other.m_impl = std::move(replacement);
	return *this;
}

template <core_concepts::exec Exec>
basic_acceptor_wrap<Exec>::~basic_acceptor_wrap() = default;

template <core_concepts::exec Exec>
auto basic_acceptor_wrap<Exec>::get_executor() noexcept -> executor_t {
	return m_impl->get_executor();
}

template <core_concepts::exec Exec>
void basic_acceptor_wrap<Exec>::accept(accept_handler_t handler) {
	m_impl->accept(std::move(handler));
}

template <core_concepts::exec Exec>
void basic_acceptor_wrap<Exec>::cancel() noexcept {
	m_impl->cancel();
}

template <core_concepts::exec Exec>
auto basic_acceptor_wrap<Exec>::acceptor() const noexcept -> const acceptor_t& {
	return m_impl->acceptor();
}

template <core_concepts::exec Exec>
auto basic_acceptor_wrap<Exec>::acceptor() noexcept -> acceptor_t& {
	return m_impl->acceptor();
}

} //namespace riwo::http_exp

#endif //RIWO_HTTP_EXP_SERVER_DETAIL_ACCEPTOR_WRAP_H
