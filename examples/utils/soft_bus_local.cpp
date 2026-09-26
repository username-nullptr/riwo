// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/utils/sbus.h>
#include <iostream>
#include <atomic>
#include <chrono>
#include <thread>

namespace
{

std::atomic_bool received = false;
std::atomic_bool changed = false;

bool wait_for(std::atomic_bool &state)
{
	for(int retry = 0; retry < 100 and not state; ++retry)
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	return state;
}

} //namespace

int main()
{
	constexpr auto topic = "example.counter";
	asio::thread_pool pool(1);

	riwo::utils::sbus::local_subscriber subscriber(pool);
	subscriber.subscribe(topic, [](int value)
	{
		std::cout << "Received " << value << '\n';
		received = true;
	});
	riwo::utils::sbus::publish<riwo::utils::sbus::local_interface>(topic, 42);

	riwo::utils::sbus::local_cache cache(pool);

	cache.changed(topic).connect(
	[](std::vector<std::byte>, std::vector<std::byte>) {
		changed = true;
	});

	cache.set(topic, 7);
	std::cout << "Cached " << cache.get<int>(topic).value_or(0) << '\n';

	const bool delivered =
		wait_for(received) and wait_for(changed);

	pool.stop();
	pool.join();
	return delivered ? 0 : 1;
}
