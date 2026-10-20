// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "session.h"
#include <riwo/http_exp/protocol/detail/h2/session.h>
#include <riwo/http_exp/transport/preface.h>
#include <riwo/http_exp/transport/tcp_connection.h>

namespace riwo::http_exp::detail { namespace
{

[[nodiscard]] bool contains(const protocol_policy &policy,
	protocol value) noexcept
{
	return std::ranges::find(policy.preference, value) !=
		policy.preference.end();
}

[[nodiscard]] bool permits_http1(const protocol_policy &policy) noexcept
{
#if RIWO_HTTP_EXP_HTTP2_SUPPORT
	if( policy.requirement == protocol_requirement::require_http2 ) return false;
#endif
	return contains(policy, protocol::v1_0) or
		contains(policy, protocol::v1_1);
}

#if RIWO_HTTP_EXP_HTTP2_SUPPORT
[[nodiscard]] bool permits_http2(const protocol_policy &policy) noexcept
{
	if( policy.requirement == protocol_requirement::require_http1 ) return false;
	return contains(policy, protocol::v2);
}
#endif

class replay_connection final : public ::riwo::http_exp::connection,
	public std::enable_shared_from_this<replay_connection>
{
public:
	replay_connection(std::shared_ptr<::riwo::http_exp::connection> next,
		std::string prefix) : next_(std::move(next)), prefix_(std::move(prefix)) {}

	[[nodiscard]] executor_type get_executor() const noexcept override {
		return next_->get_executor();
	}

	void read_some(mutable_buffer buffer, io_handler handler) override
	{
		if( offset_ == prefix_.size() )
			return next_->read_some(buffer, std::move(handler));
		const auto count = copy_prefix(buffer);
		asio::post(get_executor(),
		[handler = std::move(handler), count]() mutable {
			std::move(handler)(error_code{}, count);
		});
	}

	[[nodiscard]] size_t read_some_sync(mutable_buffer buffer,
		error_code &error) noexcept override
	{
		error.clear();
		if( offset_ != prefix_.size() ) return copy_prefix(buffer);
		return next_->read_some_sync(buffer, error);
	}

	void write(const_buffer buffer, io_handler handler) override {
		next_->write(buffer, std::move(handler));
	}
	[[nodiscard]] size_t write_sync(const_buffer buffer,
		error_code &error) noexcept override {
		return next_->write_sync(buffer, error);
	}
	void close(close_handler handler) override {
		next_->close(std::move(handler));
	}
	void cancel() noexcept override { next_->cancel(); }
	void set_read_timeout(optional<timeout_type> value) override {
		next_->set_read_timeout(value);
	}
	void set_write_timeout(optional<timeout_type> value) override {
		next_->set_write_timeout(value);
	}
	[[nodiscard]] bool is_open() const noexcept override {
		return next_->is_open();
	}
	[[nodiscard]] endpoint_type remote_endpoint(
		error_code &error) const noexcept override {
		return next_->remote_endpoint(error);
	}
	[[nodiscard]] endpoint_type local_endpoint(
		error_code &error) const noexcept override {
		return next_->local_endpoint(error);
	}

private:
	[[nodiscard]] size_t copy_prefix(mutable_buffer buffer) noexcept
	{
		const auto count = std::min(buffer.size(), prefix_.size() - offset_);
		if( count != 0 )
			std::memcpy(buffer.data(), prefix_.data() + offset_, count);
		offset_ += count;
		return count;
	}

	std::shared_ptr<::riwo::http_exp::connection> next_;
	std::string prefix_;
	size_t offset_ = 0;
};

class pending_server_connection final : public server_connection,
	public std::enable_shared_from_this<pending_server_connection>
{
public:
	pending_server_connection(
		std::shared_ptr<::riwo::http_exp::connection> transport,
		server_config config, server_accept_handler handler) :
		transport_(std::move(transport)), config_(std::move(config)),
		handler_(std::move(handler))
	{
	}

	void start()
	{
		const auto h1 = permits_http1(config_.protocols);
#if RIWO_HTTP_EXP_HTTP2_SUPPORT
		const auto allow_h2 = permits_http2(config_.protocols);
		if( allow_h2 )
		{
			transport_->set_read_timeout(config_.timeout.header);
			auto self = shared_from_this();
			detector_.detect(transport_,
			[self = std::move(self), h1]
			(error_code error, preface_result result,
				std::string prefix) mutable {
				if( error ) return self->finish_error(error);
				if( result == preface_result::http2 )
				{
					self->selected_ = h2::accept_h2(self->transport_,
						self->config_, std::move(prefix),
						std::move(self->handler_));
					return;
				}
				if( not h1 )
					return self->finish_error(
						make_error_code(errc::negotiation_failed));
				auto replay = std::make_shared<replay_connection>(
					self->transport_, std::move(prefix));
				self->selected_ = accept_h1(std::move(replay),
					self->config_.request_limits, self->config_.timeout,
					std::move(self->handler_));
			});
			return;
		}
#endif
		if( h1 )
		{
			selected_ = accept_h1(transport_, config_.request_limits,
				config_.timeout, std::move(handler_));
			return;
		}
		finish_error(make_error_code(errc::negotiation_failed));
	}

	void cancel() noexcept override
	{
		if( selected_ ) selected_->cancel();
		else transport_->cancel();
	}

	[[nodiscard]] bool closed() const noexcept override
	{
		return selected_ ? selected_->closed() : not transport_->is_open();
	}

private:
	void finish_error(error_code error)
	{
		transport_->cancel();
		if( handler_ ) std::move(handler_)(error, {}, {});
	}

	std::shared_ptr<::riwo::http_exp::connection> transport_;
	server_config config_;
	server_accept_handler handler_;
	http2_preface_detector detector_;
	server_connection_ptr selected_;
};

} //namespace

error_code validate_server_config(const server_config &config) noexcept
{
	if( config.h1.max_pipeline_depth != 1 or
		config.protocols.preference.empty() )
		return make_error_code(config.h1.max_pipeline_depth != 1 ?
			errc::operation_not_supported : errc::negotiation_failed);
	for( auto current = config.protocols.preference.begin();
		current != config.protocols.preference.end(); ++current )
		if( std::ranges::find(config.protocols.preference.begin(), current,
			*current) != current )
			return make_error_code(errc::negotiation_failed);

	if( not permits_http1(config.protocols)
#if RIWO_HTTP_EXP_HTTP2_SUPPORT
		and not permits_http2(config.protocols)
#endif
	) return make_error_code(errc::negotiation_failed);
	const auto &limits = config.request_limits;
	if( limits.max_start_line_size == 0 or
		limits.max_field_name_size == 0 or
		limits.max_field_value_size == 0 or
		limits.max_header_list_size == 0 or
		limits.max_field_count == 0 or limits.max_buffered_body == 0 or
		limits.max_discard_body == 0 )
		return make_error_code(errc::operation_not_permitted);

#if RIWO_HTTP_EXP_HTTP2_SUPPORT
	if( permits_http2(config.protocols) )
	{
		const auto &value = config.h2;
		constexpr uint32_t maximum_window = 0x7fffffffU;
		if( value.enable_extended_connect )
			return make_error_code(errc::operation_not_supported);
		if( value.initial_stream_window == 0 or
			value.initial_stream_window > maximum_window or
			value.initial_connection_window < 65'535 or
			value.initial_connection_window > maximum_window or
			value.max_concurrent_streams == 0 or
			value.max_frame_size < 16U * 1024U or
			value.max_frame_size > 16'777'215U or
			value.max_header_list_size == 0 or
			value.max_header_list_size > UINT32_MAX or
			value.max_dynamic_table_size > UINT32_MAX or
			value.inbound_stream_buffer < value.initial_stream_window or
			value.outbound_stream_buffer == 0 or
			value.inbound_connection_buffer <
				value.initial_connection_window or
			value.outbound_connection_buffer == 0 )
			return make_error_code(errc::operation_not_permitted);
		const auto reserved =
			static_cast<uint64_t>(value.initial_stream_window) *
			value.max_concurrent_streams;
		if( reserved > value.inbound_connection_buffer )
			return make_error_code(errc::operation_not_permitted);
	}
#endif
	return {};
}

server_connection_ptr accept_server(asio::ip::tcp::socket socket,
	server_config config, server_accept_handler handler)
{
	error_code option_error;
	(void)socket.set_option(asio::ip::tcp::no_delay(true), option_error);
	auto value = std::make_shared<pending_server_connection>(
		std::make_shared<tcp_connection>(std::move(socket)),
		std::move(config), std::move(handler));
	value->start();
	return value;
}

} //namespace riwo::http_exp::detail
