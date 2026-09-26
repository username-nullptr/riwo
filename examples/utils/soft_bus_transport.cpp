// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/utils/sbus.h>
#include <iostream>
#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

// A real adapter can map these methods to DDS, another IPC system, or a network.
class transport_interface
{
public:
	static void publish(std::string_view topic, const void *data, size_t size)
	{
		riwo::utils::sbus::local_interface::publish(topic, data, size);
	}

	uint64_t subscribe(std::string_view topic,std::function<void(const void*,size_t)> callback)
	{
		return m_local->subscribe(topic, std::move(callback));
	}

	uint64_t subscribe(std::function<void(std::string_view,const void*,size_t)> callback)
	{
		return m_local->subscribe(std::move(callback));
	}

	void cancel_topic(std::string_view topic)
	{
		m_local->cancel_topic(topic);
	}

	void cancel_sid(uint64_t sid)
	{
		m_local->cancel_sid(sid);
	}

	void cancel()
	{
		m_local->cancel();
	}

private:
	std::shared_ptr<riwo::utils::sbus::local_interface> m_local =
		std::make_shared<riwo::utils::sbus::local_interface>();
};

using transport_subscriber =
	riwo::utils::sbus::basic_subscriber<transport_interface>;

std::atomic_bool received = false;

int main()
{
	asio::thread_pool pool(1);
	transport_subscriber subscriber(pool);

	subscriber.subscribe("example.transport", [](const void *data, size_t size)
	{
		std::cout << std::string_view(static_cast<const char*>(data), size) << '\n';
		received = true;
	});

	riwo::utils::sbus::publish<transport_interface>(
		"example.transport", "replace this adapter with DDS"
	);
	for(int retry = 0; retry < 100 and not received; ++retry)
		std::this_thread::sleep_for(std::chrono::milliseconds(1));

	pool.stop();
	pool.join();
	return received ? 0 : 1;
}
