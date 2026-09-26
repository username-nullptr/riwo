// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "test.h"

#include <riwo/websocket/client.h>
#include <riwo/websocket/server.h>
#include <riwo/core/system/app_utls.h>

#include <thread>

namespace
{

namespace ws = riwo::websocket;

class thread_joiner
{
public:
	explicit thread_joiner(std::thread &thread) noexcept : m_thread(thread) {}

	thread_joiner(const thread_joiner &) = delete;
	thread_joiner &operator=(const thread_joiner &) = delete;

	~thread_joiner()
	{
		if( m_thread.joinable() )
			m_thread.join();
	}

private:
	std::thread &m_thread;
};

class scoped_environment
{
	struct entry
	{
		std::string name;
		riwo::optional<std::string> value;
	};

public:
	scoped_environment(std::initializer_list<std::string_view> names)
	{
		for(const auto name : names)
		{
			auto value = riwo::app::getenv(name);
			m_entries.push_back({std::string(name), value ?
				riwo::optional<std::string>(*value) : riwo::nullopt});
		}
	}

	~scoped_environment()
	{
		for(const auto &item : m_entries)
		{
			if( item.value )
				riwo::ignore_unused(riwo::app::setenv(item.name, *item.value));
			else
				riwo::ignore_unused(riwo::app::unsetenv(item.name));
		}
	}

private:
	std::vector<entry> m_entries;
};

// Test-only CA/server identity for 127.0.0.1 and localhost. Keeping it in the
// binary makes the TLS round trip hermetic and avoids invoking OpenSSL tools.
constexpr std::string_view certificate = R"PEM(-----BEGIN CERTIFICATE-----
MIIDNzCCAh+gAwIBAgIUG+Y00SCSOoyXCDj+ErjHjy49UhcwDQYJKoZIhvcNAQEL
BQAwFDESMBAGA1UEAwwJbG9jYWxob3N0MCAXDTI2MDkxMTAxNTIxNVoYDzIxMjYw
ODE4MDE1MjE1WjAUMRIwEAYDVQQDDAlsb2NhbGhvc3QwggEiMA0GCSqGSIb3DQEB
AQUAA4IBDwAwggEKAoIBAQCXDa32OqIeVfXD+eEUUVrr668n5ZpPkn/wUrmq+A1x
+HC11RLHGS1A6xs/oqSyRo3M0OVQ8+ZvsNFJ8IjkDp2elIdfG1BNkO+NSxWRg2R4
uPiQb7blCAc4V/weqOihP3/Lr2USAhendMWKtWWgw1H/oftLL4M14dOj0LLABSBz
zwC3dSxmnqBoPA8m0hFhAQcExwBMcebms+MAsQB1R3dC+ek/alVXiL6I8oJVJ5ZN
ZigjefuU3rgT8SIJunfk+t8WjDjPb0mKrBrwNjdIyRdqsMWIKDSfYWWkaxYJBMdu
wKyXZ6Vqeq3Rok6DxTQjbcc9wBYSkTbp6FRg1VsZjmHZAgMBAAGjfzB9MB0GA1Ud
DgQWBBTNAXXOO5neJJbN9+ku4UsVhNudXTAfBgNVHSMEGDAWgBTNAXXOO5neJJbN
9+ku4UsVhNudXTAaBgNVHREEEzARgglsb2NhbGhvc3SHBH8AAAEwDwYDVR0TAQH/
BAUwAwEB/zAOBgNVHQ8BAf8EBAMCAqQwDQYJKoZIhvcNAQELBQADggEBAHCzj6oQ
dhdLx+dYnmgsC//vDrv2hdQQFOdzG0QYs/5qnt/Wm+gb+qMs6gCSbw+l/gpVyMEo
iLHfsDhDEa07nkRl2SUFE6e7VQVLjACd3F3O2BNUgwHgxnqmcQRSwt+/1XyurHue
33V81vNrQIH5lpxxzAOL8B9JlNqNOrg+DI4gj5OV+YIgPeWQZgUSolQpZs75KWRQ
KCgL+GDaKVG/WCtK2S52iDyAf903R5gCgELYbx6JXzQ0zRiy4dGKpsUOMoQdQlOE
8go2/Jrlo4funkaoCmqIvsKNW5xmmp0glwsTAA8LlP0WzC6Cx1hTPWKDF8nljdfR
o2v1PpYnNtf+HfI=
-----END CERTIFICATE-----
)PEM";

