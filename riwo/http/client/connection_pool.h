// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_CLIENT_CONNECTION_POOL_H
#define RIWO_HTTP_CLIENT_CONNECTION_POOL_H

#include <riwo/http/client/connection_lease.h>
#include <riwo/http/client/connector.h>
#include <riwo/core/execution.h>

namespace riwo::http
{

struct connection_pool_config
{
	size_t max_count = std::numeric_limits<size_t>::max();
	using seconds_t = std::chrono::seconds;
	struct {
		seconds_t idle {60}, health {5};
	} timeout;
};

template <core_concepts::exec Exec = asio::any_io_executor>
class RIWO_HTTP_TAPI basic_connection_pool
{
	RIWO_DISABLE_COPY(basic_connection_pool)

public:
	using executor_type = Exec;
	using executor_t = executor_type;

	using config_t = connection_pool_config;
	using target_t = connect_target;

	using connector_t = basic_connector<executor_t>;
	using connector_ptr = connector_t::ptr_t;

	using connection_t = basic_connection<executor_t>;
	using connection_ptr = connection_t::ptr_t;

	using lease_t = basic_connection_lease<executor_t>;
	using lease_ptr = lease_t::ptr_t;

public:
	explicit basic_connection_pool(const config_t &config = {}) requires
		core_concepts::match_sched<io_executor_t,executor_t>;

	template <typename Exec0>
	explicit basic_connection_pool(Exec0 &&exec, const config_t &config = {}) requires (
		not std::same_as<std::remove_cvref_t<Exec0>,basic_connection_pool> and
		core_concepts::match_sched<Exec0,executor_t>
	);
	// A configured connector is part of this pool's routing identity. Proxy-aware
	// applications inject one here; the default constructors remain direct-only.
	explicit basic_connection_pool (
		connector_ptr connector_instance, const config_t &config = {}
	);
	~basic_connection_pool();

	basic_connection_pool(basic_connection_pool &&other) noexcept;
	basic_connection_pool &operator=(basic_connection_pool &&other) noexcept;

public:
	template <typename Token = use_sync_t>
	[[nodiscard]] auto get(const target_t &key, Token &&token = {}) noexcept
		requires core_concepts::dis_detached_tf_opt_token<Token,error_code,lease_ptr>;

	template <typename Token = use_sync_t>
	[[nodiscard]] auto try_get(const target_t &key, Token &&token = {}) noexcept
		requires core_concepts::dis_detached_tf_opt_token<Token,error_code,lease_ptr>;

public:
	basic_connection_pool &cancel() noexcept;
	[[nodiscard]] executor_t get_executor() noexcept;

	[[nodiscard]] config_t config() const noexcept;
	[[nodiscard]] size_t count() const noexcept;

private:
	class impl;
	std::shared_ptr<impl> m_impl;
};

using connection_pool = basic_connection_pool<>;

} //namespace riwo::http
#include <riwo/http/client/detail/connection_pool.h>


#endif //RIWO_HTTP_CLIENT_CONNECTION_POOL_H
