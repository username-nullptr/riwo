// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_EXP_HTTP_SERVER_TLS_H
#define RIWO_EXP_HTTP_SERVER_TLS_H

#include <riwo/experimental/http/server/acceptor_wrap.h>
#include <riwo/experimental/http/protocol/policy.h>

#if RIWO_OPENSSL_SUPPORT

namespace riwo::experimental::http
{

struct server_tls_config
{
	std::filesystem::path certificate_chain_file {};
	std::filesystem::path private_key_file {};

	optional<std::filesystem::path> certificate_authority_file {};
	protocol_policy protocols {};

	milliseconds handshake_timeout {30000};
	bool verify_peer = false;
};

class RIWO_EXP_HTTP_API server_tls_context
{
	RIWO_DISABLE_COPY_MOVE(server_tls_context)

public:
	explicit server_tls_context(server_tls_config config);
	~server_tls_context();

	[[nodiscard]] asio::ssl::context &native_context() noexcept;
	[[nodiscard]] const server_tls_config &config() const noexcept;

private:
	static int select_alpn(void *ssl,
		const unsigned char **out, unsigned char *out_length,
		const unsigned char *input, unsigned int input_length,
		void *argument
	) noexcept;

	server_tls_config m_config {};
	asio::ssl::context m_context;
};

using server_tls_context_ptr = std::shared_ptr<server_tls_context>;

[[nodiscard]] RIWO_EXP_HTTP_API acceptor_wrap
make_tls_acceptor(acceptor_wrap acceptor, server_tls_context_ptr context);

} //namespace riwo::experimental::http


#endif //RIWO_OPENSSL_SUPPORT
#endif //RIWO_EXP_HTTP_SERVER_TLS_H
