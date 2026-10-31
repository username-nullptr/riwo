// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_EXP_HTTP_TRANSPORT_DETAIL_BYTE_STREAM_H
#define RIWO_EXP_HTTP_TRANSPORT_DETAIL_BYTE_STREAM_H

#include <riwo/experimental/http/error.h>
#include <charconv>

namespace riwo::experimental::http
{

template<core_concepts::exec Exec>
class RIWO_EXP_HTTP_TAPI basic_byte_stream<Exec>::interface : public connection
{
	RIWO_DISABLE_COPY_MOVE(interface)

public:
	interface() = default;
	~interface() override = default;

public:
	[[nodiscard]] virtual transport_capabilities capabilities() const noexcept = 0;

	[[nodiscard]] transport_metadata metadata() const
	{
		auto result = metadata_impl();
		if( m_metadata.local_endpoint )
			result.local_endpoint = m_metadata.local_endpoint;

		if( m_metadata.remote_endpoint )
			result.remote_endpoint = m_metadata.remote_endpoint;

		result.tls = result.tls or m_metadata.tls;
		if( m_metadata.alpn )
			result.alpn = m_metadata.alpn;

		for(const auto &[name, value] : m_metadata.attributes)
			result.attributes.insert_or_assign(name, value);
		return result;
	}

	void read_some(mutable_buffer buffer, io_handler_t handler) final
	{
		if( m_initial_offset == m_initial_input.size() )
			return read_some_impl(buffer, std::move(handler));

		const auto count = copy_initial(buffer);
		asio::post(get_executor(),
		[handler = std::move(handler), count]() mutable {
			std::move(handler)(error_code{}, count);
		});
	}

	[[nodiscard]] size_t read_some_sync(const mutable_buffer &buffer, error_code &error) noexcept final
	{
		error.clear();
		if( m_initial_offset != m_initial_input.size() )
			return copy_initial(buffer);
		return read_some_sync_impl(buffer, error);
	}

	[[nodiscard]] endpoint_t remote_endpoint(error_code &error) const noexcept final
	{
		try {
			return parse_endpoint(metadata().remote_endpoint, error);
		}
		catch(...) {
			error = exception_error(std::current_exception());
		}
		return {};
	}

	[[nodiscard]] endpoint_t local_endpoint(error_code &error) const noexcept final
	{
		try {
			return parse_endpoint(metadata().local_endpoint, error);
		}
		catch(...) {
			error = exception_error(std::current_exception());
		}
		return {};
	}

	void prepend_input(std::vector<std::byte> input)
	{
		if( input.empty() )
			return ;

		if( m_initial_offset == m_initial_input.size() )
		{
			m_initial_input = std::move(input);
			m_initial_offset = 0;
			return ;
		}
		std::vector<std::byte> combined {};
		combined.reserve(input.size() + m_initial_input.size() - m_initial_offset);
		combined.insert(combined.end(), input.begin(), input.end());

		combined.insert(combined.end(),
			m_initial_input.begin() + static_cast<std::ptrdiff_t>(m_initial_offset),
			m_initial_input.end()
		);
		m_initial_input = std::move(combined);
		m_initial_offset = 0;
	}

	void merge_metadata(transport_metadata metadata) {
		m_metadata = std::move(metadata);
	}

protected:
	[[nodiscard]] virtual transport_metadata metadata_impl() const = 0;

	virtual void read_some_impl(mutable_buffer, io_handler_t) = 0;
	[[nodiscard]] virtual size_t read_some_sync_impl(mutable_buffer, error_code &) noexcept = 0;

private:
	[[nodiscard]] size_t copy_initial(const mutable_buffer &buffer) noexcept
	{
		const auto count = std::min(buffer.size(),
			m_initial_input.size() - m_initial_offset
		);
		if( count != 0 )
		{
			std::memcpy(buffer.data(),
				m_initial_input.data() + m_initial_offset, count
			);
		}
		m_initial_offset += count;
		return count;
	}

