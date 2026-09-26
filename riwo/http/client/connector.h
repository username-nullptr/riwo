// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_CLIENT_CONNECTOR_H
#define RIWO_HTTP_CLIENT_CONNECTOR_H

#include <riwo/http/utils/connection.h>
#include <riwo/http/cxx/concepts.h>

namespace riwo::http
{

enum class security_mode {
	plain, tls, // ... ...
};

enum class proxy_tunnel_type {
	http_connect, socks5,
};

struct proxy_tunnel
{
	proxy_tunnel_type type = proxy_tunnel_type::http_connect;
	std::string host {};
	uint16_t port = 0;
	security_mode security = security_mode::plain;

	optional<std::string> authorization {};
	optional<std::string> username {};
	optional<std::string> password {};

	friend bool operator== (
		const proxy_tunnel&, const proxy_tunnel&
	) = default;
};

struct connect_target
{
	std::string host {};
	uint16_t port = 0;

	security_mode security {};
	bool no_delay = true;

	optional<proxy_tunnel> tunnel {};

	friend bool operator== (
		const connect_target&, const connect_target&
	) = default;
};

template <core_concepts::exec Exec = asio::any_io_executor>
class RIWO_HTTP_TAPI basic_connector
{
	RIWO_DISABLE_COPY_MOVE(basic_connector)

public:
	using executor_type = Exec;
	using executor_t = executor_type;
	using ptr_t = std::shared_ptr<basic_connector>;

	using connection_t = basic_connection<executor_t>;
	using connection_ptr = connection_t::ptr_t;

public:
	explicit basic_connector (
		core_concepts::match_sched<executor_t> auto &&exec
	);
#if RIWO_OPENSSL_SUPPORT
	basic_connector (
		core_concepts::match_sched<executor_t> auto &&exec,
		asio::ssl::context &tls_context
	);
#endif //RIWO_OPENSSL_SUPPORT
	virtual ~basic_connector();

	template <typename Token = use_sync_t>
	auto connect(const connect_target &target, Token &&token = {}) noexcept
		requires core_concepts::dis_detached_tf_opt_token<Token,error_code,connection_ptr>;

	[[nodiscard]] executor_t get_executor() const noexcept;

protected:
	// The default implementation handles direct connections and the optional
	// HTTP CONNECT/SOCKS5 tunnel carried by connect_target. Applications can
	// still derive a connector for custom routing.
	[[nodiscard]] virtual sys_expected<connection_ptr>
	do_connect(const connect_target &target) noexcept;

	[[nodiscard]] virtual awaitable<sys_expected<connection_ptr>>
	co_do_connect(const connect_target &target) noexcept;

private:
	class impl;
	impl *m_impl;
};

using connector = basic_connector<>;

} //namespace riwo::http
#include <riwo/http/client/detail/connector.h>


#endif //RIWO_HTTP_CLIENT_CONNECTOR_H
