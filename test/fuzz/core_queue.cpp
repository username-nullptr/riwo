// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/core/lock_free_queue.h>

#include <deque>

namespace
{

template <typename Queue>
void exercise_queue(const uint8_t *data, size_t size)
{
	size_t capacity = size_t(data[0] % 16U) + 1;
	Queue queue(capacity);
	std::deque<uint32_t> model;
	for(size_t index = 1; index < size; ++index)
	{
		const auto value = static_cast<uint32_t>(data[index]) |
			(static_cast<uint32_t>(index) << 8U);
		switch(data[index] % 5U)
		{
		case 0:
		case 1:
		{
			const bool expected = model.size() < capacity;
			const bool inserted = queue.enqueue(value);
			if(inserted != expected)
				std::abort();
			if(inserted)
				model.push_back(value);
			break;
		}
		case 2:
		{
			auto actual = queue.dequeue();
			if(bool(actual) != not model.empty())
				std::abort();
			if(actual)
			{
				if(*actual != model.front())
					std::abort();
				model.pop_front();
			}
			break;
		}
		case 3:
			capacity = size_t(data[(index + 1) % size] % 16U) + 1;
			queue.set_capacity(capacity);
			if(queue.capacity() != capacity)
				std::abort();
			break;
		default:
		{
			size_t expected_evicted = 0;
			while(model.size() >= capacity)
			{
				model.pop_front();
				++expected_evicted;
			}
			model.push_back(value);
			if(queue.force_emplace(value) != expected_evicted)
				std::abort();
			break;
		}
		}

		if(queue.size() != model.size() or queue.empty() != model.empty() or
			queue.full() != (model.size() >= capacity))
		{
			std::abort();
		}
	}

	while(not model.empty())
	{
		auto actual = queue.dequeue();
		if(not actual or *actual != model.front())
			std::abort();
		model.pop_front();
	}
	if(not queue.empty() or queue.size() != 0 or queue.dequeue())
		std::abort();
}

} //namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	if(size == 0 or size > RIWO_FUZZ_MAX_LENGTH)
		return 0;
	exercise_queue<riwo::circular_lock_free_queue<uint32_t>>(data, size);
	exercise_queue<riwo::linked_lock_free_queue<uint32_t>>(data, size);
	return 0;
}
