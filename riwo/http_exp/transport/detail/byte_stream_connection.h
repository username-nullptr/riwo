// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_TRANSPORT_DETAIL_BYTE_STREAM_CONNECTION_H
#define RIWO_HTTP_EXP_TRANSPORT_DETAIL_BYTE_STREAM_CONNECTION_H

#include <riwo/http_exp/transport/byte_stream.h>
#include <riwo/http_exp/transport/connection.h>
#include <charconv>
#include <cstring>

namespace riwo::http_exp::detail
{

template<core_concepts::exec Exec>
class byte_stream_connection final : public connection,
	public std::enable_shared_from_this<byte_stream_connection<Exec>>
{
public:
	byte_stream_connection(basic_byte_stream<Exec> stream,
		std::vector<std::byte> initial = {}) :
		m_metadata(stream.metadata()), m_stream(std::move(stream)),
		m_initial(std::move(initial))
	{
	}

	[[nodiscard]] executor_type get_executor() const noexcept override {
		return asio::any_io_executor(m_stream.get_executor());
	}

	void read_some(mutable_buffer buffer, io_handler handler) override
	{
		if( m_initial_offset == m_initial.size() )
			return m_stream.read_some(buffer, std::move(handler));
		const auto count = copy_initial(buffer);
		asio::post(get_executor(),
			[handler = std::move(handler), count]() mutable {
				std::move(handler)(error_code{}, count);
			});
	}

	void write(const_buffer buffer, io_handler handler) override {
		m_stream.write(buffer, std::move(handler));
	}
	void close(close_handler handler) override {
		m_stream.close(std::move(handler));
	}
	void cancel() noexcept override { m_stream.cancel(); }
	[[nodiscard]] bool is_open() const noexcept override {
		return m_stream.is_open();
	}
	[[nodiscard]] size_t read_some_sync(mutable_buffer buffer,
		error_code &error) noexcept override
	{
		error.clear();
		if( m_initial_offset != m_initial.size() ) return copy_initial(buffer);
		return m_stream.read_some_sync(buffer, error);
	}
	[[nodiscard]] size_t write_sync(const_buffer buffer,
		error_code &error) noexcept override {
		return m_stream.write_sync(buffer, error);
	}

	[[nodiscard]] endpoint_type remote_endpoint(
		error_code &error) const noexcept override {
		return parse_endpoint(m_metadata.remote_endpoint, error);
	}
	[[nodiscard]] endpoint_type local_endpoint(
		error_code &error) const noexcept override {
		return parse_endpoint(m_metadata.local_endpoint, error);
	}

private:
	[[nodiscard]] size_t copy_initial(mutable_buffer buffer) noexcept
	{
		const auto count = std::min(buffer.size(),
			m_initial.size() - m_initial_offset);
		if( count != 0 ) std::memcpy(buffer.data(),
			m_initial.data() + m_initial_offset, count);
		m_initial_offset += count;
		return count;
	}

	[[nodiscard]] static endpoint_type parse_endpoint(
		const optional<std::string> &text, error_code &error) noexcept
	{
		error.clear();
		if( not text )
		{
			error = make_error_code(errc::operation_not_supported);
			return {};
		}
		try
		{
			std::string_view value(*text);
			std::string_view address = value;
			std::string_view port;
			if( value.starts_with('[') )
			{
				const auto end = value.find(']');
				if( end == std::string_view::npos or end + 1 >= value.size() or
					value[end + 1] != ':' ) throw std::invalid_argument("endpoint");
				address = value.substr(1, end - 1);
				port = value.substr(end + 2);
			}
			else
			{
				const auto colon = value.rfind(':');
				if( colon == std::string_view::npos )
					throw std::invalid_argument("endpoint");
				address = value.substr(0, colon);
				port = value.substr(colon + 1);
			}
			uint16_t port_value = 0;
			const auto parsed = std::from_chars(port.data(),
				port.data() + port.size(), port_value);
			if( parsed.ec != std::errc{} or parsed.ptr != port.data() + port.size() )
				throw std::invalid_argument("endpoint");
			auto ip = asio::ip::make_address(address, error);
			if( error ) return {};
			return {ip, port_value};
		}
		catch(...) {
			error = make_error_code(errc::operation_not_supported);
			return {};
		}
	}

	transport_metadata m_metadata;
	basic_byte_stream<Exec> m_stream;
	std::vector<std::byte> m_initial;
	size_t m_initial_offset = 0;
};

template<core_concepts::exec Exec>
[[nodiscard]] std::shared_ptr<connection> make_connection(
	basic_negotiated_transport<Exec> transport)
{
	return std::make_shared<byte_stream_connection<Exec>>(
		std::move(transport.stream), std::move(transport.initial_input));
}

} //namespace riwo::http_exp::detail

#endif //RIWO_HTTP_EXP_TRANSPORT_DETAIL_BYTE_STREAM_CONNECTION_H
