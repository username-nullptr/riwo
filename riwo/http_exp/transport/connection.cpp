// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "connection.h"

namespace riwo::http_exp
{

connection::connection() = default;

connection::~connection() = default;

void connection::connect_sync(std::string_view host, uint16_t port, error_code &error) noexcept
{
	ignore_unused(host);
	ignore_unused(port);
	error = make_error_code(errc::operation_not_supported);
}

void connection::connect_async(std::string_view host, uint16_t port, close_handler_t handler)
{
	ignore_unused(host);
	ignore_unused(port);
	std::move(handler)(make_error_code(errc::operation_not_supported));
}

void connection::write(std::span<const const_buffer> buffers, io_handler_t handler)
{
	write_span(buffers, std::move(handler));
}

size_t connection::read_some_sync(const mutable_buffer&, error_code &error) noexcept
{
	error = make_error_code(errc::operation_not_supported);
	return 0;
}

size_t connection::write_sync(const const_buffer&, error_code &error) noexcept
{
	error = make_error_code(errc::operation_not_supported);
	return 0;
}

void connection::set_read_timeout(optional<timeout_t> value)
{
	ignore_unused(value);
}

void connection::set_write_timeout(optional<timeout_t> value)
{
	ignore_unused(value);
}

bool connection::is_open() const noexcept
{
	return true;
}

auto connection::remote_endpoint(error_code &error) const noexcept -> endpoint_t
{
	error = make_error_code(errc::operation_not_supported);
	return {};
}

auto connection::local_endpoint(error_code &error) const noexcept -> endpoint_t
{
	error = make_error_code(errc::operation_not_supported);
	return {};
}

void connection::write_span(std::span<const const_buffer> buffers, io_handler_t handler)
{
	size_t total = 0;
	for(const auto &value : buffers)
	{
		if( value.size() > std::numeric_limits<size_t>::max() - total )
		{
			std::move(handler) (
				make_system_error_code(std::errc::value_too_large), 0
			);
			return ;
		}
		total += value.size();
	}
	auto bytes = std::make_shared<std::vector<std::byte>>(total);
	size_t offset = 0;

	for(const auto &value : buffers)
	{
		if( value.size() != 0 )
			std::memcpy(bytes->data() + offset, value.data(), value.size());
		offset += value.size();
	}
	write(buffer(*bytes),
	[bytes, handler = std::move(handler)](error_code error, size_t size) mutable
	{
		RIWO_UNUSED(bytes);
		std::move(handler)(error, size);
	});
}

} //namespace riwo::http_exp