	[[nodiscard]] static endpoint_t parse_endpoint
	(const optional<std::string> &text, error_code &error) noexcept
	{
		error.clear();
		if( not text )
		{
			error = make_error_code(errc::operation_not_supported);
			return {};
		}
		std::string_view value(*text);
		std::string_view address = value;
		std::string_view port;

		if( value.starts_with('[') )
		{
			const auto end = value.find(']');
			if( end == std::string_view::npos or end + 1 >= value.size() or
				value[end + 1] != ':' )
			{
				error = make_error_code(errc::operation_not_supported);
				return {};
			}
			address = value.substr(1, end - 1);
			port = value.substr(end + 2);
		}
		else
		{
			const auto colon = value.rfind(':');
			if( colon == std::string_view::npos )
			{
				error = make_error_code(errc::operation_not_supported);
				return {};
			}
			address = value.substr(0, colon);
			port = value.substr(colon + 1);
		}
		uint16_t port_value = 0;
		const auto parsed = std::from_chars (
			port.data(), port.data() + port.size(), port_value
		);
		if( parsed.ec != std::errc{} or parsed.ptr != port.data() + port.size() )
		{
			error = make_error_code(errc::operation_not_supported);
			return {};
		}
		auto ip = asio::ip::make_address(address, error);
		if( error )
			return {};

		return {ip, port_value};
	}

	std::vector<std::byte> m_initial_input {};
	transport_metadata m_metadata {};
	size_t m_initial_offset = 0;
};

template<core_concepts::exec Exec>
template<concepts::byte_stream_model Stream>
class RIWO_EXP_HTTP_TAPI basic_byte_stream<Exec>::model final : public interface
{
	RIWO_DISABLE_COPY_MOVE(model)

public:
	explicit model(Stream stream) :
		m_stream(std::move(stream)) {}

	void write(const_buffer buffer, connection::io_handler_t handler) override {
		m_stream.write(buffer, std::move(handler));
	}
	using connection::write;

	[[nodiscard]] size_t write_sync(const const_buffer &buffer, error_code &error) noexcept override
	{
		if constexpr( requires(Stream &stream) {
			{ stream.write_sync(buffer, error) } -> std::same_as<size_t>;
		})
			return m_stream.write_sync(buffer, error);
		else
		{
			error = make_error_code(errc::operation_not_supported);
			return 0;
		}
	}

	void close(connection::close_handler_t handler) override {
		m_stream.close(std::move(handler));
	}
	void cancel() noexcept override {
		m_stream.cancel();
	}

public:
	[[nodiscard]] transport_capabilities capabilities() const noexcept override
	{
		if constexpr( requires(const Stream &stream) {
			{ stream.capabilities() } -> std::same_as<transport_capabilities>;
		})
			return m_stream.capabilities();
		else
		{
			return {
				.synchronous = requires
				(Stream &stream, mutable_buffer read_buffer, const_buffer write_buffer, error_code &error)
				{
					{ stream.read_some_sync(read_buffer, error) } -> std::same_as<size_t>;
					{ stream.write_sync(write_buffer, error) } -> std::same_as<size_t>;
				}
			};
		}
	}

	[[nodiscard]] bool is_open() const noexcept override {
		return m_stream.is_open();
	}
	[[nodiscard]] connection::executor_t get_executor() const noexcept override {
		return connection::executor_t(m_stream.get_executor());
	}

protected:
	[[nodiscard]] transport_metadata metadata_impl() const override
	{
		if constexpr( requires(const Stream &stream) {
			{ stream.metadata() } -> std::same_as<transport_metadata>;
		})
			return m_stream.metadata();
		else
			return {};
	}

	void read_some_impl(mutable_buffer buffer, connection::io_handler_t handler) override {
		m_stream.read_some(buffer, std::move(handler));
	}

