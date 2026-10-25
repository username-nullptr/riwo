// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_PROTOCOL_DETAIL_MULTIPART_UTILS_H
#define RIWO_HTTP_EXP_PROTOCOL_DETAIL_MULTIPART_UTILS_H

#include <riwo/http_exp/protocol/detail/syntax.h>
#include <riwo/core/utils/string_tools.h>
#include <random>

namespace riwo::http_exp::detail
{

[[nodiscard]] inline bool valid_multipart_boundary(std::string_view value) noexcept
{
	return not value.empty() and value.size() <= 70 and value.back() != ' ' and
		value.find_first_of("\r\n") == std::string_view::npos;
}

[[nodiscard]] inline sys_expected<std::string> parse_multipart_boundary
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
		const auto equal = item.find('=');

		if( equal != std::string_view::npos and
			strtls::ascii_equal(trim_ows(item.substr(0, equal)), "boundary", false) )
		{
			auto value = trim_ows(item.substr(equal + 1));

			if( value.size() >= 2 and value.front() == '"' and value.back() == '"' )
				value = value.substr(1, value.size() - 2);

			if( not valid_multipart_boundary(value) )
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

[[nodiscard]] inline std::string make_multipart_boundary()
{
	static std::atomic<uint64_t> sequence {0};
	std::random_device random {};

	return std::format("riwo-{:016x}-{:016x}",
		static_cast<uint64_t>(random()), sequence.fetch_add(1, std::memory_order_relaxed)
	);
}

} //namespace riwo::http_exp::detail


#endif //RIWO_HTTP_EXP_PROTOCOL_DETAIL_MULTIPART_UTILS_H
