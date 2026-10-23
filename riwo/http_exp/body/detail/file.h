// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_BODY_DETAIL_FILE_H
#define RIWO_HTTP_EXP_BODY_DETAIL_FILE_H

namespace riwo::http_exp
{

template <typename Stream>
requires (concepts::input_file_stream<Stream> or concepts::output_file_stream<Stream>)
borrowed_file_ref<Stream>::borrowed_file_ref(Stream &stream) noexcept :
	m_stream(&stream)
{

}

template <typename Stream>
requires (concepts::input_file_stream<Stream> or concepts::output_file_stream<Stream>)
borrowed_file_ref<Stream>::borrowed_file_ref(borrowed_file_ref &&other) noexcept :
	m_stream(std::exchange(other.m_stream, nullptr))
{

}

template <typename Stream>
requires (concepts::input_file_stream<Stream> or concepts::output_file_stream<Stream>)
borrowed_file_ref<Stream> &borrowed_file_ref<Stream>::operator=(borrowed_file_ref &&other) noexcept
{
	if( this != &other )
		m_stream = std::exchange(other.m_stream, nullptr);
	return *this;
}

template <typename Stream>
requires (concepts::input_file_stream<Stream> or concepts::output_file_stream<Stream>)
Stream *borrowed_file_ref<Stream>::get() const noexcept
{
	return m_stream;
}

template <typename Stream>
requires (concepts::input_file_stream<Stream> or concepts::output_file_stream<Stream>)
Stream *borrowed_file_ref<Stream>::operator*() const noexcept
{
	return m_stream;
}

template <typename Stream>
borrowed_file_ref<Stream> borrowed_file(Stream &stream) noexcept
	requires (concepts::input_file_stream<Stream> or concepts::output_file_stream<Stream>)
{
	return borrowed_file_ref<Stream>(stream);
}

} //namespace riwo::http_exp


#endif //RIWO_HTTP_EXP_BODY_DETAIL_FILE_H
