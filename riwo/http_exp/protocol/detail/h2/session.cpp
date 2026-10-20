// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "session.h"

#if RIWO_HTTP_EXP_HTTP2_SUPPORT

#include "nghttp2_compat.h"
#include <atomic>
#include <charconv>
#include <deque>
#include <mutex>

namespace riwo::http_exp::detail::h2 { namespace
{

class h2_session;
class h2_stream;

[[nodiscard]] error_code aborted() noexcept {
	return asio::error::make_error_code(asio::error::operation_aborted);
}

[[nodiscard]] error_code protocol_error() noexcept {
	return make_error_code(errc::connection_protocol_error);
}

[[nodiscard]] bool forbidden_header(std::string_view name) noexcept
{
	static constexpr std::array<std::string_view,5> values {
		"connection", "keep-alive", "proxy-connection",
		"transfer-encoding", "upgrade"
	};
	return std::ranges::find(values, name) != values.end();
}

[[nodiscard]] std::string lower(std::string_view value)
{
	std::string result(value);
	std::ranges::transform(result, result.begin(), [](unsigned char byte) {
		return static_cast<char>(std::tolower(byte));
	});
	return result;
}

struct nv_block
{
	std::vector<std::string> storage;
	std::vector<nghttp2_nv> values;
};

void add_nv(nv_block &block, std::string name, std::string value)
{
	block.storage.push_back(std::move(name));
	block.storage.push_back(std::move(value));
	auto &stored_name = block.storage[block.storage.size() - 2];
	auto &stored_value = block.storage.back();
	block.values.push_back({
		reinterpret_cast<uint8_t *>(stored_name.data()),
		reinterpret_cast<uint8_t *>(stored_value.data()),
		stored_name.size(), stored_value.size(), NGHTTP2_NV_FLAG_NONE
	});
}

[[nodiscard]] sys_expected<nv_block> request_headers(
	const request_head &head) noexcept
{
	try {
		nv_block result;
		result.storage.reserve((head.headers.size() + 4) * 2);
		result.values.reserve(head.headers.size() + 4);
		add_nv(result, ":method", std::string(head.method.text()));
		add_nv(result, ":scheme", head.scheme);
		add_nv(result, ":authority", head.authority);
		add_nv(result, ":path", head.target.empty() ? "/" : head.target);
		for( const auto &field : head.headers )
		{
			auto name = lower(field.name);
			if( forbidden_header(name) or
				(name == "te" and lower(field.value) != "trailers") )
				return sys_unexpected(make_error_code(errc::invalid_field_name));
			add_nv(result, std::move(name), field.value);
		}
		return result;
	}
	catch(...) {
		return sys_unexpected(exception_error(std::current_exception()));
	}
}

[[nodiscard]] sys_expected<nv_block> trailer_headers(
	const fields &trailers) noexcept
{
	try {
		nv_block result;
		result.storage.reserve(trailers.size() * 2);
		result.values.reserve(trailers.size());
		for( const auto &field : trailers )
		{
			auto name = lower(field.name);
			if( name.starts_with(':') or forbidden_header(name) or name == "te" )
				return sys_unexpected(make_error_code(errc::invalid_trailer));
			add_nv(result, std::move(name), field.value);
		}
		return result;
	}
	catch(...) {
		return sys_unexpected(exception_error(std::current_exception()));
	}
}

[[nodiscard]] error_code validate(const client_config &config) noexcept
{
	const auto &value = config.h2;
	constexpr uint32_t max_window = 0x7fffffffU;
	if( value.initial_stream_window == 0 or
		value.initial_stream_window > max_window or
		value.initial_connection_window < 65'535 or
		value.initial_connection_window > max_window or
		value.max_concurrent_streams == 0 or
		value.max_frame_size < 16U * 1024U or
		value.max_frame_size > 16'777'215U or
		value.max_header_list_size == 0 or
		value.inbound_stream_buffer < value.initial_stream_window or
		value.outbound_stream_buffer == 0 or
		value.inbound_connection_buffer < 65'535 or
		value.outbound_connection_buffer == 0 or
		value.enable_extended_connect )
		return make_error_code(value.enable_extended_connect ?
			errc::operation_not_supported : errc::operation_not_permitted);

	const auto reserved = static_cast<uint64_t>(value.initial_stream_window) *
		value.max_concurrent_streams;
	if( reserved > value.inbound_connection_buffer )
		return make_error_code(errc::operation_not_permitted);
	return {};
}

class h2_reader_adapter final : public body_reader_adapter
{
public:
	explicit h2_reader_adapter(std::weak_ptr<h2_stream> owner) :
		owner_(std::move(owner)) {}

	[[nodiscard]] body_read_result read_some_sync(mutable_buffer buffer,
		error_code &error) noexcept override;
	void read_some(mutable_buffer buffer, read_handler handler) override;
	[[nodiscard]] fields trailers_sync(error_code &error) noexcept override;
	void trailers(trailers_handler handler) override;
	void cancel() noexcept override;

private:
	std::weak_ptr<h2_stream> owner_;
};

class h2_writer_adapter final : public body_writer_adapter
{
public:
	explicit h2_writer_adapter(std::weak_ptr<h2_stream> owner) :
		owner_(std::move(owner)) {}