constexpr std::string_view private_key = R"PEM(-----BEGIN PRIVATE KEY-----
MIIEvgIBADANBgkqhkiG9w0BAQEFAASCBKgwggSkAgEAAoIBAQCXDa32OqIeVfXD
+eEUUVrr668n5ZpPkn/wUrmq+A1x+HC11RLHGS1A6xs/oqSyRo3M0OVQ8+ZvsNFJ
8IjkDp2elIdfG1BNkO+NSxWRg2R4uPiQb7blCAc4V/weqOihP3/Lr2USAhendMWK
tWWgw1H/oftLL4M14dOj0LLABSBzzwC3dSxmnqBoPA8m0hFhAQcExwBMcebms+MA
sQB1R3dC+ek/alVXiL6I8oJVJ5ZNZigjefuU3rgT8SIJunfk+t8WjDjPb0mKrBrw
NjdIyRdqsMWIKDSfYWWkaxYJBMduwKyXZ6Vqeq3Rok6DxTQjbcc9wBYSkTbp6FRg
1VsZjmHZAgMBAAECggEACNlTDdfo++R2wYyPqDhM3oG/0NNFSzCOoKZD/LVL2lee
yLQrBbFeoo0zWzK8ubni4DMDoA6tuvDNjeqSpmOJzcqvNH3d4jFhqzIRGSVsmxrg
6ts0iG3EOIOFAEnOWPXml2jWv6uSNavkT6gpRDX1BYf4d/LYWa15T8Ev24phWI9a
jz0/VFiye2mls4HN0bE3BnB+DdhoSJHHNSouJ0MDhD9ZX6fx2345oYpe/z76ttis
RyzIqTRHlBqEJ2SXEDj53/dF5XcxU755Iatg1Savuemv/AEbgnUlOz6HaUMxcNqL
0fd4KAb2/G7Oi6tZ+ZL4waKLrY7YtNPbxuarr+1DtQKBgQDOOejVZu/7yGlMPrIk
KAKXej3yP17prjnm7o5YJZrWjMsxufq2tZ3bTTUps3TTHCDDAw1SUQJMl98BhZVJ
VPcT5BIgqToLzDiTNQR4yGXYBp+y11VuF80lIiAwAFe61OAnt+6WBgsBpFKr/xiu
qrRk43JLkvg7fGM69kbdy9MmhQKBgQC7gs/mAPbJBisBG80Wtt2/h6I10dTzp1PP
CFMDKooNbqChjMhrYGp4j70+Yqd6xfUnhlcZ69TEJKF4n28uveNH6KLPvGGhXhzJ
yNztbOY290jXO6Ty7xbIE3yl3lxabgk3I+Z5KciJ/mpswmSQeot0BAiXCA5SHdES
5c0DMKEARQKBgQDNVSF74YtO8cPOE5rBn0i9VAx7uBcjqsKiKaJ3J7Icr4DdmSSF
aR2srhoh1DmcvSPtp4tLC7ezVX/Ifx4eLsf3+R0Hghd2ibdG8wHp1PZ6elXl9rtr
66zprSnJQX4YWz54rY2TuJ6a2ucps8v6laMZ1NEHaGVarUYL/gyfaANsFQKBgEKP
EXrkNrVukg9Hrgn1CUt2Orb976g5af9gRg8mp3BTJ4OQtIeg5so6w4MEg8yJvha0
kfBqjNC+6+4kMdQWpTmeM0Sn6sPb4z4hJYLFmAZEyr9TtZ5iDeUhPaqm/oM+8dh3
ztSNZ1jMTTyj2AyM4zlgQShTPLobSV564cXTGiM9AoGBALJ0CUaAzQB+2/dLOPPv
p1qYLtIHS6d8fgvIPB4Am5E/xqKmA33Cpoupxn+6/SeBP1jLmZOmOwVZwsrqWu53
qDi2BO5EEQJEuR4fhJkP2OsAqiC6ytGxwiBN1p2moipNGZwYnbMuAdYbAHMmvucx
rVFcoTPAjw+pZOOdRgD1V3TG
-----END PRIVATE KEY-----
)PEM";

void relay_socket(const std::shared_ptr<asio::ip::tcp::socket> &source,
	const std::shared_ptr<asio::ip::tcp::socket> &destination)
{
	std::array<std::byte,16 * 1024> buffer {};
	riwo::error_code error;
	for(;;)
	{
		auto size = source->read_some(asio::buffer(buffer), error);
		if( error )
			break;
		asio::write(*destination, asio::buffer(buffer.data(), size), error);
		if( error )
			break;
	}
	destination->shutdown(asio::ip::tcp::socket::shutdown_send, error);
}

