// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "preface.h"

namespace riwo::http_exp { namespace
{

struct RIWO_HTTP_EXP_TAPI operation final : std::enable_shared_from_this<operation>
{
	using detect_handler_t = v2_preface_detector::detect_handler_t;

	v2_preface_detector *detector = nullptr;
	std::array<char,v2_preface.size()> buffer {};

	connection_ptr stream {};
	detect_handler_t handler {};

	operation(v2_preface_detector *detector_value,
		connection_ptr stream_value, detect_handler_t completion) :
		detector(detector_value), stream(std::move(stream_value)),
		handler(std::move(completion)) {}

	void start()
	{
		auto missing = v2_preface.size() - std::min (
			v2_preface.size(), detector->buffered_size()
		);
		auto self = shared_from_this();

		stream->read_some(mutable_buffer(buffer.data(), missing),
		[self = std::move(self)](error_code error, size_t size) mutable
		{
			if( error )
			{
				std::move(self->handler)(error,
					preface_result::v1, self->detector->take_buffer()
				);
				return ;
			}
			auto result = self->detector->feed (
				const_buffer(self->buffer.data(), size)
			);
			if( result == preface_result::need_more )
			{
				self->start();
				return ;
			}
			std::move(self->handler)(error_code{},
				result, self->detector->take_buffer()
			);
		});
	}
};

} //namespace

v2_preface_detector::v2_preface_detector(size_t max_bytes) :
	m_max_bytes(std::max(max_bytes, v2_preface.size()))
{

}

preface_result v2_preface_detector::feed(const const_buffer &input)
{
	if( m_result != preface_result::need_more )
		return m_result;

	const auto *data = static_cast<const char *>(input.data());
	const auto available = m_max_bytes - std::min(m_max_bytes, m_buffer.size());
	const auto count = std::min(input.size(), available);

	for(size_t index=0; index<count; ++index)
	{
		m_buffer.push_back(data[index]);
		const auto position = m_buffer.size() - 1;

		if( position < v2_preface.size() )
		{
			if( m_buffer[position] != v2_preface[position] )
			{
				m_result = preface_result::v1;
				auto remaining = std::min (
					input.size() - index - 1, available - index - 1
				);
				if( remaining != 0 )
					m_buffer.append(data + index + 1, remaining);
				return m_result;
			}
			if( position + 1 == v2_preface.size() )
			{
				m_result = preface_result::v2;
				auto remaining = std::min (
					input.size() - index - 1, available - index - 1
				);
				if( remaining != 0 )
					m_buffer.append(data + index + 1, remaining);
				return m_result;
			}
		}
	}
	if( m_buffer.size() >= m_max_bytes )
		m_result = preface_result::v1;
	return m_result;
}

preface_result v2_preface_detector::result() const noexcept
{
	return m_result;
}

const std::string &v2_preface_detector::buffer() const noexcept
{
	return m_buffer;
}

size_t v2_preface_detector::buffered_size() const noexcept
{
	return m_buffer.size();
}

std::string v2_preface_detector::take_buffer()
{
	auto result = std::move(m_buffer);
	m_buffer.clear();
	return result;
}

void v2_preface_detector::reset() noexcept
{
	m_buffer.clear();
	m_result = preface_result::need_more;
}

void v2_preface_detector::detect(const connection_ptr &stream, detect_handler_t handler)
{
	if( not stream or m_result != preface_result::need_more )
	{
		std::move(handler)(error_code{}, m_result, take_buffer());
		return ;
	}
	std::make_shared<operation>(this, stream, std::move(handler))->start();
}

} //namespace riwo::http_exp
