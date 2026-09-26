// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/core/lock_free_queue.h>
#include <riwo/core/jthread.h>
#include <iostream>
#include <atomic>
#include <thread>

int main()
{
	constexpr int item_count = 100;
	riwo::lock_free_queue<int,riwo::queue_type::circular,32> queue;

	std::atomic_int consumed = 0;
	std::atomic_bool producer_done = false;

	riwo::jthread producer([&]
	{
		for(int value = 0; value < item_count; ++value)
		{
			while(not queue.enqueue(value))
				std::this_thread::yield();
		}
		producer_done = true;
	});

	riwo::jthread consumer([&]
	{
		while(not producer_done or not queue.empty())
		{
			if(queue.dequeue())
				++consumed;
			else
				std::this_thread::yield();
		}
	});
	producer.join();
	consumer.join();

	std::cout << "Consumed " << consumed << " items\n";
	return consumed == item_count ? 0 : 1;
}
