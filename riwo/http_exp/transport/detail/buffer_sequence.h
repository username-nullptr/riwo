// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_TRANSPORT_DETAIL_BUFFER_SEQUENCE_H
#define RIWO_HTTP_EXP_TRANSPORT_DETAIL_BUFFER_SEQUENCE_H

#include <riwo/http_exp/global.h>
#include <span>

namespace riwo::http_exp::detail
{

class const_buffer_sequence
{
	static constexpr size_t inline_capacity = 4;

public:
	using value_type = const_buffer;
	using const_iterator = const value_type*;

	explicit const_buffer_sequence(std::span<const const_buffer> buffers)
	{
		m_size = buffers.size();
		if( m_size <= inline_capacity )
			std::ranges::copy(buffers, m_inline.begin());
		else
			m_dynamic.assign(buffers.begin(), buffers.end());
	}

	[[nodiscard]] const_iterator begin() const noexcept
	{
		return m_size <= inline_capacity ? m_inline.data() : m_dynamic.data();
	}

	[[nodiscard]] const_iterator end() const noexcept
	{
		return begin() + m_size;
	}

private:
	std::array<const_buffer,inline_capacity> m_inline {};
	std::vector<const_buffer> m_dynamic {};
	size_t m_size = 0;
};

} //namespace riwo::http_exp::detail


#endif //RIWO_HTTP_EXP_TRANSPORT_DETAIL_BUFFER_SEQUENCE_H
