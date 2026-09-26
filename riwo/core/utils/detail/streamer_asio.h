// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_CXX_DETAIL_STREAMER_ASIO_H
#define RIWO_CORE_CXX_DETAIL_STREAMER_ASIO_H

#include <riwo/core/cxx/asio.h>
#include <chrono>

namespace riwo
{

template <>
struct streamer<asio::ip::address>
{
	using addr_t = asio::ip::address;

	[[nodiscard]] static auto encode(const addr_t &v) {
		return streamer<std::string>::encode(v.to_string());
	}

	[[nodiscard]] static decoder_data<addr_t>
	decode(const std::vector<std::byte> &buf, size_t offset = 0)
	{
		auto data = streamer<std::string>::decode(buf, offset);
		return { asio::ip::make_address(*data), data.size };
	}
};

template <>
struct streamer<asio::ip::address_v4>
{
	using addr_t = asio::ip::address_v4;

	[[nodiscard]] static auto encode(const addr_t &v) {
		return streamer<std::string>::encode(v.to_string());
	}

	[[nodiscard]] static decoder_data<addr_t>
	decode(const std::vector<std::byte> &buf, size_t offset = 0)
	{
		auto data = streamer<std::string>::decode(buf, offset);
		return { asio::ip::make_address_v4(*data), data.size };
	}
};

template <>
struct streamer<asio::ip::address_v6>
{
	using addr_t = asio::ip::address_v6;

	[[nodiscard]] static auto encode(const addr_t &v) {
		return streamer<std::string>::encode(v.to_string());
	}

	[[nodiscard]] static decoder_data<addr_t>
	decode(const std::vector<std::byte> &buf, size_t offset = 0)
	{
		auto data = streamer<std::string>::decode(buf, offset);
		return { asio::ip::make_address_v6(*data), data.size };
	}
};

template <typename InternetProtocol>
struct streamer<asio::ip::basic_endpoint<InternetProtocol>>
{
	using endpoint_t = asio::ip::basic_endpoint<InternetProtocol>;

	[[nodiscard]] static auto encode(const endpoint_t &v)
	{
		auto buf = streamer<asio::ip::address>::encode(v.address());
		auto sub = streamer<asio::ip::port_type>::encode(v.port()); // uint16_t
		buf.insert(buf.end(),
			std::make_move_iterator(sub.begin()),
			std::make_move_iterator(sub.end())
		);
		return buf;
	}

	[[nodiscard]] static decoder_data<endpoint_t>
	decode(const std::vector<std::byte> &buf, size_t offset = 0)
	{
		auto addr = streamer<asio::ip::address>::decode(buf, offset);
		auto port = streamer<asio::ip::port_type>::decode(buf, offset + addr.size);
		return { endpoint_t { *addr, *port }, addr.size + port.size };
	}
};

} //namespace riwo


#endif //RIWO_CORE_CXX_DETAIL_STREAMER_ASIO_H