	[[nodiscard]] size_t read_some_sync_impl
	(mutable_buffer buffer, error_code &error) noexcept override
	{
		if constexpr( requires(Stream &stream) {
			{ stream.read_some_sync(buffer, error) } -> std::same_as<size_t>;
		})
			return m_stream.read_some_sync(buffer, error);
		else
		{
			error = make_error_code(errc::operation_not_supported);
			return 0;
		}
	}

private:
	Stream m_stream;
};

template<core_concepts::exec Exec>
basic_byte_stream<Exec>::basic_byte_stream() noexcept = default;

template<core_concepts::exec Exec>
template<concepts::byte_stream_model Stream>
basic_byte_stream<Exec>::basic_byte_stream(Stream stream)
	requires std::convertible_to<typename Stream::executor_type,executor_t> :
	m_interface(std::make_shared<model<Stream>>(std::move(stream))),
	m_exec(stream.get_executor())
{
}

template<core_concepts::exec Exec>
basic_byte_stream<Exec>::basic_byte_stream(basic_byte_stream &&other) noexcept :
	m_interface(std::move(other.m_interface)), m_exec(other.m_exec)
{

}

template<core_concepts::exec Exec>
basic_byte_stream<Exec> &basic_byte_stream<Exec>::operator=(basic_byte_stream &&other) noexcept
{
	if( this == &other )
		return *this;
	m_exec = other.m_exec;
	m_interface = std::move(other.m_interface);
	return *this;
}

template<core_concepts::exec Exec>
basic_byte_stream<Exec>::~basic_byte_stream() = default;

template<core_concepts::exec Exec>
auto basic_byte_stream<Exec>::get_executor() const noexcept -> executor_t
{
	if( m_exec )
		return *m_exec;
	std::terminate();
}

template<core_concepts::exec Exec>
void basic_byte_stream<Exec>::read_some(mutable_buffer buffer, io_handler_t handler)
{
	if( not m_interface )
	{
		return std::move(handler) (
			make_error_code(errc::operation_not_permitted), 0
		);
	}
	m_interface->read_some(buffer, std::move(handler));
}

template<core_concepts::exec Exec>
void basic_byte_stream<Exec>::write(const_buffer buffer, io_handler_t handler)
{
	if( not m_interface )
	{
		return std::move(handler) (
			make_error_code(errc::operation_not_permitted), 0
		);
	}
	m_interface->write(buffer, std::move(handler));
}

template<core_concepts::exec Exec>
void basic_byte_stream<Exec>::close(close_handler_t handler)
{
	if( not m_interface )
		return std::move(handler)(error_code{});
	m_interface->close(std::move(handler));
}

template<core_concepts::exec Exec>
void basic_byte_stream<Exec>::cancel() noexcept
{
	if( m_interface )
		m_interface->cancel();
}

template<core_concepts::exec Exec>
bool basic_byte_stream<Exec>::is_open() const noexcept
{
	return m_interface and m_interface->is_open();
}

template<core_concepts::exec Exec>
size_t basic_byte_stream<Exec>::read_some_sync(mutable_buffer buffer, error_code &error) noexcept
{
	if( m_interface )
		return m_interface->read_some_sync(buffer, error);
	error = make_error_code(errc::operation_not_permitted);
	return 0;
}

template<core_concepts::exec Exec>
size_t basic_byte_stream<Exec>::write_sync(const_buffer buffer, error_code &error) noexcept
{
	if( m_interface )
		return m_interface->write_sync(buffer, error);
	error = make_error_code(errc::operation_not_permitted);
	return 0;
}

template<core_concepts::exec Exec>
transport_capabilities basic_byte_stream<Exec>::capabilities() const noexcept
{
	return m_interface ? m_interface->capabilities() : transport_capabilities{};
}

template<core_concepts::exec Exec>
transport_metadata basic_byte_stream<Exec>::metadata() const
{
	return m_interface ? m_interface->metadata() : transport_metadata{};
}

template<core_concepts::exec Exec>
std::shared_ptr<connection> basic_byte_stream<Exec>::release_connection
(std::vector<std::byte> initial_input, transport_metadata metadata) &&
{
	if( not m_interface )
		return {};

	m_interface->prepend_input(std::move(initial_input));
	m_interface->merge_metadata(std::move(metadata));
	return std::move(m_interface);
}

template<concepts::byte_stream_model Stream>
basic_byte_stream<typename Stream::executor_type> make_byte_stream(Stream stream)
{
	return basic_byte_stream<typename Stream::executor_type>(std::move(stream));
}

} //namespace riwo::experimental::http


#endif //RIWO_EXP_HTTP_TRANSPORT_DETAIL_BYTE_STREAM_H
