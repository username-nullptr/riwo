// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "multipart_utils.h"

namespace riwo::experimental::http::detail
{

bool valid_multipart_boundary(std::string_view value, bool quoted) noexcept
{
	if( value.empty() or value.size() > 70 or value.back() == ' ' )
		return false;

	return std::ranges::all_of(value, [quoted](unsigned char byte)
	{
		const bool boundary_character =
			(byte >= '0' and byte <= '9') or
			(byte >= 'A' and byte <= 'Z') or
			(byte >= 'a' and byte <= 'z') or
			std::string_view("'()+_,-./:=? ").find(static_cast<char>(byte)) != std::string_view::npos;

		return boundary_character and (quoted or ascii_token_character(byte));
	});
}

sys_expected<std::string> parse_multipart_boundary
(std::string_view content_type, std::string_view expected_media_type) noexcept
try
{
	const auto semicolon = content_type.find(';');

	if( not strtls::ascii_equal(trim_ows(content_type.substr(0, semicolon)), expected_media_type, false) )
		return sys_unexpected(make_system_error_code(std::errc::invalid_argument));

	auto rest = semicolon == std::string_view::npos ?
		std::string_view{} : content_type.substr(semicolon + 1);

	while( not rest.empty() )
	{
		const auto next = rest.find(';');
		const auto item = trim_ows(rest.substr(0, next));

		if( const auto equal = item.find('=');
			equal != std::string_view::npos and
			strtls::ascii_equal(trim_ows(item.substr(0, equal)), "boundary", false) )
		{
			auto value = trim_ows(item.substr(equal + 1));
			bool quoted = false;

			if( not value.empty() and value.front() == '"' )
			{
				if( value.size() < 2 or value.back() != '"' )
					return sys_unexpected(make_system_error_code(std::errc::invalid_argument));

				value = value.substr(1, value.size() - 2);
				quoted = true;
			}

			if( not valid_multipart_boundary(value, quoted) )
				return sys_unexpected(make_system_error_code(std::errc::invalid_argument));

			return std::string(value);
		}
		if( next == std::string_view::npos )
			break;

		rest.remove_prefix(next + 1);
	}
	return sys_unexpected(make_system_error_code(std::errc::invalid_argument));
}
catch(...) {
	return sys_unexpected(exception_error(std::current_exception()));
}

std::string make_multipart_boundary()
{
	static std::atomic<uint64_t> sequence {0};
	std::random_device random {};

	return std::format("riwo-{:016x}-{:016x}",
		static_cast<uint64_t>(random()), sequence.fetch_add(1, std::memory_order_relaxed)
	);
}

} //namespace riwo::experimental::http::detail
