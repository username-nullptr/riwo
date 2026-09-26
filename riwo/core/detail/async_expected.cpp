// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/core/async_expected.h>

namespace riwo::detail
{

error_code canonical_error(error_code error) noexcept
{
	if( not error )
		return {};

	std::string_view category_name(error.category().name());

	auto category_matches = [category_name](const error_code &sample) noexcept {
		return category_name == sample.category().name();
	};
	if( category_matches(asio::error::make_error_code(asio::error::operation_aborted)) )
	{
		return asio::error::make_error_code (
			static_cast<asio::error::basic_errors>(error.value())
		);
	}
	if( category_matches(asio::error::make_error_code(asio::error::eof)) )
	{
		return asio::error::make_error_code (
			static_cast<asio::error::misc_errors>(error.value())
		);
	}
	if( category_matches(asio::error::make_error_code(asio::error::host_not_found)) )
	{
		return asio::error::make_error_code (
			static_cast<asio::error::netdb_errors>(error.value())
		);
	}
	if( category_matches(asio::error::make_error_code(asio::error::service_not_found)) )
	{
		return asio::error::make_error_code (
			static_cast<asio::error::addrinfo_errors>(error.value())
		);
	}
	if( category_name == std::generic_category().name() )
		return error_code(std::error_code(error.value(), std::generic_category()));

	if( category_name == std::system_category().name() )
		return error_code(std::error_code(error.value(), std::system_category()));
	return error;
}

} //namespace riwo::detail
