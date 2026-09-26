// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "utf8.h"

namespace riwo::websocket::detail
{

class RIWO_DECL_HIDDEN utf8_validator::impl
{
public:
	impl() = default;
	uint8_t m_remaining = 0;
	uint8_t m_lower = 0x80;
	uint8_t m_upper = 0xBF;
};

utf8_validator::utf8_validator() :
	m_impl(new impl())
{

}

utf8_validator::~utf8_validator()
{
	delete m_impl;
}

utf8_validator::utf8_validator(const utf8_validator &other) :
	m_impl(other.m_impl ? new impl(*other.m_impl) : new impl())
{

}

utf8_validator::utf8_validator(utf8_validator &&other) noexcept :
	m_impl(std::exchange(other.m_impl, nullptr))
{

}

utf8_validator &utf8_validator::operator=(const utf8_validator &other)
{
	if( this == &other )
		return *this;

	auto replacement = other.m_impl ?
		std::make_unique<impl>(*other.m_impl) : std::make_unique<impl>();
	delete m_impl;
	m_impl = replacement.release();
	return *this;
}

utf8_validator &utf8_validator::operator=(utf8_validator &&other) noexcept
{
	if( this == &other )
		return *this;

	delete m_impl;
	m_impl = std::exchange(other.m_impl, nullptr);
	return *this;
}

bool utf8_validator::consume(std::string_view text) noexcept
{
	if( not m_impl )
		return false;

	return std::ranges::all_of(text, [this](auto value)
	{
		const auto byte = static_cast<uint8_t>(value);
		if( m_impl->m_remaining != 0 )
		{
			if( byte < m_impl->m_lower or byte > m_impl->m_upper )
				return false;

			--m_impl->m_remaining;
			m_impl->m_lower = 0x80;
			m_impl->m_upper = 0xBF;
			return true;
		}
		if( byte <= 0x7F )
			return true;

		if( byte >= 0xC2 and byte <= 0xDF )
			m_impl->m_remaining = 1;

		else if( byte == 0xE0 )
		{
			m_impl->m_remaining = 2;
			m_impl->m_lower = 0xA0;
		}
		else if( (byte >= 0xE1 and byte <= 0xEC) or (byte >= 0xEE and byte <= 0xEF) )
			m_impl->m_remaining = 2;

		else if( byte == 0xED )
		{
			m_impl->m_remaining = 2;
			m_impl->m_upper = 0x9F;
		}
		else if( byte == 0xF0 )
		{
			m_impl->m_remaining = 3;
			m_impl->m_lower = 0x90;
		}
		else if( byte >= 0xF1 and byte <= 0xF3 )
			m_impl->m_remaining = 3;

		else if( byte == 0xF4 )
		{
			m_impl->m_remaining = 3;
			m_impl->m_upper = 0x8F;
		}
		else
			return false;
		return true;
	});
}

bool utf8_validator::complete() const noexcept
{
	return m_impl and m_impl->m_remaining == 0;
}

bool is_valid_utf8(std::string_view text) noexcept
{
	utf8_validator validator;
	return validator.consume(text) and validator.complete();
}

} //namespace riwo::websocket::detail