	[[nodiscard]] size_t write_sync(const_buffer buffer,
		error_code &error) noexcept override;
	void write(const_buffer buffer, write_handler handler) override;
	void finish_sync(fields trailers, error_code &error) noexcept override;
	void finish(fields trailers, finish_handler handler) override;
	void cancel() noexcept override;

private:
	std::weak_ptr<h2_stream> owner_;
};

class h2_stream final : public stream,
	public std::enable_shared_from_this<h2_stream>
{
public:
	h2_stream(std::shared_ptr<h2_session> session, request_head request,
		body_spec spec);
	~h2_stream() override;

	void initialize();
	[[nodiscard]] protocol selected_protocol() const noexcept override {
		return protocol::v2;
	}
	[[nodiscard]] stream_capabilities capabilities() const noexcept override {
		return {.trailers = true, .full_duplex = true};
	}
	[[nodiscard]] request_head request() const override { return request_; }
	[[nodiscard]] std::shared_ptr<body_reader_state>
	inbound_body() const noexcept override { return inbound_; }
	[[nodiscard]] std::shared_ptr<body_writer_state>
	outbound_body() const noexcept override { return outbound_; }
	[[nodiscard]] response_head wait_response_sync(
		error_code &error) noexcept override;
	void wait_response(response_handler handler) override;
	[[nodiscard]] endpoint_type remote_endpoint(
		error_code &error) const noexcept override;
	[[nodiscard]] endpoint_type local_endpoint(
		error_code &error) const noexcept override;
	[[nodiscard]] bool reusable() const noexcept override;
	[[nodiscard]] request_delivery delivery() const noexcept override {
		return delivery_;
	}
	void cancel() noexcept override;

	std::shared_ptr<h2_session> session_;
	request_head request_;
	body_spec spec_;
	int32_t id_ = -1;
	std::shared_ptr<h2_reader_adapter> reader_adapter_;
	std::shared_ptr<h2_writer_adapter> writer_adapter_;
	std::shared_ptr<body_reader_state> inbound_;
	std::shared_ptr<body_writer_state> outbound_;

	std::deque<std::vector<std::byte>> inbound_chunks_;
	size_t inbound_offset_ = 0;
	size_t inbound_size_ = 0;
	uint64_t inbound_total_ = 0;
	bool remote_end_ = false;
	fields inbound_trailers_;
	optional<mutable_buffer> pending_read_buffer_;
	body_reader_adapter::read_handler pending_read_handler_;

	std::deque<std::vector<std::byte>> outbound_chunks_;
	size_t outbound_offset_ = 0;
	size_t outbound_size_ = 0;
	bool outbound_finished_ = false;
	bool trailers_submitted_ = false;
	fields outbound_trailers_;
	struct pending_write
	{
		std::vector<std::byte> bytes;
		body_writer_adapter::write_handler handler;
	};
	std::deque<pending_write> pending_writes_;

	response_head response_;
	fields header_fields_;
	optional<uint16_t> header_status_;
	size_t header_bytes_ = 0;
	size_t header_count_ = 0;
	bool collecting_trailers_ = false;
	bool response_ready_ = false;
	stream::response_handler response_handler_;
	error_code terminal_error_;
	uint32_t wire_error_ = NGHTTP2_NO_ERROR;
	bool closed_ = false;
	bool cancelled_ = false;
	request_delivery delivery_ = request_delivery::not_started;
};

class h2_session final : public client_session,
	public std::enable_shared_from_this<h2_session>
{
public:
	h2_session(std::shared_ptr<::riwo::http_exp::connection> transport,
		client_config config, asio::ip::tcp::endpoint remote,
		asio::ip::tcp::endpoint local) :
		executor_(transport->get_executor()), strand_(asio::make_strand(executor_)),
		transport_(std::move(transport)), config_(std::move(config)),
		remote_(remote), local_(local),
		peer_max_streams_(config_.h2.max_concurrent_streams)
	{
	}

	~h2_session() override
	{
		if( native_ ) nghttp2_session_del(native_);
		if( callbacks_ ) nghttp2_session_callbacks_del(callbacks_);
		if( option_ ) nghttp2_option_del(option_);
	}

	[[nodiscard]] error_code initialize() noexcept
	{
		if( auto error = validate(config_) ) return error;
		if( nghttp2_session_callbacks_new(&callbacks_) != 0 or
			nghttp2_option_new(&option_) != 0 )
			return make_error_code(errc::operation_not_supported);

		nghttp2_session_callbacks_set_on_begin_headers_callback(callbacks_,
			&h2_session::on_begin_headers);
		nghttp2_session_callbacks_set_on_header_callback(callbacks_,
			&h2_session::on_header);
		nghttp2_session_callbacks_set_on_frame_recv_callback(callbacks_,
			&h2_session::on_frame_recv);
		nghttp2_session_callbacks_set_on_data_chunk_recv_callback(callbacks_,
			&h2_session::on_data_chunk);
		nghttp2_session_callbacks_set_on_stream_close_callback(callbacks_,
			&h2_session::on_stream_close);
		nghttp2_option_set_no_auto_window_update(option_, 1);
		nghttp2_option_set_max_deflate_dynamic_table_size(option_,
			config_.h2.max_dynamic_table_size);
		if( nghttp2_session_client_new2(&native_, callbacks_, this, option_) != 0 )
			return make_error_code(errc::operation_not_supported);

		std::array<nghttp2_settings_entry,4> settings {{
			{NGHTTP2_SETTINGS_INITIAL_WINDOW_SIZE,
				config_.h2.initial_stream_window},
			{NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS,
				config_.h2.max_concurrent_streams},
			{NGHTTP2_SETTINGS_MAX_FRAME_SIZE, config_.h2.max_frame_size},
			{NGHTTP2_SETTINGS_HEADER_TABLE_SIZE,
				static_cast<uint32_t>(std::min<size_t>(
					config_.h2.max_dynamic_table_size, UINT32_MAX))}
		}};
		if( nghttp2_submit_settings(native_, NGHTTP2_FLAG_NONE,
			settings.data(), settings.size()) != 0 )
			return protocol_error();
		if( config_.h2.initial_connection_window > 65'535 )
		{
			const auto increment = static_cast<int32_t>(
				config_.h2.initial_connection_window - 65'535);
			if( nghttp2_submit_window_update(native_, NGHTTP2_FLAG_NONE,
				0, increment) != 0 )
				return protocol_error();
		}
		transport_->set_read_timeout(config_.timeout.body_idle);
		transport_->set_write_timeout(config_.timeout.body_idle);
		return {};
	}

	[[nodiscard]] executor_type get_executor() const noexcept override {
		return executor_;
	}
	[[nodiscard]] protocol selected_protocol() const noexcept override {
		return protocol::v2;
	}
	[[nodiscard]] size_t available_stream_capacity() const noexcept override
	{
		if( draining_ or closed_ ) return 0;
		const auto maximum = std::min<size_t>(peer_max_streams_,
			config_.h2.max_concurrent_streams);
		const auto used = active_streams_.load() + reservations_.load();
		return used >= maximum ? 0 : maximum - used;
	}
	[[nodiscard]] bool draining() const noexcept override { return draining_; }
	[[nodiscard]] bool closed() const noexcept override { return closed_; }

	[[nodiscard]] bool try_reserve_stream() noexcept override
	{
		for(;;)
		{
			if( draining_ or closed_ ) return false;
			const auto maximum = std::min<size_t>(peer_max_streams_,
				config_.h2.max_concurrent_streams);
			auto reserved = reservations_.load();
			if( active_streams_.load() + reserved >= maximum ) return false;
			if( reservations_.compare_exchange_weak(reserved, reserved + 1) )
				return true;
		}
	}

	void release_stream(bool reusable) noexcept override
	{
		auto reserved = reservations_.load();
		while( reserved != 0 and
			not reservations_.compare_exchange_weak(reserved, reserved - 1) ) {}
		if( not reusable and active_streams_ == 0 )
			notify_release(false);
	}

	void set_release_handler(std::function<void(bool)> handler) override {
		std::lock_guard lock(handler_mutex_);
		release_handler_ = std::move(handler);
	}

	[[nodiscard]] std::shared_ptr<stream> open_sync(request_head head,
		body_spec spec, error_code &error) noexcept override
	{
		error.clear();
		if( not select_mode(1) )
		{
			error = make_error_code(errc::operation_not_permitted);
			consume_reservation();
			return {};
		}
		return submit(std::move(head), std::move(spec), error);
	}

	void open(request_head head, body_spec spec, open_handler handler) override
	{
		if( not select_mode(2) )
		{
			consume_reservation();
			std::move(handler)(make_error_code(errc::operation_not_permitted),
				std::shared_ptr<stream>{});
			return;
		}
		auto self = shared_from_this();
		asio::post(strand_,
		[self = std::move(self), head = std::move(head),
		 spec = std::move(spec), handler = std::move(handler)]() mutable {
			self->start_async();
			error_code error;
			auto value = self->submit(std::move(head), std::move(spec), error);
			if( not error ) self->write_pump();
			std::move(handler)(error, std::move(value));
		});
	}

	void graceful_shutdown(shutdown_handler handler) override
	{
		draining_ = true;
		if( mode_ == 1 )
		{
			(void)nghttp2_submit_goaway(native_, NGHTTP2_FLAG_NONE, 0,
				NGHTTP2_NO_ERROR, nullptr, 0);
			error_code error;
			flush_sync(error);
			if( active_streams_ == 0 ) transport_->cancel();
			std::move(handler)(error);
			return;
		}
		if( not select_mode(2) and mode_ != 2 )
		{
			std::move(handler)(make_error_code(errc::operation_not_permitted));
			return;
		}
		auto self = shared_from_this();
		asio::post(strand_, [self = std::move(self),
			handler = std::move(handler)]() mutable {
			self->start_async();
			(void)nghttp2_submit_goaway(self->native_, NGHTTP2_FLAG_NONE, 0,
				NGHTTP2_NO_ERROR, nullptr, 0);
			self->write_pump();
			if( self->active_streams_ == 0 ) self->transport_->cancel();
			std::move(handler)(error_code{});
		});
	}

	[[nodiscard]] asio::ip::tcp::endpoint remote_endpoint(
		error_code &error) const noexcept {
		error.clear();
		return remote_;
	}
	[[nodiscard]] asio::ip::tcp::endpoint local_endpoint(
		error_code &error) const noexcept {
		error.clear();
		return local_;
	}
	[[nodiscard]] const client_config &config() const noexcept {
		return config_;
	}

	[[nodiscard]] response_head wait_response_sync(h2_stream &value,
		error_code &error) noexcept
	{
		error.clear();
		while( not value.response_ready_ and not value.terminal_error_ )
			if( not drive_read_sync(error) ) return {};
		if( value.terminal_error_ ) error = value.terminal_error_;
		return error ? response_head{} : value.response_;
	}

	void wait_response(const std::shared_ptr<h2_stream> &value,
		stream::response_handler handler)
	{
		auto self = shared_from_this();
		asio::post(strand_, [self = std::move(self), value,
			handler = std::move(handler)]() mutable {
			if( value->terminal_error_ )
				std::move(handler)(value->terminal_error_, response_head{});
			else if( value->response_ready_ )
				std::move(handler)(error_code{}, value->response_);
			else if( value->response_handler_ )
				std::move(handler)(make_error_code(errc::operation_in_progress),
					response_head{});
			else
				value->response_handler_ = std::move(handler);
		});
	}

	[[nodiscard]] body_read_result read_sync(h2_stream &value,
		mutable_buffer buffer, error_code &error) noexcept
	{
		error.clear();
		while( value.inbound_size_ == 0 and not value.remote_end_ and
			not value.terminal_error_ )
			if( not drive_read_sync(error) ) return {};
		if( value.terminal_error_ )
		{
			error = value.terminal_error_;
			return {};
		}
		auto result = take_body(value, buffer);
		if( result.bytes != 0 ) consume(value, result.bytes);
		return result;
	}

	void read(const std::shared_ptr<h2_stream> &value, mutable_buffer buffer,
		body_reader_adapter::read_handler handler)
	{
		auto self = shared_from_this();
		asio::post(strand_, [self = std::move(self), value, buffer,
			handler = std::move(handler)]() mutable {
			if( value->terminal_error_ )
				return std::move(handler)(value->terminal_error_,
					body_read_result{});
			if( value->inbound_size_ != 0 or value->remote_end_ )
			{
				auto result = self->take_body(*value, buffer);
				if( result.bytes != 0 ) self->consume(*value, result.bytes);
				return std::move(handler)(error_code{}, result);
			}
			value->pending_read_buffer_ = buffer;
			value->pending_read_handler_ = std::move(handler);
		});
	}

	[[nodiscard]] size_t write_sync(h2_stream &value, const_buffer buffer,
		error_code &error) noexcept
	{
		error.clear();
		if( buffer.size() > config_.h2.outbound_stream_buffer )
		{
			error = make_error_code(errc::buffer_limit_exceeded);
			return 0;
		}
		while( value.outbound_size_ + buffer.size() >
				config_.h2.outbound_stream_buffer or
			outbound_buffered_ + buffer.size() >
				config_.h2.outbound_connection_buffer )
		{
			flush_sync(error);
			if( error ) return 0;
			if( value.outbound_size_ + buffer.size() <=
					config_.h2.outbound_stream_buffer and
				outbound_buffered_ + buffer.size() <=
					config_.h2.outbound_connection_buffer )
				break;
			if( not drive_read_sync(error) ) return 0;
		}
		enqueue(value, buffer);
		(void)nghttp2_session_resume_data(native_, value.id_);
		flush_sync(error);
		return error ? 0 : buffer.size();
	}

	void write(const std::shared_ptr<h2_stream> &value, const_buffer buffer,
		body_writer_adapter::write_handler handler)
	{
		auto bytes = std::vector<std::byte>(buffer.size());
		if( buffer.size() != 0 )
			std::memcpy(bytes.data(), buffer.data(), buffer.size());
		auto self = shared_from_this();
		asio::post(strand_, [self = std::move(self), value,
			bytes = std::move(bytes), handler = std::move(handler)]() mutable {
			if( value->terminal_error_ )
				return std::move(handler)(value->terminal_error_, 0);
			if( bytes.size() > self->config_.h2.outbound_stream_buffer )
				return std::move(handler)(
					make_error_code(errc::buffer_limit_exceeded), 0);
			if( value->outbound_size_ + bytes.size() >
					self->config_.h2.outbound_stream_buffer or
				self->outbound_buffered_ + bytes.size() >
					self->config_.h2.outbound_connection_buffer )
			{
				value->pending_writes_.push_back(
					{std::move(bytes), std::move(handler)});
				return;
			}
			const auto size = bytes.size();
			self->enqueue(*value, std::move(bytes));
			(void)nghttp2_session_resume_data(self->native_, value->id_);
			self->write_pump();
			std::move(handler)(error_code{}, size);
		});
	}

	void finish_sync(h2_stream &value, fields trailers,
		error_code &error) noexcept
	{
		error.clear();
		value.outbound_finished_ = true;
		value.outbound_trailers_ = std::move(trailers);
		(void)nghttp2_session_resume_data(native_, value.id_);
		flush_sync(error);
	}

	void finish(const std::shared_ptr<h2_stream> &value, fields trailers,
		body_writer_adapter::finish_handler handler)
	{
		auto self = shared_from_this();
		asio::post(strand_, [self = std::move(self), value,
			trailers = std::move(trailers), handler = std::move(handler)]() mutable {
			if( value->terminal_error_ )
				return std::move(handler)(value->terminal_error_);
			value->outbound_finished_ = true;
			value->outbound_trailers_ = std::move(trailers);
			(void)nghttp2_session_resume_data(self->native_, value->id_);
			self->write_pump();
			std::move(handler)(error_code{});
		});
	}

	void cancel(const std::shared_ptr<h2_stream> &value) noexcept
	{
		if( mode_ == 1 )
		{
			cancel_stream(*value);
			error_code ignored;
			flush_sync(ignored);
			return;
		}
		auto self = shared_from_this();
		asio::post(strand_, [self = std::move(self), value] {
			self->cancel_stream(*value);
			self->write_pump();
		});
	}

	void release_buffer_budget(size_t inbound, size_t outbound) noexcept
	{
		if( mode_ == 2 )
		{
			auto self = shared_from_this();
			asio::post(strand_, [self = std::move(self), inbound, outbound] {
				self->inbound_buffered_ -= std::min(
					self->inbound_buffered_, inbound);
				self->outbound_buffered_ -= std::min(
					self->outbound_buffered_, outbound);
			});
			return;
		}
		inbound_buffered_ -= std::min(inbound_buffered_, inbound);
		outbound_buffered_ -= std::min(outbound_buffered_, outbound);
	}

	[[nodiscard]] fields trailers_sync(h2_stream &value,
		error_code &error) noexcept
	{
		error.clear();
		if( not value.remote_end_ )
			error = make_error_code(errc::operation_not_permitted);
		return error ? fields{} : value.inbound_trailers_;
	}

	void trailers(const std::shared_ptr<h2_stream> &value,
		body_reader_adapter::trailers_handler handler)
	{
		auto self = shared_from_this();
		asio::post(strand_, [self = std::move(self), value,
			handler = std::move(handler)]() mutable {
			ignore_unused(self);
			if( not value->remote_end_ )
				std::move(handler)(make_error_code(
					errc::operation_not_permitted), fields{});
			else
				std::move(handler)(error_code{}, value->inbound_trailers_);
		});
	}

private:
	[[nodiscard]] bool select_mode(int desired) noexcept
	{
		int expected = 0;
		return mode_.compare_exchange_strong(expected, desired) or
			expected == desired;
	}

	void consume_reservation() noexcept
	{
		auto reserved = reservations_.load();
		while( reserved != 0 and
			not reservations_.compare_exchange_weak(reserved, reserved - 1) ) {}
	}

	[[nodiscard]] std::shared_ptr<h2_stream> submit(request_head head,
		body_spec spec, error_code &error) noexcept
	{
		error.clear();
		consume_reservation();
		if( draining_ or closed_ or available_stream_capacity() == 0 )
		{
			error = make_error_code(errc::operation_not_permitted);
			return {};
		}
		auto headers = request_headers(head);
		if( not headers )
		{
			error = headers.error();
			return {};
		}
		try {
			auto value = std::make_shared<h2_stream>(shared_from_this(),
				std::move(head), spec);
			value->initialize();
			nghttp2_api::data_provider provider {};
			nghttp2_api::data_provider *provider_ptr = nullptr;
			if( spec.type() != body_spec::mode::empty and
				spec.type() != body_spec::mode::head )
			{
				provider.source.ptr = value.get();
				provider.read_callback = &h2_session::data_read;
				provider_ptr = &provider;
			}
			const auto stream_id = nghttp2_api::submit_request(native_, nullptr,
				headers->values.data(), headers->values.size(), provider_ptr,
				value.get());
			if( stream_id < 0 )
			{
				error = make_error_code(errc::stream_protocol_error);
				return {};
			}
			value->id_ = stream_id;
			value->delivery_ = request_delivery::possibly_processed;
			streams_.emplace(stream_id, value);
			++active_streams_;
			return value;
		}
		catch(...) {
			error = exception_error(std::current_exception());
			return {};
		}
	}

	void start_async()
	{
		if( async_started_ ) return;
		async_started_ = true;
		read_loop();
		write_pump();
	}

	void read_loop()
	{
		if( reading_ or closed_ ) return;
		reading_ = true;
		auto self = shared_from_this();
		transport_->read_some(riwo::buffer(read_buffer_),
			asio::bind_executor(strand_,
			[self = std::move(self)](error_code error, size_t size) mutable {
				self->reading_ = false;
				if( error or size == 0 )
				{
					self->fail(error ? error : protocol_error());
					return;
				}
				const auto consumed = nghttp2_api::session_mem_recv(self->native_,
					reinterpret_cast<const uint8_t *>(self->read_buffer_.data()),
					size);
				if( consumed < 0 or static_cast<size_t>(consumed) != size )
				{
					self->fail(protocol_error());
					return;
				}
				self->write_pump();
				self->read_loop();
			}));
	}

	void write_pump()
	{
		if( writing_ or closed_ ) return;
		const uint8_t *data = nullptr;
		const auto size = nghttp2_api::session_mem_send(native_, &data);
		if( size < 0 ) return fail(protocol_error());
		if( size == 0 ) return;
		if( static_cast<size_t>(size) >
			config_.h2.outbound_connection_buffer )
			return fail(make_error_code(errc::buffer_limit_exceeded));
		writing_ = true;
		auto wire = std::make_shared<std::vector<std::byte>>(
			static_cast<size_t>(size));
		std::memcpy(wire->data(), data, wire->size());
		auto self = shared_from_this();
		transport_->write(riwo::buffer(*wire), asio::bind_executor(strand_,
			[self = std::move(self), wire]
			(error_code error, size_t written) mutable {
				self->writing_ = false;
				if( error or written != wire->size() )
				{
					self->fail(error ? error : protocol_error());
					return;
				}
				self->write_pump();
			}));
	}

	void flush_sync(error_code &error) noexcept
	{
		error.clear();
		for(;;)
		{
			const uint8_t *data = nullptr;
			const auto size = nghttp2_api::session_mem_send(native_, &data);
			if( size < 0 )
			{
				error = protocol_error();
				return;
			}
			if( size == 0 ) return;
			const auto written = transport_->write_sync(
				const_buffer(data, static_cast<size_t>(size)), error);
			if( error or written != static_cast<size_t>(size) )
			{
				if( not error ) error = protocol_error();
				return;
			}
		}
	}

	[[nodiscard]] bool drive_read_sync(error_code &error) noexcept
	{
		flush_sync(error);
		if( error ) return false;
		const auto size = transport_->read_some_sync(riwo::buffer(read_buffer_),
			error);
		if( error or size == 0 )
		{
			if( not error ) error = protocol_error();
			fail_sync(error);
			return false;
		}
		const auto consumed = nghttp2_api::session_mem_recv(native_,
			reinterpret_cast<const uint8_t *>(read_buffer_.data()), size);
		if( consumed < 0 or static_cast<size_t>(consumed) != size )
		{
			error = protocol_error();
			fail_sync(error);
			return false;
		}
		flush_sync(error);
		if( error ) fail_sync(error);
		return not error;
	}

	void fail(error_code error)
	{
		if( closed_.exchange(true) ) return;
		draining_ = true;
		for( auto &[id, value] : streams_ )
		{
			ignore_unused(id);
			complete_error(*value, error);
		}
		transport_->cancel();
		notify_release(false);
	}

	void fail_sync(error_code error)
	{
		if( closed_.exchange(true) ) return;
		draining_ = true;
		for( auto &[id, value] : streams_ )
		{
			ignore_unused(id);
			complete_error(*value, error);
		}
		transport_->cancel();
		notify_release(false);
	}

	void complete_error(h2_stream &value, error_code error)
	{
		if( not value.terminal_error_ ) value.terminal_error_ = error;
		if( value.response_handler_ )
			std::move(value.response_handler_)(value.terminal_error_,
				response_head{});
		if( value.pending_read_handler_ )
		{
			value.pending_read_buffer_.reset();
			std::move(value.pending_read_handler_)(value.terminal_error_,
				body_read_result{});
		}
		while( not value.pending_writes_.empty() )
		{
			auto pending = std::move(value.pending_writes_.front());
			value.pending_writes_.pop_front();
			std::move(pending.handler)(value.terminal_error_, 0);
		}
	}

	void notify_release(bool reusable) noexcept
	{
		std::function<void(bool)> handler;
		{
			std::lock_guard lock(handler_mutex_);
			handler = release_handler_;
		}
		try {
			if( handler ) handler(reusable);
		}
		catch(...) {}
	}

	[[nodiscard]] body_read_result take_body(h2_stream &value,
		mutable_buffer buffer)
	{
		size_t copied = 0;
		while( copied < buffer.size() and not value.inbound_chunks_.empty() )
		{
			auto &chunk = value.inbound_chunks_.front();
			const auto available = chunk.size() - value.inbound_offset_;
			const auto count = std::min(available, buffer.size() - copied);
			std::memcpy(static_cast<std::byte *>(buffer.data()) + copied,
				chunk.data() + value.inbound_offset_, count);
			copied += count;
			value.inbound_offset_ += count;
			value.inbound_size_ -= count;
			inbound_buffered_ -= count;
			if( value.inbound_offset_ == chunk.size() )
			{
				value.inbound_chunks_.pop_front();
				value.inbound_offset_ = 0;
			}
		}
		return {copied, value.remote_end_ and value.inbound_size_ == 0};
	}

	void consume(h2_stream &value, size_t size)
	{
		if( size == 0 or value.id_ <= 0 ) return;
		if( nghttp2_session_consume(native_, value.id_, size) != 0 )
			return fail(protocol_error());
		if( mode_ == 2 ) write_pump();
	}

	void enqueue(h2_stream &value, const_buffer buffer)
	{
		std::vector<std::byte> bytes(buffer.size());
		if( buffer.size() != 0 )
			std::memcpy(bytes.data(), buffer.data(), buffer.size());
		enqueue(value, std::move(bytes));
	}

	void enqueue(h2_stream &value, std::vector<std::byte> bytes)
	{
		value.outbound_size_ += bytes.size();
		outbound_buffered_ += bytes.size();
		value.outbound_chunks_.push_back(std::move(bytes));
	}

	void drain_pending(h2_stream &value)
	{
		while( not value.pending_writes_.empty() )
		{
			auto &pending = value.pending_writes_.front();
			if( value.outbound_size_ + pending.bytes.size() >
					config_.h2.outbound_stream_buffer or
				outbound_buffered_ + pending.bytes.size() >
					config_.h2.outbound_connection_buffer )
				return;
			auto item = std::move(pending);
			value.pending_writes_.pop_front();
			const auto size = item.bytes.size();
			enqueue(value, std::move(item.bytes));
			std::move(item.handler)(error_code{}, size);
		}
	}

	void deliver_pending_read(h2_stream &value)
	{
		if( not value.pending_read_handler_ or
			(value.inbound_size_ == 0 and not value.remote_end_ and
				not value.terminal_error_) ) return;
		auto handler = std::move(value.pending_read_handler_);
		auto buffer = *value.pending_read_buffer_;
		value.pending_read_buffer_.reset();
		if( value.terminal_error_ )
		{
			std::move(handler)(value.terminal_error_, body_read_result{});
			return;
		}
		auto result = take_body(value, buffer);
		if( result.bytes != 0 ) consume(value, result.bytes);
		std::move(handler)(error_code{}, result);
	}

	void response_ready(h2_stream &value)
	{
		if( not value.header_status_ )
		{
			complete_error(value, make_error_code(errc::stream_protocol_error));
			(void)nghttp2_submit_rst_stream(native_, NGHTTP2_FLAG_NONE,
				value.id_, NGHTTP2_PROTOCOL_ERROR);
			return;
		}
		if( *value.header_status_ < 200 )
		{
			value.header_fields_ = {};
			value.header_status_.reset();
			value.header_bytes_ = 0;
			value.header_count_ = 0;
			return;
		}
		try {
			value.response_ = {http_exp::status(*value.header_status_),
				std::move(value.header_fields_)};
			value.response_ready_ = true;
			if( value.response_handler_ )
				std::move(value.response_handler_)(error_code{}, value.response_);
		}
		catch(...) {
			complete_error(value, make_error_code(errc::stream_protocol_error));
		}
	}

	void remote_end(h2_stream &value)
	{
		value.remote_end_ = true;
		deliver_pending_read(value);
	}

	void cancel_stream(h2_stream &value)
	{
		if( value.cancelled_ or value.closed_ ) return;
		value.cancelled_ = true;
		complete_error(value, aborted());
		if( value.id_ > 0 )
			(void)nghttp2_submit_rst_stream(native_, NGHTTP2_FLAG_NONE,
				value.id_, NGHTTP2_CANCEL);
	}

	static int on_begin_headers(nghttp2_session *, const nghttp2_frame *frame,
		void *user_data)
	{
		auto &self = *static_cast<h2_session *>(user_data);
		if( frame->hd.type != NGHTTP2_HEADERS ) return 0;
		auto found = self.streams_.find(frame->hd.stream_id);
		if( found == self.streams_.end() ) return 0;
		auto &value = *found->second;
		value.collecting_trailers_ = value.response_ready_;
		value.header_fields_ = {};
		value.header_status_.reset();
		value.header_bytes_ = 0;
		value.header_count_ = 0;
		return 0;
	}

	static int on_header(nghttp2_session *, const nghttp2_frame *frame,
		const uint8_t *name, size_t name_size, const uint8_t *field_value,
		size_t value_size, uint8_t, void *user_data)
	{
		auto &self = *static_cast<h2_session *>(user_data);
		auto found = self.streams_.find(frame->hd.stream_id);
		if( found == self.streams_.end() ) return 0;
		auto &value = *found->second;
		const auto header_limit = std::min(self.config_.h2.max_header_list_size,
			self.config_.response_limits.max_header_list_size);
		value.header_bytes_ += name_size + value_size + 32;
		++value.header_count_;
		if( value.header_bytes_ > header_limit or
			value.header_count_ > self.config_.response_limits.max_field_count or
			name_size > self.config_.response_limits.max_field_name_size or
			value_size > self.config_.response_limits.max_field_value_size )
		{
			(void)nghttp2_submit_rst_stream(self.native_, NGHTTP2_FLAG_NONE,
				value.id_, NGHTTP2_ENHANCE_YOUR_CALM);
			self.complete_error(value,
				make_error_code(errc::header_limit_exceeded));
			return 0;
		}
		const std::string_view header_name(
			reinterpret_cast<const char *>(name), name_size);
		const std::string_view header_value(
			reinterpret_cast<const char *>(field_value), value_size);
		if( header_name == ":status" )
		{
			uint16_t status = 0;
			const auto parsed = std::from_chars(header_value.data(),
				header_value.data() + header_value.size(), status);
			if( value.collecting_trailers_ or value.header_status_ or
				parsed.ec != std::errc{} or
				parsed.ptr != header_value.data() + header_value.size() or
				status < 100 or status > 599 )
				return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
			value.header_status_ = status;
			return 0;
		}
		if( header_name.starts_with(':') or forbidden_header(header_name) or
			(header_name == "te" and lower(header_value) != "trailers") )
			return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
		try {
			if( value.collecting_trailers_ )
				value.inbound_trailers_.add(std::string(header_name),
					std::string(header_value));
			else
				value.header_fields_.add(std::string(header_name),
					std::string(header_value));
		}
		catch(...) { return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE; }
		return 0;
	}

	static int on_frame_recv(nghttp2_session *, const nghttp2_frame *frame,
		void *user_data)
	{
		auto &self = *static_cast<h2_session *>(user_data);
		if( frame->hd.type == NGHTTP2_GOAWAY )
		{
			self.draining_ = true;
			for( auto &[id, value] : self.streams_ )
				if( id > frame->goaway.last_stream_id )
				{
					value->wire_error_ = frame->goaway.error_code;
					value->delivery_ =
						request_delivery::definitely_not_processed;
					self.complete_error(*value,
						make_error_code(errc::stream_protocol_error));
				}
			self.notify_release(false);
			return 0;
		}
		if( frame->hd.type == NGHTTP2_SETTINGS and
			not (frame->hd.flags & NGHTTP2_FLAG_ACK) )
		{
			const auto peer = nghttp2_session_get_remote_settings(self.native_,
				NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS);
			self.peer_max_streams_ = static_cast<size_t>(std::min<uint64_t>(peer,
				self.config_.h2.max_concurrent_streams));
			self.notify_release(true);
			return 0;
		}
		if( frame->hd.type != NGHTTP2_HEADERS and
			frame->hd.type != NGHTTP2_DATA and
			frame->hd.type != NGHTTP2_RST_STREAM ) return 0;
		auto found = self.streams_.find(frame->hd.stream_id);
		if( found == self.streams_.end() ) return 0;
		auto &value = *found->second;
		if( frame->hd.type == NGHTTP2_HEADERS and not value.collecting_trailers_ )
			self.response_ready(value);
		if( frame->hd.type == NGHTTP2_RST_STREAM )
		{
			value.wire_error_ = frame->rst_stream.error_code;
			if( value.wire_error_ == NGHTTP2_REFUSED_STREAM )
				value.delivery_ =
					request_delivery::definitely_not_processed;
			self.complete_error(value,
				make_error_code(errc::stream_protocol_error));
		}
		if( frame->hd.flags & NGHTTP2_FLAG_END_STREAM ) self.remote_end(value);
		return 0;
	}

	static int on_data_chunk(nghttp2_session *, uint8_t, int32_t stream_id,
		const uint8_t *data, size_t size, void *user_data)
	{
		auto &self = *static_cast<h2_session *>(user_data);
		auto found = self.streams_.find(stream_id);
		if( found == self.streams_.end() ) return 0;
		auto &value = *found->second;
		if( value.inbound_size_ + size >
				self.config_.h2.inbound_stream_buffer or
			self.inbound_buffered_ + size >
				self.config_.h2.inbound_connection_buffer or
			(self.config_.response_limits.max_body_size and
				value.inbound_total_ + size >
					*self.config_.response_limits.max_body_size) )
		{
			(void)nghttp2_submit_rst_stream(self.native_, NGHTTP2_FLAG_NONE,
				stream_id, NGHTTP2_ENHANCE_YOUR_CALM);
			self.complete_error(value,
				make_error_code(errc::buffer_limit_exceeded));
			return 0;
		}
		try {
			std::vector<std::byte> bytes(size);
			if( size != 0 ) std::memcpy(bytes.data(), data, size);
			value.inbound_chunks_.push_back(std::move(bytes));
			value.inbound_size_ += size;
			value.inbound_total_ += size;
			self.inbound_buffered_ += size;
			self.deliver_pending_read(value);
		}
		catch(...) { return NGHTTP2_ERR_CALLBACK_FAILURE; }
		return 0;
	}

	static int on_stream_close(nghttp2_session *, int32_t stream_id,
		uint32_t error_code_value, void *user_data)
	{
		auto &self = *static_cast<h2_session *>(user_data);
		auto found = self.streams_.find(stream_id);
		if( found == self.streams_.end() ) return 0;
		auto value = found->second;
		value->closed_ = true;
		value->wire_error_ = error_code_value;
		if( error_code_value != NGHTTP2_NO_ERROR and not value->terminal_error_ )
			self.complete_error(*value,
				make_error_code(errc::stream_protocol_error));
		if( error_code_value == NGHTTP2_NO_ERROR ) self.remote_end(*value);
		self.streams_.erase(found);
		if( self.active_streams_ != 0 ) --self.active_streams_;
		self.notify_release(not self.draining_ and not self.closed_);
		if( self.draining_ and self.active_streams_ == 0 )
			self.transport_->cancel();
		return 0;
	}

	static nghttp2_api::ssize_type data_read(nghttp2_session *session, int32_t,
		uint8_t *buffer, size_t length, uint32_t *flags,
		nghttp2_data_source *source, void *user_data)
	{
		auto &self = *static_cast<h2_session *>(user_data);
		auto &value = *static_cast<h2_stream *>(source->ptr);
		if( value.terminal_error_ ) return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
		if( value.outbound_size_ == 0 )
		{
			if( not value.outbound_finished_ ) return NGHTTP2_ERR_DEFERRED;
			*flags |= NGHTTP2_DATA_FLAG_EOF;
			if( not value.outbound_trailers_.empty() and not value.trailers_submitted_ )
			{
				auto trailers = trailer_headers(value.outbound_trailers_);
				if( not trailers ) return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
				*flags |= NGHTTP2_DATA_FLAG_NO_END_STREAM;
				if( nghttp2_submit_trailer(session, value.id_,
					trailers->values.data(), trailers->values.size()) != 0 )
					return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
				value.trailers_submitted_ = true;
			}
			return 0;
		}

		size_t copied = 0;
		while( copied < length and not value.outbound_chunks_.empty() )
		{
			auto &chunk = value.outbound_chunks_.front();
			const auto available = chunk.size() - value.outbound_offset_;
			const auto count = std::min(available, length - copied);
			std::memcpy(buffer + copied,
				chunk.data() + value.outbound_offset_, count);
			copied += count;
			value.outbound_offset_ += count;
			value.outbound_size_ -= count;
			self.outbound_buffered_ -= count;
			if( value.outbound_offset_ == chunk.size() )
			{
				value.outbound_chunks_.pop_front();
				value.outbound_offset_ = 0;
			}
		}
		self.drain_pending(value);
		if( value.outbound_size_ == 0 and value.outbound_finished_ )
		{
			*flags |= NGHTTP2_DATA_FLAG_EOF;
			if( not value.outbound_trailers_.empty() and not value.trailers_submitted_ )
			{
				auto trailers = trailer_headers(value.outbound_trailers_);
				if( not trailers ) return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
				*flags |= NGHTTP2_DATA_FLAG_NO_END_STREAM;
				if( nghttp2_submit_trailer(session, value.id_,
					trailers->values.data(), trailers->values.size()) != 0 )
					return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
				value.trailers_submitted_ = true;
			}
		}
		return static_cast<nghttp2_api::ssize_type>(copied);
	}

	executor_type executor_;
	asio::strand<asio::any_io_executor> strand_;
	std::shared_ptr<::riwo::http_exp::connection> transport_;
	client_config config_;
	asio::ip::tcp::endpoint remote_;
	asio::ip::tcp::endpoint local_;
	nghttp2_session_callbacks *callbacks_ = nullptr;
	nghttp2_option *option_ = nullptr;
	nghttp2_session *native_ = nullptr;
	std::unordered_map<int32_t,std::shared_ptr<h2_stream>> streams_;
	std::array<std::byte,16 * 1024> read_buffer_ {};
	size_t inbound_buffered_ = 0;
	size_t outbound_buffered_ = 0;
	std::atomic<size_t> active_streams_ {0};
	std::atomic<size_t> reservations_ {0};
	std::atomic<size_t> peer_max_streams_;
	std::atomic<bool> draining_ {false};
	std::atomic<bool> closed_ {false};
	std::atomic<int> mode_ {0};
	bool async_started_ = false;
	bool reading_ = false;
	bool writing_ = false;
	std::mutex handler_mutex_;
	std::function<void(bool)> release_handler_;
};

h2_stream::h2_stream(std::shared_ptr<h2_session> session,
	request_head request, body_spec spec) :
	session_(std::move(session)), request_(std::move(request)), spec_(std::move(spec))
{
}

h2_stream::~h2_stream()
{
	if( session_ )
		session_->release_buffer_budget(inbound_size_, outbound_size_);
}

void h2_stream::initialize()
{
	reader_adapter_ = std::make_shared<h2_reader_adapter>(weak_from_this());
	writer_adapter_ = std::make_shared<h2_writer_adapter>(weak_from_this());
	inbound_ = make_body_reader_state(reader_adapter_, false,
		session_->config().response_limits.max_discard_body);
	outbound_ = make_body_writer_state(writer_adapter_, spec_, true);
}

response_head h2_stream::wait_response_sync(error_code &error) noexcept {
	return session_->wait_response_sync(*this, error);
}

void h2_stream::wait_response(response_handler handler) {
	session_->wait_response(shared_from_this(), std::move(handler));
}

auto h2_stream::remote_endpoint(error_code &error) const noexcept -> endpoint_type {
	return session_->remote_endpoint(error);
}

auto h2_stream::local_endpoint(error_code &error) const noexcept -> endpoint_type {
	return session_->local_endpoint(error);
}

bool h2_stream::reusable() const noexcept {
	return closed_ and not terminal_error_ and not session_->draining();
}

void h2_stream::cancel() noexcept {
	if( session_ ) session_->cancel(shared_from_this());
}

body_read_result h2_reader_adapter::read_some_sync(mutable_buffer buffer,
	error_code &error) noexcept
{
	auto owner = owner_.lock();
	if( not owner )
	{
		error = aborted();
		return {};
	}
	return owner->session_->read_sync(*owner, buffer, error);
}

void h2_reader_adapter::read_some(mutable_buffer buffer, read_handler handler)
{
	auto owner = owner_.lock();
	if( not owner ) return std::move(handler)(aborted(), body_read_result{});
	owner->session_->read(owner, buffer, std::move(handler));
}

fields h2_reader_adapter::trailers_sync(error_code &error) noexcept
{
	auto owner = owner_.lock();
	if( not owner )
	{
		error = aborted();
		return {};
	}
	return owner->session_->trailers_sync(*owner, error);
}

void h2_reader_adapter::trailers(trailers_handler handler)
{
	auto owner = owner_.lock();
	if( not owner ) return std::move(handler)(aborted(), fields{});
	owner->session_->trailers(owner, std::move(handler));
}

void h2_reader_adapter::cancel() noexcept {
	if( auto owner = owner_.lock() ) owner->cancel();
}

size_t h2_writer_adapter::write_sync(const_buffer buffer,
	error_code &error) noexcept
{
	auto owner = owner_.lock();
	if( not owner )
	{
		error = aborted();
		return 0;
	}
	return owner->session_->write_sync(*owner, buffer, error);
}

void h2_writer_adapter::write(const_buffer buffer, write_handler handler)
{
	auto owner = owner_.lock();
	if( not owner ) return std::move(handler)(aborted(), 0);
	owner->session_->write(owner, buffer, std::move(handler));
}

void h2_writer_adapter::finish_sync(fields trailers, error_code &error) noexcept
{
	auto owner = owner_.lock();
	if( not owner )
	{
		error = aborted();
		return;
	}
	owner->session_->finish_sync(*owner, std::move(trailers), error);
}

void h2_writer_adapter::finish(fields trailers, finish_handler handler)
{
	auto owner = owner_.lock();
	if( not owner ) return std::move(handler)(aborted());
	owner->session_->finish(owner, std::move(trailers), std::move(handler));
}

void h2_writer_adapter::cancel() noexcept {
	if( auto owner = owner_.lock() ) owner->cancel();
}

} //namespace

sys_expected<std::shared_ptr<client_session>> make_client_session(
	std::shared_ptr<::riwo::http_exp::connection> transport,
	client_config config, asio::ip::tcp::endpoint remote,
	asio::ip::tcp::endpoint local)
{
	if( not transport )
		return sys_unexpected(make_error_code(errc::negotiation_failed));
	auto result = std::make_shared<h2_session>(std::move(transport),
		std::move(config), remote, local);
	if( auto error = result->initialize() ) return sys_unexpected(error);
	return std::static_pointer_cast<client_session>(std::move(result));
}

} //namespace riwo::http_exp::detail::h2

#endif //RIWO_HTTP_EXP_HTTP2_SUPPORT
