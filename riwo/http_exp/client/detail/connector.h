// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_CLIENT_DETAIL_CONNECTOR_H
#define RIWO_HTTP_EXP_CLIENT_DETAIL_CONNECTOR_H

#include <riwo/http_exp/client/config.h>
#include <riwo/http_exp/protocol/request_head.h>
#include <riwo/http_exp/transport/connection.h>

namespace riwo::http_exp::detail
{

using endpoint = asio::ip::tcp::endpoint;

struct origin
{
	std::string scheme {};
	std::string host {};
	uint16_t port = 0;

	friend bool operator==(const origin &, const origin &) = default;
};

struct tls_identity
{
	bool enabled = false;
	bool verify_peer = true;
	std::string certificate_authority_file {};

	friend bool operator==(const tls_identity &, const tls_identity &) = default;
};

struct proxy_identity
{
	optional<std::string> endpoint {};

	friend bool operator==(
		const proxy_identity &, const proxy_identity &) = default;
};

struct protocol_policy_identity
{
	std::vector<protocol> preference {};
	protocol_requirement requirement = protocol_requirement::automatic;

	friend bool operator==(const protocol_policy_identity &,
		const protocol_policy_identity &) = default;
};

// The key identifies one reusable Session, not one application request. The
// local bind endpoint is present even though phase C has no public setter yet;
// later bind support must not silently change connection identity.
struct session_key
{
	std::string scheme {};
	std::string host {};
	uint16_t port = 0;
	proxy_identity proxy {};
	tls_identity tls {};
	protocol_policy_identity protocols {};
	optional<endpoint> local_bind {};

	[[nodiscard]] origin target_origin() const noexcept {
		return {scheme, host, port};
	}

	friend bool operator==(const session_key &, const session_key &) = default;
};

struct session_key_hash
{
	[[nodiscard]] size_t operator()(const session_key &value) const noexcept;
};

[[nodiscard]] RIWO_HTTP_EXP_API session_key make_session_key(
	const request_head &head, const client_config &config);

struct negotiated_transport
{
	std::shared_ptr<::riwo::http_exp::connection> stream {};
	protocol selected_protocol = protocol::v1_1;
	endpoint remote {};
	endpoint local {};
	optional<std::string> alpn {};
};

class RIWO_HTTP_EXP_API connector
{
	RIWO_DISABLE_COPY_MOVE(connector)

public:
	using executor_type = asio::any_io_executor;
	using connect_handler = asio::any_completion_handler<void(
		error_code, negotiated_transport)>;

	explicit connector(executor_type executor);
	virtual ~connector();

	[[nodiscard]] virtual executor_type get_executor() const noexcept;
	[[nodiscard]] virtual sys_expected<negotiated_transport> connect(
		const session_key &key, const client_config &config) noexcept = 0;
	virtual void connect_async(const session_key &key,
		const client_config &config, connect_handler handler);

protected:
	executor_type m_executor;
};

using connector_ptr = std::shared_ptr<connector>;

[[nodiscard]] RIWO_HTTP_EXP_API connector_ptr make_default_connector(
	asio::any_io_executor executor);

[[nodiscard]] RIWO_HTTP_EXP_API sys_expected<protocol> negotiate_protocol(
	const protocol_policy &policy, const session_key &key,
	bool tls_available = RIWO_OPENSSL_SUPPORT != 0);

} //namespace riwo::http_exp::detail

#endif //RIWO_HTTP_EXP_CLIENT_DETAIL_CONNECTOR_H
