// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "detail/session.h"

namespace riwo::http_exp::detail
{

namespace
{
[[nodiscard]] error_code unsupported() noexcept {
	return make_error_code(errc::operation_not_supported);
}
} //namespace

stream_capabilities stream::capabilities() const noexcept {
	return {};
}

request_head stream::request() const {
	return {};
}

std::shared_ptr<body_reader_state> stream::inbound_body() const noexcept {
	return make_body_reader_state();
}

std::shared_ptr<body_writer_state> stream::outbound_body() const noexcept {
	return make_body_writer_state();
}

response_head stream::wait_response_sync(error_code &error) noexcept
{
	error = unsupported();
	return {};
}

void stream::wait_response(response_handler handler) {
	std::move(handler)(unsupported(), response_head{});
}

auto stream::remote_endpoint(error_code &error) const noexcept -> endpoint_type
{
	error = unsupported();
	return {};
}

auto stream::local_endpoint(error_code &error) const noexcept -> endpoint_type
{
	error = unsupported();
	return {};
}

void stream::respond_sync(response_head, body_spec, error_code &error) noexcept {
	error = unsupported();
}

void stream::respond(response_head, body_spec, operation_handler handler) {
	std::move(handler)(unsupported());
}

void stream::inform_sync(response_head, error_code &error) noexcept {
	error = unsupported();
}

void stream::inform(response_head, operation_handler handler) {
	std::move(handler)(unsupported());
}

std::shared_ptr<duplex_state> stream::open_duplex_sync(
	optional<response_head>, error_code &error) noexcept
{
	error = unsupported();
	return {};
}

void stream::open_duplex(optional<response_head>, duplex_handler handler) {
	std::move(handler)(unsupported(), std::shared_ptr<duplex_state>{});
}

namespace
{

class body_duplex_state final : public duplex_state,
	public std::enable_shared_from_this<body_duplex_state>
{
public:
	body_duplex_state(asio::any_io_executor executor, protocol version,
		std::shared_ptr<body_reader_state> reader,
		std::shared_ptr<body_writer_state> writer,
		endpoint_type remote, endpoint_type local,
		std::shared_ptr<void> owner, std::function<void()> cancel) :
		executor_(std::move(executor)), version_(version),
		reader_(std::move(reader)), writer_(std::move(writer)),
		remote_(remote), local_(local), owner_(std::move(owner)),
		cancel_(std::move(cancel))
	{
	}

	[[nodiscard]] asio::any_io_executor get_executor() const noexcept override {
		return executor_;
	}
	[[nodiscard]] protocol selected_protocol() const noexcept override {
		return version_;
	}
	[[nodiscard]] bool is_open() const noexcept override {
		return not cancelled_ and not closed_;
	}
	[[nodiscard]] size_t read_some_sync(mutable_buffer buffer,
		error_code &error) noexcept override
	{
		auto result = reader_->read_some(buffer, error);
		if( not error and result.bytes == 0 and result.eof )
			error = asio::error::make_error_code(asio::error::eof);
		return result.bytes;
	}
	void read_some(mutable_buffer buffer, io_handler handler) override
	{
		reader_->read_some(buffer,
		[handler = std::move(handler)]
		(error_code error, body_read_result result) mutable {
			if( not error and result.bytes == 0 and result.eof )
				error = asio::error::make_error_code(asio::error::eof);
			std::move(handler)(error, result.bytes);
		});
	}
	[[nodiscard]] size_t write_sync(std::span<const const_buffer> buffers,
		error_code &error) noexcept override
	{
		error.clear();
		size_t total = 0;
		for( const auto &buffer : buffers )
		{
			auto size = writer_->write(buffer, error);
			total += size;
			if( error or size != buffer.size() ) break;
		}
		return total;
	}
	void write(std::span<const const_buffer> buffers, io_handler handler) override
	{
		size_t total = 0;
		for( const auto &buffer : buffers ) total += buffer.size();
		auto bytes = std::make_shared<std::vector<std::byte>>(total);
		size_t offset = 0;
		for( const auto &buffer : buffers )
		{
			if( buffer.size() != 0 )
				std::memcpy(bytes->data() + offset, buffer.data(), buffer.size());
			offset += buffer.size();
		}
		writer_->write(riwo::buffer(*bytes),
		[bytes, handler = std::move(handler)]
		(error_code error, size_t size) mutable {
			std::move(handler)(error, size);
		});
	}
	void close_sync(error_code &error) noexcept override
	{
		if( closed_ ) { error.clear(); return; }
		writer_->finish({}, error);
		if( not error ) closed_ = true;
	}
	void close(close_handler handler) override
	{
		if( closed_ )
		{
			std::move(handler)(error_code{});
			return;
		}
		auto self = shared_from_this();
		writer_->finish({},
		[self = std::move(self), owner = owner_, handler = std::move(handler)]
		(error_code error) mutable {
			ignore_unused(owner);
			if( not error ) self->closed_ = true;
			std::move(handler)(error);
		});
	}
	void cancel() noexcept override
	{
		if( cancelled_ ) return;
		cancelled_ = true;
		if( cancel_ ) cancel_();
	}
	[[nodiscard]] endpoint_type remote_endpoint(
		error_code &error) const noexcept override { error.clear(); return remote_; }
	[[nodiscard]] endpoint_type local_endpoint(
		error_code &error) const noexcept override { error.clear(); return local_; }

private:
	asio::any_io_executor executor_;
	protocol version_;
	std::shared_ptr<body_reader_state> reader_;
	std::shared_ptr<body_writer_state> writer_;
	endpoint_type remote_;
	endpoint_type local_;
	std::shared_ptr<void> owner_;
	std::function<void()> cancel_;
	bool cancelled_ = false;
	bool closed_ = false;
};

} //namespace

std::shared_ptr<duplex_state> make_body_duplex_state(
	asio::any_io_executor executor, protocol version,
	std::shared_ptr<body_reader_state> reader,
	std::shared_ptr<body_writer_state> writer,
	asio::ip::tcp::endpoint remote, asio::ip::tcp::endpoint local,
	std::shared_ptr<void> owner, std::function<void()> cancel)
{
	return std::make_shared<body_duplex_state>(std::move(executor), version,
		std::move(reader), std::move(writer), remote, local,
		std::move(owner), std::move(cancel));
}

std::shared_ptr<stream> client_session::open_sync(request_head, body_spec,
	error_code &error) noexcept
{
	error = unsupported();
	return {};
}

} //namespace riwo::http_exp::detail
