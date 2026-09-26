// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/http/utils/connection.h>

namespace riwo::http::detail
{

const_buffer_sequence::const_buffer_sequence(std::span<const const_buffer> buffers)
{
	m_size = buffers.size();
	if( m_size <= inline_capacity )
		std::ranges::copy(buffers, m_inline.begin());
	else
		m_dynamic.assign(buffers.begin(), buffers.end());
}

std::span<const const_buffer> const_buffer_sequence::buffers() const noexcept
{
	if( m_size <= inline_capacity )
		return {m_inline.data(), m_size};
	return m_dynamic;
}

auto const_buffer_sequence::begin() const noexcept -> const_iterator
{
	return buffers().data();
}

auto const_buffer_sequence::end() const noexcept -> const_iterator
{
	auto sequence = buffers();
	return sequence.data() + sequence.size();
}

std::shared_ptr<std::string> copy_buffer(const const_buffer &buffer)
{
	auto result = std::make_shared<std::string>();
	if( buffer.size() > 0 )
	{
		result->assign(
			static_cast<const char*>(buffer.data()), buffer.size()
		);
	}
	return result;
}

std::shared_ptr<std::string> copy_buffers(std::span<const const_buffer> buffers)
{
	auto result = std::make_shared<std::string>();
	size_t size = 0;
	for( const auto &buffer : buffers )
	{
		if( buffer.size() > result->max_size() - size )
			length_error::loc_throw("riwo::http::basic_connection::write");
		size += buffer.size();
	}
	result->reserve(size);
	for( const auto &buffer : buffers )
	{
		if( buffer.size() > 0 )
		{
			result->append (
				static_cast<const char*>(buffer.data()),
				buffer.size()
			);
		}
	}
	return result;
}

endpoint to_endpoint(const asio::ip::tcp::endpoint &value) noexcept
{
	return { .address = value.address(), .port = value.port() };
}

} //namespace riwo::http::detail
