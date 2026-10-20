// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "preface.h"

namespace riwo::http_exp
{

class RIWO_DECL_HIDDEN http2_preface_detector::impl
{
public:
	explicit impl(size_t max_bytes_value) :
		m_max_bytes(std::max(max_bytes_value, http2_preface.size()))
	{
	}

	size_t m_max_bytes;
	std::string m_buffer;
	preface_result m_result = preface_result::need_more;
};

http2_preface_detector::http2_preface_detector(
	size_t max_bytes) :
	m_impl(std::make_unique<impl>(max_bytes))
{
}

http2_preface_detector::~http2_preface_detector() =
	default;

preface_result http2_preface_detector::feed(const_buffer input)
{
	if( m_impl->m_result != preface_result::need_more )
		return m_impl->m_result;

	const auto *data = static_cast<const char *>(input.data());
	const auto available = m_impl->m_max_bytes -
		std::min(m_impl->m_max_bytes, m_impl->m_buffer.size());
	const auto count = std::min(input.size(), available);
	for( size_t index = 0; index < count; ++index )
	{
		m_impl->m_buffer.push_back(data[index]);
		const auto position = m_impl->m_buffer.size() - 1;
		if( position < http2_preface.size() )
		{
			if( m_impl->m_buffer[position] != http2_preface[position] )
			{
				m_impl->m_result = preface_result::http1;
				auto remaining = std::min(input.size() - index - 1,
					available - index - 1);
				if( remaining != 0 )
					m_impl->m_buffer.append(data + index + 1, remaining);
				return m_impl->m_result;
			}
			if( position + 1 == http2_preface.size() )
			{
				m_impl->m_result = preface_result::http2;
				auto remaining = std::min(input.size() - index - 1,
					available - index - 1);
				if( remaining != 0 )
					m_impl->m_buffer.append(data + index + 1, remaining);
				return m_impl->m_result;
			}
		}
	}
	if( m_impl->m_buffer.size() >= m_impl->m_max_bytes )
		m_impl->m_result = preface_result::http1;
	return m_impl->m_result;
}

preface_result http2_preface_detector::result() const noexcept {
	return m_impl->m_result;
}

const std::string &
http2_preface_detector::buffer() const noexcept {
	return m_impl->m_buffer;
}

size_t http2_preface_detector::buffered_size() const noexcept {
	return m_impl->m_buffer.size();
}

std::string http2_preface_detector::take_buffer()
{
	auto result = std::move(m_impl->m_buffer);
	m_impl->m_buffer.clear();
	return result;
}

void http2_preface_detector::reset() noexcept
{
	m_impl->m_buffer.clear();
	m_impl->m_result = preface_result::need_more;
}

void http2_preface_detector::detect(
	const std::shared_ptr<connection> &stream, detect_handler handler)
{
	if( not stream or m_impl->m_result != preface_result::need_more )
	{
		std::move(handler)(error_code{}, m_impl->m_result, take_buffer());
		return;
	}

	struct operation final : std::enable_shared_from_this<operation>
	{
		http2_preface_detector *detector;
		std::shared_ptr<connection> stream;
		detect_handler handler;
		std::array<char,http2_preface.size()> buffer {};

		operation(http2_preface_detector *detector_value,
			std::shared_ptr<connection> stream_value,
			detect_handler completion) : detector(detector_value),
			stream(std::move(stream_value)), handler(std::move(completion))
		{
		}

		void start()
		{
			auto missing = http2_preface.size() -
				std::min(http2_preface.size(), detector->buffered_size());
			auto self = shared_from_this();
			stream->read_some(mutable_buffer(buffer.data(), missing),
			[self = std::move(self)](error_code error, size_t size) mutable {
				if( error )
				{
					std::move(self->handler)(error, preface_result::http1,
						self->detector->take_buffer());
					return;
				}
				auto result = self->detector->feed(
					const_buffer(self->buffer.data(), size));
				if( result == preface_result::need_more )
				{
					self->start();
					return;
				}
				std::move(self->handler)(error_code{}, result,
					self->detector->take_buffer());
			});
		}
	};

	std::make_shared<operation>(this, stream,
		std::move(handler))->start();
}

} //namespace riwo::http_exp
