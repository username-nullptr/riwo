// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/utils/signal_slot.h>

namespace
{

std::atomic_uint64_t received_a {0};
std::atomic_uint64_t received_b {0};

void receive_value_a(uint8_t value)
{
	received_a.fetch_add(value, std::memory_order_relaxed);
}

void receive_value_b(uint8_t value)
{
	received_b.fetch_add(value, std::memory_order_relaxed);
}

} //namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	if(size == 0 or size > RIWO_FUZZ_MAX_LENGTH)
		return 0;

	riwo::utils::signal<void(uint8_t)> signal;
	received_a.store(0, std::memory_order_relaxed);
	received_b.store(0, std::memory_order_relaxed);
	uint64_t expected_a = 0;
	uint64_t expected_b = 0;
	bool connected_a = false;
	bool connected_b = false;
	bool blocked = false;
	for(size_t index = 0; index < size; ++index)
	{
		switch(data[index] % 8)
		{
		case 0:
			signal.connect(receive_value_a);
			connected_a = true;
			break;
		case 1:
			signal.disconnect(receive_value_a);
			connected_a = false;
			break;
		case 2:
			signal.connect(receive_value_b);
			connected_b = true;
			break;
		case 3:
			signal.disconnect(receive_value_b);
			connected_b = false;
			break;
		case 4:
			signal.disconnect();
			connected_a = false;
			connected_b = false;
			break;
		case 5:
			blocked = (data[index] & 0x80U) != 0;
			signal.block(blocked);
			break;
		case 6:
		{
			const auto value = data[(index + 1) % size];
			signal(value);
			if(not blocked and connected_a)
				expected_a += value;
			if(not blocked and connected_b)
				expected_b += value;
			break;
		}
		default:
			break;
		}
		if(signal.is_blocked() != blocked or
			received_a.load(std::memory_order_relaxed) != expected_a or
			received_b.load(std::memory_order_relaxed) != expected_b)
			std::abort();
	}
	signal.disconnect();
	const auto before_a = received_a.load(std::memory_order_relaxed);
	const auto before_b = received_b.load(std::memory_order_relaxed);
	signal.block(false);
	signal(data[0]);
	if(received_a.load(std::memory_order_relaxed) != before_a or
		received_b.load(std::memory_order_relaxed) != before_b)
		std::abort();
	return 0;
}
