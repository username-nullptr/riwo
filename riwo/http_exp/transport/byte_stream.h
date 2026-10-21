// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_TRANSPORT_BYTE_STREAM_H
#define RIWO_HTTP_EXP_TRANSPORT_BYTE_STREAM_H

#include <riwo/http_exp/error.h>
#include <riwo/http_exp/transport/metadata.h>

namespace riwo::http_exp
{

template<class Stream>
concept byte_stream_model = requires(Stream &stream, mutable_buffer input,
	const_buffer output,
	asio::any_completion_handler<void(error_code,size_t)> io_handler,
	asio::any_completion_handler<void(error_code)> close_handler)
{
	typename Stream::executor_type;
	{ stream.get_executor() } ->
		std::convertible_to<typename Stream::executor_type>;
	stream.read_some(input, std::move(io_handler));
	stream.write(output, asio::any_completion_handler<void(error_code,size_t)>{});
	stream.close(std::move(close_handler));
	{ stream.cancel() } noexcept;
	{ stream.is_open() } noexcept -> std::same_as<bool>;
};

template<core_concepts::exec Exec = asio::any_io_executor>
class RIWO_HTTP_EXP_TAPI basic_byte_stream
{
	RIWO_DISABLE_COPY(basic_byte_stream)

public:
	using executor_type = Exec;
	using io_handler = asio::any_completion_handler<void(error_code,size_t)>;
	using close_handler = asio::any_completion_handler<void(error_code)>;

	basic_byte_stream() noexcept = default;

	template<byte_stream_model Stream>
	explicit basic_byte_stream(Stream stream)
		requires std::convertible_to<typename Stream::executor_type,executor_type> :
		m_impl(std::make_unique<model<Stream>>(std::move(stream)))
	{
	}

	basic_byte_stream(basic_byte_stream &&) noexcept = default;
	basic_byte_stream &operator=(basic_byte_stream &&) noexcept = default;
	~basic_byte_stream() = default;

	[[nodiscard]] executor_type get_executor() const noexcept {
		if( m_impl ) return m_impl->get_executor();
		std::terminate();
	}

	void read_some(mutable_buffer buffer, io_handler handler) {
		if( not m_impl ) return std::move(handler)(
			make_error_code(errc::operation_not_permitted), 0);
		m_impl->read_some(buffer, std::move(handler));
	}

	void write(const_buffer buffer, io_handler handler) {
		if( not m_impl ) return std::move(handler)(
			make_error_code(errc::operation_not_permitted), 0);
		m_impl->write(buffer, std::move(handler));
	}

	void close(close_handler handler) {
		if( not m_impl ) return std::move(handler)(error_code{});
		m_impl->close(std::move(handler));
	}

	void cancel() noexcept { if( m_impl ) m_impl->cancel(); }
	[[nodiscard]] bool is_open() const noexcept {
		return m_impl and m_impl->is_open();
	}

	[[nodiscard]] size_t read_some_sync(mutable_buffer buffer,
		error_code &error) noexcept
	{
		if( m_impl ) return m_impl->read_some_sync(buffer, error);
		error = make_error_code(errc::operation_not_permitted);
		return 0;
	}

	[[nodiscard]] size_t write_sync(const_buffer buffer,
		error_code &error) noexcept
	{
		if( m_impl ) return m_impl->write_sync(buffer, error);
		error = make_error_code(errc::operation_not_permitted);
		return 0;
	}

	[[nodiscard]] transport_capabilities capabilities() const noexcept {
		return m_impl ? m_impl->capabilities() : transport_capabilities{};
	}

	[[nodiscard]] transport_metadata metadata() const {
		return m_impl ? m_impl->metadata() : transport_metadata{};
	}

private:
	class interface
	{
	public:
		virtual ~interface() = default;
		[[nodiscard]] virtual executor_type get_executor() const noexcept = 0;
		virtual void read_some(mutable_buffer, io_handler) = 0;
		virtual void write(const_buffer, io_handler) = 0;
		virtual void close(close_handler) = 0;
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

	template<byte_stream_model Stream>
	class model final : public interface
	{
	public:
		explicit model(Stream stream) : m_stream(std::move(stream)) {}

		[[nodiscard]] executor_type get_executor() const noexcept override {
			return executor_type(m_stream.get_executor());
		}
		void read_some(mutable_buffer buffer, io_handler handler) override {
			m_stream.read_some(buffer, std::move(handler));
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
			if constexpr( requires(Stream &stream) {
				{ stream.read_some_sync(buffer, error) } -> std::same_as<size_t>;
			}) return m_stream.read_some_sync(buffer, error);
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
			}) return m_stream.write_sync(buffer, error);
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
			}) return m_stream.capabilities();
			else return {.synchronous = requires(Stream &stream,
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
			}) return m_stream.metadata();
			else return {};
		}

	private:
		Stream m_stream;
	};

	std::unique_ptr<interface> m_impl;
};

using byte_stream = basic_byte_stream<>;

template<byte_stream_model Stream>
[[nodiscard]] auto make_byte_stream(Stream stream) ->
	basic_byte_stream<typename Stream::executor_type>
{
	return basic_byte_stream<typename Stream::executor_type>(std::move(stream));
}

} //namespace riwo::http_exp

#endif //RIWO_HTTP_EXP_TRANSPORT_BYTE_STREAM_H
