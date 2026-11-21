// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_TEST_SBUS_UDP_FIXTURE_H
#define RIWO_TEST_SBUS_UDP_FIXTURE_H

#include <riwo/utils/sbus.h>

namespace riwo::test
{

class udp_port_reservation
{
	using udp = asio::ip::udp;

public:
	udp_port_reservation()
	{
		// Keep the OS-assigned port reserved while multicast receivers share it.
		m_socket.set_option(asio::socket_base::reuse_address(true));
		m_socket.bind(udp::endpoint(udp::v4(), 0));
	}

	[[nodiscard]] uint16_t port() const {
		return m_socket.local_endpoint().port();
	}

private:
	asio::io_context m_context;
	udp::socket m_socket {m_context, udp::v4()};
};

class sbus_udp_fixture
{
	using udp_interface = utils::sbus::udp_interface;

public:
	sbus_udp_fixture()
	{
		udp_interface::config_t config {};
		config.multicast_port = m_port.port();
		udp_interface::set_config(config);
	}

	~sbus_udp_fixture() {
		udp_interface::set_config(m_previous);
	}

private:
	udp_interface::config_t m_previous {udp_interface::config()};
	udp_port_reservation m_port;
};

} //namespace riwo::test

#endif //RIWO_TEST_SBUS_UDP_FIXTURE_H
