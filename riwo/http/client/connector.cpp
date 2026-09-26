// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "connector.h"

namespace riwo::http::detail
{

#if RIWO_OPENSSL_SUPPORT

asio::ssl::context &default_ssl_context() noexcept
{
	static asio::ssl::context obj (
		asio::ssl::context::tls_client
	);
	return obj;
}
RIWO_REGISTRATION
{
	auto &ctx = default_ssl_context();
	error_code error {};
	ctx.set_verify_mode(asio::ssl::verify_peer, error);
	if( not error )
		ctx.set_default_verify_paths(error);

	ctx.set_options (
		asio::ssl::context::default_workarounds |
		asio::ssl::context::no_tlsv1   |
		asio::ssl::context::no_tlsv1_1 |
		asio::ssl::context::no_sslv2   |
		asio::ssl::context::no_sslv3
	);

#if OPENSSL_VERSION_NUMBER < 0x10100000L
	SSL_CTX_set_options(ctx.native_handle(),
		SSL_OP_NO_TLSv1 | SSL_OP_NO_TLSv1_1
	);
#endif //OPENSSL_VERSION_NUMBER
}

#endif //RIWO_OPENSSL_SUPPORT

} //namespace riwo::http::detail
