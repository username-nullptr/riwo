// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_TRANSPORT_DETAIL_BYTE_STREAM_H
#define RIWO_HTTP_EXP_TRANSPORT_DETAIL_BYTE_STREAM_H

namespace riwo::http_exp
{

template<core_concepts::exec Exec>
class basic_byte_stream<Exec>::interface
{
public:
	virtual ~interface() = default;
	[[nodiscard]] virtual executor_t get_executor() const noexcept = 0;
	virtual void read_some(mutable_buffer, io_handler_t) = 0;
	virtual void write(const_buffer, io_handler_t) = 0;
	virtual void close(close_handler_t) = 0;
	virtual void cancel() noexcept = 0;
	[[nodiscard]] virtual bool is_open() const noexcept = 0;
	[[nodiscard]] virtual size_t read_some_sync(
		mutable_buffer, error_code &) noexcept = 0;
	[[nodiscard]] virtual size_t write_sync(
		const_buffer, error_code &) noexcept = 0;
	[[nodiscard]] virtual transport_capabilities capabilities()
		const noexcept = 0;
	[[nodiscard]] virtual transport_metadata metadata() const = 0;
};

template<core_concepts::exec Exec>
template<concepts::byte_stream_model Stream>
class basic_byte_stream<Exec>::model final : public basic_byte_stream<Exec>::interface
{
public:
	explicit model(Stream stream) : m_stream(std::move(stream)) {}

	[[nodiscard]] executor_t get_executor() const noexcept override {
		return executor_t(m_stream.get_executor());
	}
	void read_some(mutable_buffer buffer, io_handler_t handler) override {
		m_stream.read_some(buffer, std::move(handler));
	}
	void write(const_buffer buffer, io_handler_t handler) override {
		m_stream.write(buffer, std::move(handler));
	}
	void close(close_handler_t handler) override {
		m_stream.close(std::move(handler));
	}
	void cancel() noexcept override { m_stream.cancel(); }
	[[nodiscard]] bool is_open() const noexcept override {
		return m_stream.is_open();
	}
	[[nodiscard]] size_t read_some_sync(mutable_buffer buffer,
		error_code &error) noexcept override
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
	[[nodiscard]] size_t write_sync(const_buffer buffer,
		error_code &error) noexcept override
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
	[[nodiscard]] transport_capabilities capabilities()
		const noexcept override
	{
		if constexpr( requires(const Stream &stream) {
			{ stream.capabilities() } ->
				std::same_as<transport_capabilities>;
		})
			return m_stream.capabilities();
		else
			return {.synchronous = requires(Stream &stream,
			mutable_buffer read_buffer, const_buffer write_buffer,
			error_code &error) {
				{ stream.read_some_sync(read_buffer, error) } ->
					std::same_as<size_t>;
				{ stream.write_sync(write_buffer, error) } ->
					std::same_as<size_t>;
			}};
	}
	[[nodiscard]] transport_metadata metadata() const override
	{
		if constexpr( requires(const Stream &stream) {
			{ stream.metadata() } -> std::same_as<transport_metadata>;
		})
			return m_stream.metadata();
		else
			return {};
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
	m_executor(stream.get_executor()),
	m_impl(std::make_unique<model<Stream>>(std::move(stream)))
{
}

template<core_concepts::exec Exec>
basic_byte_stream<Exec>::basic_byte_stream(basic_byte_stream &&other) noexcept :
	m_executor(other.m_executor), m_impl(std::move(other.m_impl))
{
}

template<core_concepts::exec Exec>
basic_byte_stream<Exec> &basic_byte_stream<Exec>::operator=(
	basic_byte_stream &&other) noexcept
{
	if( this == &other )
		return *this;
	m_executor = other.m_executor;
	m_impl = std::move(other.m_impl);
	return *this;
}

template<core_concepts::exec Exec>
basic_byte_stream<Exec>::~basic_byte_stream() = default;

template<core_concepts::exec Exec>
auto basic_byte_stream<Exec>::get_executor() const noexcept -> executor_t
{
	if( m_executor )
		return *m_executor;
	std::terminate();
}

template<core_concepts::exec Exec>
void basic_byte_stream<Exec>::read_some(
	mutable_buffer buffer, io_handler_t handler)
{
	if( not m_impl )
		return std::move(handler)(
		make_error_code(errc::operation_not_permitted), 0);
	m_impl->read_some(buffer, std::move(handler));
}

template<core_concepts::exec Exec>
void basic_byte_stream<Exec>::write(const_buffer buffer, io_handler_t handler)
{
	if( not m_impl )
		return std::move(handler)(
		make_error_code(errc::operation_not_permitted), 0);
	m_impl->write(buffer, std::move(handler));
}

template<core_concepts::exec Exec>
void basic_byte_stream<Exec>::close(close_handler_t handler)
{
	if( not m_impl )
		return std::move(handler)(error_code{});
	m_impl->close(std::move(handler));
}

template<core_concepts::exec Exec>
void basic_byte_stream<Exec>::cancel() noexcept {
	if( m_impl )
		m_impl->cancel();
}

template<core_concepts::exec Exec>
bool basic_byte_stream<Exec>::is_open() const noexcept {
	return m_impl and m_impl->is_open();
}

template<core_concepts::exec Exec>
size_t basic_byte_stream<Exec>::read_some_sync(
	mutable_buffer buffer, error_code &error) noexcept
{
	if( m_impl )
		return m_impl->read_some_sync(buffer, error);
	error = make_error_code(errc::operation_not_permitted);
	return 0;
}

template<core_concepts::exec Exec>
size_t basic_byte_stream<Exec>::write_sync(
	const_buffer buffer, error_code &error) noexcept
{
	if( m_impl )
		return m_impl->write_sync(buffer, error);
	error = make_error_code(errc::operation_not_permitted);
	return 0;
}

template<core_concepts::exec Exec>
transport_capabilities basic_byte_stream<Exec>::capabilities() const noexcept {
	return m_impl ? m_impl->capabilities() : transport_capabilities{};
}

template<core_concepts::exec Exec>
transport_metadata basic_byte_stream<Exec>::metadata() const {
	return m_impl ? m_impl->metadata() : transport_metadata{};
}

template<concepts::byte_stream_model Stream>
auto make_byte_stream(Stream stream) ->
	basic_byte_stream<typename Stream::executor_type>
{
	return basic_byte_stream<typename Stream::executor_type>(std::move(stream));
}

} //namespace riwo::http_exp

#endif //RIWO_HTTP_EXP_TRANSPORT_DETAIL_BYTE_STREAM_H
