// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_TRANSPORT_BYTE_STREAM_H
#define RIWO_HTTP_EXP_TRANSPORT_BYTE_STREAM_H

#include <riwo/http_exp/transport/connection.h>
#include <riwo/http_exp/transport/metadata.h>

namespace riwo::http_exp { namespace detail {
struct byte_stream_access;
} //namespace detail

namespace concepts
{

template<class Stream>
concept byte_stream_model = requires(Stream &stream, mutable_buffer input, const_buffer output,
	asio::any_completion_handler<void(error_code,size_t)> io_handler_t,
	asio::any_completion_handler<void(error_code)> close_handler_t)
{
	typename Stream::executor_type;
	{ stream.get_executor() } -> std::convertible_to<typename Stream::executor_type>;

	stream.read_some(input, std::move(io_handler_t));
	stream.write(output, asio::any_completion_handler<void(error_code,size_t)>{});
	stream.close(std::move(close_handler_t));

	{ stream.cancel() } noexcept;
	{ stream.is_open() } noexcept -> std::same_as<bool>;
};

} //namespace concepts

template<core_concepts::exec Exec = asio::any_io_executor>
class RIWO_HTTP_EXP_TAPI basic_byte_stream
{
	RIWO_DISABLE_COPY(basic_byte_stream)

public:
	using executor_type = Exec;
	using executor_t = executor_type;

	using io_handler_t = asio::any_completion_handler<void(error_code,size_t)>;
	using close_handler_t = asio::any_completion_handler<void(error_code)>;

public:
	template<concepts::byte_stream_model Stream>
	explicit basic_byte_stream(Stream stream)
		requires std::convertible_to<typename Stream::executor_type,executor_t>;

	basic_byte_stream(basic_byte_stream &&other) noexcept;
	basic_byte_stream &operator=(basic_byte_stream &&other) noexcept;

	basic_byte_stream() noexcept;
	~basic_byte_stream();

public:
	void read_some(mutable_buffer buffer, io_handler_t handler);
	void write(const_buffer buffer, io_handler_t handler);

	[[nodiscard]] size_t read_some_sync(mutable_buffer buffer, error_code &error) noexcept;
	[[nodiscard]] size_t write_sync(const_buffer buffer, error_code &error) noexcept;

	void close(close_handler_t handler);
	void cancel() noexcept;

	[[nodiscard]] transport_capabilities capabilities() const noexcept;
	[[nodiscard]] transport_metadata metadata() const;

	[[nodiscard]] bool is_open() const noexcept;
	[[nodiscard]] executor_t get_executor() const noexcept;

private:
	template<concepts::byte_stream_model Stream>
	class model;

	class interface;
	friend struct detail::byte_stream_access;

	[[nodiscard]] std::shared_ptr<connection> release_connection (
		std::vector<std::byte> initial_input, transport_metadata metadata
	) &&;

	std::shared_ptr<interface> m_interface {};
	optional<executor_t> m_exec {};
};

using byte_stream = basic_byte_stream<>;

template<concepts::byte_stream_model Stream>
[[nodiscard]] RIWO_HTTP_EXP_TAPI
basic_byte_stream<typename Stream::executor_type> make_byte_stream(Stream stream);

} //namespace riwo::http_exp
#include <riwo/http_exp/transport/detail/byte_stream.h>


#endif //RIWO_HTTP_EXP_TRANSPORT_BYTE_STREAM_H
