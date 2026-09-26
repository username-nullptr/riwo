// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "ctrl_payload.h"

namespace riwo::websocket
{

ctrl_payload::ctrl_payload(storage_type payload) noexcept :
	m_storage(std::move(payload))
{

}

ctrl_payload::ctrl_payload(std::string_view payload)
{
	assign(payload);
}

ctrl_payload::ctrl_payload(const char *payload) :
	ctrl_payload(std::string_view(payload))
{

}

ctrl_payload::ctrl_payload(const const_buffer &payload)
{
	assign(payload);
}

ctrl_payload::ctrl_payload(std::span<const std::byte> payload) :
	m_storage(payload.begin(), payload.end())
{

}

ctrl_payload &ctrl_payload::assign(std::string_view payload)
{
	if(payload.empty())
		m_storage.clear();
	else
	{
		const auto *first = reinterpret_cast<const std::byte *>(payload.data());
		m_storage.assign(first, first + payload.size());
	}
	return *this;
}

ctrl_payload &ctrl_payload::assign(const char *payload)
{
	return assign(std::string_view(payload));
}

ctrl_payload &ctrl_payload::assign(const const_buffer &payload)
{
	if(payload.size() == 0)
		m_storage.clear();
	else
	{
		const auto *first = static_cast<const std::byte *>(payload.data());
		m_storage.assign(first, first + payload.size());
	}
	return *this;
}

ctrl_payload &ctrl_payload::assign(std::span<const std::byte> payload)
{
	m_storage.assign(payload.begin(), payload.end());
	return *this;
}


ctrl_payload &ctrl_payload::operator=(std::string_view payload)
{
	assign(payload);
	return *this;
}

ctrl_payload &ctrl_payload::operator=(const char *payload)
{
	assign(std::string_view(payload));
	return *this;
}

void ctrl_payload::clear() noexcept
{
	m_storage.clear();
}

void ctrl_payload::resize(size_t size)
{
	m_storage.resize(size);
}

bool ctrl_payload::empty() const noexcept
{
	return m_storage.empty();
}

size_t ctrl_payload::size() const noexcept
{
	return m_storage.size();
}

std::byte *ctrl_payload::data() noexcept
{
	return m_storage.data();
}

const std::byte *ctrl_payload::data() const noexcept
{
	return m_storage.data();
}

std::string_view ctrl_payload::text() const noexcept
{
	return m_storage.empty() ?
		std::string_view{} :
		std::string_view (
			reinterpret_cast<const char *>(m_storage.data()),
			m_storage.size()
		);
}

std::span<std::byte> ctrl_payload::bytes() noexcept
{
	return m_storage;
}

std::span<const std::byte> ctrl_payload::bytes() const noexcept
{
	return m_storage;
}

mutable_buffer ctrl_payload::as_mutable_buffer() noexcept
{
	return { m_storage.data(), m_storage.size() };
}

const_buffer ctrl_payload::as_const_buffer() const noexcept
{
	return { m_storage.data(), m_storage.size() };
}

auto ctrl_payload::storage() noexcept -> storage_type&
{
	return m_storage;
}

auto ctrl_payload::storage() const noexcept -> const storage_type&
{
	return m_storage;
}

} //namespace riwo::websocket