void secure_round_trip()
{
	const scoped_environment environment({
		"wss_proxy", "WSS_PROXY", "no_proxy", "NO_PROXY",
	});
	riwo::io_context_t context;
	asio::ssl::context server_tls(asio::ssl::context::tls_server);
	server_tls.set_options(
		asio::ssl::context::default_workarounds |
		asio::ssl::context::no_sslv2 |
		asio::ssl::context::no_sslv3
	);
	server_tls.use_certificate_chain(
		asio::buffer(certificate.data(), certificate.size()));
	server_tls.use_private_key(
		asio::buffer(private_key.data(), private_key.size()),
		asio::ssl::context::pem);

	asio::ip::tcp::acceptor acceptor(context);
	ws::tls_server service({std::move(acceptor), server_tls});
	service.bind({riwo::ip_type::v4, 0}).start();
	const auto port = service.http_server().acceptor_wrap()
		.acceptor().local_endpoint().port();

	asio::io_context proxy_context;
	asio::ip::tcp::acceptor proxy_acceptor(proxy_context,
		{asio::ip::address_v4::loopback(), 0});
	const auto proxy_port = proxy_acceptor.local_endpoint().port();
	RIWO_TEST_CHECK(riwo::app::setenv("wss_proxy", std::format(
		"http://user:secret@127.0.0.1:{}/", proxy_port)));
#if !defined(_WIN32)
	RIWO_TEST_CHECK(riwo::app::unsetenv("WSS_PROXY"));
#endif
	RIWO_TEST_CHECK(riwo::app::unsetenv("no_proxy"));
	RIWO_TEST_CHECK(riwo::app::unsetenv("NO_PROXY"));
	std::exception_ptr proxy_error;
	std::thread proxy_thread([&]
	{
		try {
			auto downstream = std::make_shared<asio::ip::tcp::socket>(proxy_context);
			proxy_acceptor.accept(*downstream);
			std::string header;
			riwo::error_code error;
			while( not header.ends_with("\r\n\r\n") )
			{
				char byte = 0;
				asio::read(*downstream, asio::buffer(&byte, 1), error);
				if( error or header.size() >= 16 * 1024 )
					throw std::system_error(error ? error :
						riwo::make_system_error_code(std::errc::message_size));
				header.push_back(byte);
			}
			const auto expected_target = std::format(
				"CONNECT 127.0.0.1:{} HTTP/1.1\r\n", port);
			if( not header.starts_with(expected_target) or
				header.find("Proxy-Authorization: Basic dXNlcjpzZWNyZXQ=\r\n") ==
					std::string::npos )
				throw std::runtime_error("invalid HTTP CONNECT request");

			auto upstream = std::make_shared<asio::ip::tcp::socket>(proxy_context);
			upstream->connect({asio::ip::address_v4::loopback(), port});
			constexpr std::string_view connected =
				"HTTP/1.1 200 Connection Established\r\n\r\n";
			asio::write(*downstream, asio::buffer(connected));

			std::thread outbound([=] { relay_socket(downstream, upstream); });
			thread_joiner outbound_joiner(outbound);
			relay_socket(upstream, downstream);
		}
		catch(...) {
			proxy_error = std::current_exception();
		}
	});
	thread_joiner proxy_thread_joiner(proxy_thread);

	auto accepted = asio::co_spawn(context,
		[&]() -> riwo::awaitable<void>
		{
			auto connection = co_await service.accept(riwo::use_awaitable);
			RIWO_TEST_CHECK_EQ(connection.request.path, "/secure/echo");
			auto message = co_await connection.stream.read<std::string>(
				riwo::use_awaitable);
			RIWO_TEST_CHECK_EQ(message.body, "hello over TLS");
			co_await connection.stream.write_text(
				"secure: " + message.body, riwo::use_awaitable);
			auto close_result = co_await connection.stream.read<std::string>(
				asio::as_tuple(riwo::use_awaitable));
			auto &[close_error, trailing] = close_result;
			riwo::ignore_unused(close_error, trailing);
			co_return;
		}, asio::use_future);

	asio::ssl::context client_tls(asio::ssl::context::tls_client);
	client_tls.set_verify_mode(asio::ssl::verify_peer);
	client_tls.add_certificate_authority(
		asio::buffer(certificate.data(), certificate.size()));
	auto connector = std::make_shared<riwo::http::connector>(
		context.get_executor(), client_tls);
	riwo::http::connection_pool pool(std::move(connector));
	riwo::http::client http_client(std::move(pool));
	ws::client client(std::move(http_client));

	auto connected = asio::co_spawn(context,
		[&]() -> riwo::awaitable<void>
		{
			ws::connect_request request(std::format(
				"wss://127.0.0.1:{}/secure/echo", port));
			RIWO_TEST_CHECK(not request.proxy);
			ws::open_diagnostics diagnostics;
			auto stream = co_await client.open(std::move(request), diagnostics,
				riwo::use_awaitable);
			RIWO_TEST_CHECK_EQ(diagnostics.endpoint.protocol(), "wss");
			RIWO_TEST_CHECK(diagnostics.reply);
			RIWO_TEST_CHECK_EQ(diagnostics.reply->status(),
				riwo::http::status::switching_protocols);
			co_await stream.write_text("hello over TLS", riwo::use_awaitable);
			auto response = co_await stream.read<std::string>(
				riwo::use_awaitable);
			RIWO_TEST_CHECK_EQ(response.body, "secure: hello over TLS");
			auto close_result = co_await stream.close(
				asio::as_tuple(riwo::use_awaitable));
			auto &[close_error, closed] = close_result;
			service.stop();
			RIWO_TEST_CHECK(not close_error);
			RIWO_TEST_CHECK(closed.clean);
			co_return;
		}, asio::use_future);

	context.run();
	accepted.get();
	connected.get();
	proxy_thread.join();
	if( proxy_error )
		std::rethrow_exception(proxy_error);
}

} //namespace

int main(int argc, const char *const argv[])
{
	return riwo::test::run(argc, argv, {
		{"secure round trip", secure_round_trip},
	});
}
