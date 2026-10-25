// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "range.h"
#include "detail/multipart_utils.h"
#include "detail/syntax.h"

namespace riwo::http_exp { namespace
{

[[nodiscard]] error_code invalid() noexcept
{
	return make_system_error_code(std::errc::invalid_argument);
}

[[nodiscard]] sys_expected<uint64_t> decimal(std::string_view value) noexcept
{
	if( value.empty() )
		return sys_unexpected(invalid());

	uint64_t result = 0;
	const auto parsed = std::from_chars (
		value.data(), value.data() + value.size(), result
	);
	if( parsed.ec != std::errc{} or parsed.ptr != value.data() + value.size() )
	{
		return sys_unexpected(parsed.ec == std::errc::result_out_of_range ?
			make_system_error_code(std::errc::result_out_of_range) : invalid()
		);
	}
	return result;
}

} //namespace

uint64_t content_range::length() const noexcept
{
	return satisfied and last >= first ? last - first + 1 : 0;
}

sys_expected<range_specifier> parse_range_header(std::string_view value) noexcept
try
{
	value = detail::trim_ows(value);
	const auto equal = value.find('=');

	if( equal == std::string_view::npos )
		return sys_unexpected(invalid());

	const auto unit = detail::trim_ows(value.substr(0, equal));
	if( unit.empty() or std::ranges::any_of(unit,
		[](unsigned char ch) {
			return not detail::ascii_token_character(ch);
		}) )
		return sys_unexpected(invalid());

	range_specifier result {strtls::to_lower(unit), {}};
	value.remove_prefix(equal + 1);
	for(;;)
	{
		const auto comma = value.find(',');
		const auto item = detail::trim_ows(value.substr(0, comma));
		const auto dash = item.find('-');

		if( item.empty() or dash == std::string_view::npos or
			item.find('-', dash + 1) != std::string_view::npos )
			return sys_unexpected(invalid());

		const auto first = detail::trim_ows(item.substr(0, dash));
		const auto last = detail::trim_ows(item.substr(dash + 1));

		byte_range_spec spec {};
		if( first.empty() )
		{
			auto parsed = decimal(last);
			if( not parsed )
				return sys_unexpected(parsed.error());

			spec.type = byte_range_spec::form::suffix;
			spec.first = *parsed;
		}
		else
		{
			auto parsed_first = decimal(first);
			if( not parsed_first )
				return sys_unexpected(parsed_first.error());

			spec.first = *parsed_first;
			if( last.empty() )
				spec.type = byte_range_spec::form::open_ended;
			else
			{
				auto parsed_last = decimal(last);
				if( not parsed_last )
					return sys_unexpected(parsed_last.error());

				if( *parsed_last < spec.first )
					return sys_unexpected(invalid());

				spec.type = byte_range_spec::form::closed;
				spec.last = *parsed_last;
			}
		}
		result.ranges.emplace_back(spec);
		if( comma == std::string_view::npos )
			break;

		value.remove_prefix(comma + 1);
	}
	return result;
}
catch(...) {
	return sys_unexpected(exception_error(std::current_exception()));
}

std::vector<resolved_byte_range> resolve_byte_ranges
(const range_specifier &specifier, uint64_t complete_length) noexcept
{
	std::vector<resolved_byte_range> result {};
	if( specifier.unit != "bytes" or complete_length == 0 )
		return result;
	try {
		for(const auto &spec : specifier.ranges)
		{
			resolved_byte_range range {};
			if( spec.type == byte_range_spec::form::suffix )
			{
				if( spec.first == 0 )
					continue;

				range.length = std::min(spec.first, complete_length);
				range.offset = complete_length - range.length;
			}
			else
			{
				if( spec.first >= complete_length )
					continue;

				range.offset = spec.first;
				const auto last = spec.type == byte_range_spec::form::open_ended ?
					complete_length - 1 : std::min(spec.last, complete_length - 1);

				range.length = last - range.offset + 1;
			}
			result.emplace_back(range);
		}
	}
	catch(...) {
		result.clear();
	}
	return result;
}

sys_expected<content_range> parse_content_range(std::string_view value) noexcept
try
{
	value = detail::trim_ows(value);
	const auto space = value.find_first_of(" \t");

	if( space == std::string_view::npos )
		return sys_unexpected(invalid());

	const auto unit = value.substr(0, space);

	if( unit.empty() or std::ranges::any_of(unit,
		[](unsigned char ch) {
			return not detail::ascii_token_character(ch);
		}) )
		return sys_unexpected(invalid());

	value = detail::trim_ows(value.substr(space + 1));

	content_range result {};
	result.unit = strtls::to_lower(unit);

	if( value.starts_with("*/") )
	{
		auto complete = decimal(value.substr(2));
		if( not complete )
			return sys_unexpected(complete.error());

		result.complete_length = *complete;
		return result;
	}
	const auto dash = value.find('-');
	const auto slash = value.find('/',
		dash == std::string_view::npos ? 0 : dash + 1
	);
	if( dash == std::string_view::npos or slash == std::string_view::npos )
		return sys_unexpected(invalid());

	auto first = decimal(value.substr(0, dash));
	auto last = decimal(value.substr(dash + 1, slash - dash - 1));

	if( not first )
		return sys_unexpected(first.error());

	if( not last )
		return sys_unexpected(last.error());

	if( *last < *first )
		return sys_unexpected(invalid());

	result.satisfied = true;
	result.first = *first;
	result.last = *last;

	const auto complete_text = value.substr(slash + 1);
	if( complete_text != "*" )
	{
		auto complete = decimal(complete_text);
		if( not complete )
			return sys_unexpected(complete.error());

		if( *complete <= result.last )
			return sys_unexpected(invalid());

		result.complete_length = *complete;
	}
	return result;
}
catch(...) {
	return sys_unexpected(exception_error(std::current_exception()));
}

std::string format_content_range(const resolved_byte_range &range, uint64_t complete_length)
{
	if( range.length == 0 or range.offset >= complete_length or
		range.length > complete_length - range.offset )
		return {};

	return std::format("bytes {}-{}/{}", range.offset,
		range.offset + range.length - 1, complete_length
	);
}

std::string format_unsatisfied_content_range(uint64_t complete_length)
{
	return std::format("bytes */{}", complete_length);
}

sys_expected<std::string> parse_multipart_byte_ranges_boundary(std::string_view content_type) noexcept
{
	return detail::parse_multipart_boundary(content_type, "multipart/byteranges");
}

} //namespace riwo::http_exp
