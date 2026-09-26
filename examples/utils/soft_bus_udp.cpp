// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/utils/sbus.h>
#include <iostream>

int main()
{
	using namespace std::chrono_literals;
	constexpr std::string_view topic = "riwo.example.sbus.udp";

	asio::thread_pool pool(1);
	riwo::utils::sbus::udp_subscriber subscriber(pool);

	std::atomic_bool received {false};
	subscriber.subscribe(topic, [&](asio::const_buffer payload)
	{
		std::cout << std::string_view(
			static_cast<const char*>(payload.data()), payload.size()
		) << '\n';
		received.store(true, std::memory_order_release);
	});
	riwo::utils::sbus::publish<riwo::utils::sbus::udp_interface>(
		topic, "hello over UDP multicast"
	);

	for(int retry = 0; retry < 2'000 and
		not received.load(std::memory_order_acquire); ++retry)
	{
		std::this_thread::sleep_for(1ms);
	}
	subscriber.cancel();
	pool.stop();
	pool.join();

	return received.load(std::memory_order_acquire) ? 0 : 1;
}
