// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_CLIENT_DETAIL_PROXY_TUNNEL_H
#define RIWO_HTTP_CLIENT_DETAIL_PROXY_TUNNEL_H

#include <riwo/http/cxx/attributes.h>

namespace riwo::http::detail
{

[[nodiscard]] RIWO_HTTP_API
std::string proxy_authority(const connect_target &target);

[[nodiscard]] RIWO_HTTP_API
error_code validate_proxy_tunnel(const connect_target &target, const proxy_tunnel &proxy) noexcept;

template <typename Stream>
RIWO_HTTP_TAPI error_code sync_http_connect
(Stream &stream, const connect_target &target, const proxy_tunnel &proxy)
{
	auto authority = proxy_authority(target);
	auto request = "CONNECT " + authority + " HTTP/1.1\r\nHost: " +
		authority + "\r\nProxy-Connection: Keep-Alive\r\n";

	if( proxy.authorization )
		request += "Proxy-Authorization: " + *proxy.authorization + "\r\n";
	request += "\r\n";

	error_code error;
	asio::write(stream, asio::buffer(request), error);
	if( error )
		return error;

	std::string header;
	header.reserve(1024);

	while( not header.ends_with("\r\n\r\n") )
	{
		if( header.size() >= 16 * 1024 )
			return make_system_error_code(std::errc::message_size);

		char byte = 0;
		asio::read(stream, asio::buffer(&byte, 1), error);
		if( error )
			return error;
		header.push_back(byte);
	}
	auto line_end = header.find("\r\n");
	auto first_space = header.find(' ');

	if( line_end == std::string::npos or first_space == std::string::npos or
		first_space + 4 > line_end or not header.starts_with("HTTP/") )
		return make_system_error_code(std::errc::protocol_error);

	const auto status = header.substr(first_space + 1, 3);
	if( status == "407" )
		return make_system_error_code(std::errc::permission_denied);

	if( status.size() != 3 or status[0] != '2' or
		status[1] < '0' or status[1] > '9' or
		status[2] < '0' or status[2] > '9' )
		return make_system_error_code(std::errc::connection_refused);
	return {};
}

template <typename Stream>
RIWO_HTTP_TAPI error_code sync_socks5_connect
(Stream &stream, const connect_target &target, const proxy_tunnel &proxy)
{
	error_code error;
	const bool credentials = proxy.username or proxy.password;

	std::array<uint8_t,4> greeting {5, static_cast<uint8_t>(credentials ? 2 : 1), 0, 2};
	asio::write(stream, asio::buffer(greeting.data(), credentials ? 4 : 3), error);
	if( error )
		return error;

	std::array<uint8_t,2> selection {};
	asio::read(stream, asio::buffer(selection), error);
	if( error )
		return error;

	if( selection[0] != 5 or selection[1] == 0xFF )
		return make_system_error_code(std::errc::permission_denied);

	if( selection[1] == 2 )
	{
		if( not credentials )
			return make_system_error_code(std::errc::permission_denied);

		const auto username = proxy.username.value_or("");
		const auto password = proxy.password.value_or("");

		std::vector<uint8_t> auth;
		auth.reserve(3 + username.size() + password.size());
		auth.insert(auth.end(), {1, static_cast<uint8_t>(username.size())});
		auth.insert(auth.end(), username.begin(), username.end());
		auth.push_back(static_cast<uint8_t>(password.size()));
		auth.insert(auth.end(), password.begin(), password.end());

		asio::write(stream, asio::buffer(auth), error);
		if( error )
			return error;

		asio::read(stream, asio::buffer(selection), error);
		if( error )
			return error;

		if( selection[0] != 1 or selection[1] != 0 )
			return make_system_error_code(std::errc::permission_denied);
	}
	else if( selection[1] != 0 )
		return make_system_error_code(std::errc::protocol_error);

	std::vector<uint8_t> request {5, 1, 0, 3,
		static_cast<uint8_t>(target.host.size())
	};
	request.insert(request.end(), target.host.begin(), target.host.end());
	request.push_back(static_cast<uint8_t>(target.port >> 8));
	request.push_back(static_cast<uint8_t>(target.port));

	asio::write(stream, asio::buffer(request), error);
	if( error )
		return error;

	std::array<uint8_t,4> response {};
	asio::read(stream, asio::buffer(response), error);
	if( error )
		return error;

	if( response[0] != 5 or response[2] != 0 )
		return make_system_error_code(std::errc::protocol_error);

	if( response[1] != 0 )
	{
		return response[1] == 2 ?
			make_system_error_code(std::errc::permission_denied) :
			make_system_error_code(std::errc::connection_refused);
	}
	size_t tail_size = 0;
	if( response[3] == 1 )
		tail_size = 4 + 2;

	else if( response[3] == 4 )
		tail_size = 16 + 2;

	else if( response[3] == 3 )
	{
		uint8_t length = 0;
		asio::read(stream, asio::buffer(&length, 1), error);
		if( error )
			return error;
		tail_size = length + 2;
	}
	else
		return make_system_error_code(std::errc::protocol_error);

	std::array<uint8_t,258> tail {};
	asio::read(stream, asio::buffer(tail.data(), tail_size), error);
	return error;
}

template <typename Stream>
RIWO_HTTP_TAPI error_code sync_proxy_connect
(Stream &stream, const connect_target &target, const proxy_tunnel &proxy)
{
	if( auto error = validate_proxy_tunnel(target, proxy) )
		return error;
	return proxy.type == proxy_tunnel_type::http_connect ?
		sync_http_connect(stream, target, proxy) :
		sync_socks5_connect(stream, target, proxy);
}

template <typename Stream>
RIWO_HTTP_TAPI awaitable<error_code> async_http_connect
(Stream &stream, const connect_target &target, const proxy_tunnel &proxy)
{
	auto authority = proxy_authority(target);
	auto request = "CONNECT " + authority + " HTTP/1.1\r\nHost: " +
		authority + "\r\nProxy-Connection: Keep-Alive\r\n";

	if( proxy.authorization )
		request += "Proxy-Authorization: " + *proxy.authorization + "\r\n";
	request += "\r\n";

	error_code error;
	co_await asio::async_write(stream, asio::buffer(request),
		asio::redirect_error(use_awaitable, error)
	);
	if( error )
		co_return error;

	std::string header;
	header.reserve(1024);

	while( not header.ends_with("\r\n\r\n") )
	{
		if( header.size() >= 16 * 1024 )
			co_return make_system_error_code(std::errc::message_size);

		char byte = 0;
		co_await asio::async_read(stream, asio::buffer(&byte, 1),
			asio::redirect_error(use_awaitable, error)
		);
		if( error )
			co_return error;
		header.push_back(byte);
	}
	auto line_end = header.find("\r\n");
	auto first_space = header.find(' ');

	if( line_end == std::string::npos or first_space == std::string::npos or
		first_space + 4 > line_end or not header.starts_with("HTTP/") )
		co_return make_system_error_code(std::errc::protocol_error);

	const auto status = header.substr(first_space + 1, 3);
	if( status == "407" )
		co_return make_system_error_code(std::errc::permission_denied);

	if( status.size() != 3 or status[0] != '2' or
		status[1] < '0' or status[1] > '9' or
		status[2] < '0' or status[2] > '9' )
		co_return make_system_error_code(std::errc::connection_refused);
	co_return error_code{};
}

template <typename Stream>
RIWO_HTTP_TAPI awaitable<error_code> async_socks5_connect
(Stream &stream, const connect_target &target, const proxy_tunnel &proxy)
{
	error_code error;
	const bool credentials = proxy.username or proxy.password;

	std::array<uint8_t,4> greeting {
		5, static_cast<uint8_t>(credentials ? 2 : 1), 0, 2
	};
	co_await asio::async_write(stream,
		asio::buffer(greeting.data(), credentials ? 4 : 3),
		asio::redirect_error(use_awaitable, error)
	);
	if( error )
		co_return error;

	std::array<uint8_t,2> selection {};
	co_await asio::async_read(stream, asio::buffer(selection),
		asio::redirect_error(use_awaitable, error)
	);
	if( error )
		co_return error;

	if( selection[0] != 5 or selection[1] == 0xFF )
		co_return make_system_error_code(std::errc::permission_denied);

	if( selection[1] == 2 )
	{
		if( not credentials )
			co_return make_system_error_code(std::errc::permission_denied);

		const auto username = proxy.username.value_or("");
		const auto password = proxy.password.value_or("");

		std::vector<uint8_t> auth;
		auth.reserve(3 + username.size() + password.size());
		auth.insert(auth.end(), {1, static_cast<uint8_t>(username.size())});
		auth.insert(auth.end(), username.begin(), username.end());
		auth.push_back(static_cast<uint8_t>(password.size()));
		auth.insert(auth.end(), password.begin(), password.end());

		co_await asio::async_write(stream, asio::buffer(auth),
			asio::redirect_error(use_awaitable, error)
		);
		if( error )
			co_return error;

		co_await asio::async_read(stream, asio::buffer(selection),
			asio::redirect_error(use_awaitable, error)
		);
		if( error )
			co_return error;

		if( selection[0] != 1 or selection[1] != 0 )
			co_return make_system_error_code(std::errc::permission_denied);
	}
	else if( selection[1] != 0 )
		co_return make_system_error_code(std::errc::protocol_error);

	std::vector<uint8_t> request {5, 1, 0, 3,
		static_cast<uint8_t>(target.host.size())
	};
	request.insert(request.end(), target.host.begin(), target.host.end());
	request.push_back(static_cast<uint8_t>(target.port >> 8));
	request.push_back(static_cast<uint8_t>(target.port));

	co_await asio::async_write(stream, asio::buffer(request),
		asio::redirect_error(use_awaitable, error)
	);
	if( error )
		co_return error;

	std::array<uint8_t,4> response {};
	co_await asio::async_read(stream, asio::buffer(response),
		asio::redirect_error(use_awaitable, error)
	);
	if( error )
		co_return error;

	if( response[0] != 5 or response[2] != 0 )
		co_return make_system_error_code(std::errc::protocol_error);

	if( response[1] != 0 )
	{
		co_return response[1] == 2 ?
			make_system_error_code(std::errc::permission_denied) :
			make_system_error_code(std::errc::connection_refused);
	}
	size_t tail_size = 0;
	if( response[3] == 1 )
		tail_size = 4 + 2;

	else if( response[3] == 4 )
		tail_size = 16 + 2;

	else if( response[3] == 3 )
	{
		uint8_t length = 0;
		co_await asio::async_read(stream, asio::buffer(&length, 1),
			asio::redirect_error(use_awaitable, error)
		);
		if( error )
			co_return error;
		tail_size = length + 2;
	}
	else
		co_return make_system_error_code(std::errc::protocol_error);

	std::array<uint8_t,258> tail {};
	co_await asio::async_read(stream, asio::buffer(tail.data(), tail_size),
		asio::redirect_error(use_awaitable, error)
	);
	co_return error;
}

template <typename Stream>
RIWO_HTTP_TAPI awaitable<error_code> async_proxy_connect
(Stream &stream, const connect_target &target, const proxy_tunnel &proxy)
{
	if( auto error = validate_proxy_tunnel(target, proxy) )
		co_return error;

	if( proxy.type == proxy_tunnel_type::http_connect )
		co_return co_await async_http_connect(stream, target, proxy);

	co_return co_await async_socks5_connect(stream, target, proxy);
}

} //namespace riwo::http::detail

#endif //RIWO_HTTP_CLIENT_DETAIL_PROXY_TUNNEL_H
