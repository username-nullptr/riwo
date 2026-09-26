// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/http/protocol/utils.h>
#include <riwo/http/protocol/utils/core/range.h>
#include <riwo/http/utils/connection.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	if(size == 0 or size > RIWO_FUZZ_MAX_LENGTH)
		return 0;

	const std::string input(reinterpret_cast<const char*>(data), size);
	const size_t split = size_t(data[0]) % size;
	const auto left = std::string_view(input).substr(0, split);
	const auto right = std::string_view(input).substr(split);

	riwo::http::endpoint endpoint;
	if(endpoint.from_string(input))
	{
		riwo::http::endpoint reparsed;
		if(not reparsed.from_string(endpoint.to_string()) or
			reparsed.to_string() != endpoint.to_string())
			std::abort();
	}
	const auto parsed_method = riwo::http::method::from_string(input);
	if(parsed_method == riwo::http::method::none)
	{
		bool rejected = false;
		try {
			riwo::ignore_unused(riwo::http::method::from_string(input, true));
		}
		catch(const riwo::invalid_argument&) {
			rejected = true;
		}
		if(not rejected)
			std::abort();
	}
	else if(riwo::http::method::from_string(input, true) != parsed_method)
	{
		std::abort();
	}
	else if(riwo::http::method::from_string(
		riwo::http::method::string(parsed_method), true) != parsed_method)
	{
		std::abort();
	}
	const auto ranges = riwo::http::parse_range_header(input);
	if(ranges)
	{
		const size_t complete_length = 1 +
			(size > 1 ? (size_t(data[0]) << 8U) | data[1] : data[0]);
		for(const auto &range : riwo::http::resolve_byte_ranges(
			*ranges, complete_length))
		{
			if(range.total == 0 or range.begin >= complete_length or
				range.total > complete_length - range.begin)
				std::abort();
		}
	}
	const auto content_range = riwo::http::parse_content_range(input);
	if(content_range and content_range->satisfied)
	{
		if(content_range->length() == 0 or
			content_range->length() != content_range->last - content_range->first + 1)
			std::abort();
		if(content_range->complete_length)
		{
			const auto formatted = riwo::http::format_content_range(
				{content_range->first, content_range->length()},
				*content_range->complete_length);
			const auto reparsed = riwo::http::parse_content_range(formatted);
			if(not reparsed or reparsed->first != content_range->first or
				reparsed->last != content_range->last or
				reparsed->complete_length != content_range->complete_length)
				std::abort();
		}
	}
	riwo::ignore_unused(riwo::http::parse_multipart_byte_ranges_boundary(input));
	riwo::ignore_unused(riwo::http::parse_entity_tag(input));
	riwo::ignore_unused(riwo::http::parse_http_date(input));
	riwo::ignore_unused(riwo::http::content_coding_quality(input, right));
	riwo::ignore_unused(riwo::http::form_data_boundary(input));
	riwo::ignore_unused(riwo::http::parse_multipart_form_data(left, right));

	riwo::http::headers headers;
	headers[riwo::http::header::connection] = std::string(left);
	headers[riwo::http::header::upgrade] = std::string(right);
	headers[riwo::http::header::if_match] = input;
	riwo::ignore_unused(riwo::http::header_has_token(
		headers, riwo::http::header::connection, right));
	riwo::ignore_unused(riwo::http::is_upgrade_request(headers));
	riwo::ignore_unused(riwo::http::is_upgrade_response(
		riwo::http::status::switching_protocols, headers));
	riwo::ignore_unused(riwo::http::upgrade_protocol(headers));
	riwo::ignore_unused(riwo::http::evaluate_preconditions(
		riwo::http::method::get, headers, headers, (data[0] & 1U) != 0));

	riwo::http::cookie cookie(input);
	cookie.set_domain(std::string(left)).set_path(std::string(right));
	cookie.set_same_site(input).set_priority(std::string(left));
	cookie.set_expires(size).set_max_age(size).set_size(size);
	cookie.set_http_only((data[0] & 1U) != 0).set_secure((data[0] & 2U) != 0);
	cookie.set_attribute(std::string(left), std::string(right));
	riwo::ignore_unused(cookie.attribute(std::string(left)));
	riwo::ignore_unused(cookie.attributes());
	cookie.unset_domain().unset_path().unset_same_site().unset_priority();
	cookie.unset_expires().unset_max_age().unset_size();
	cookie.unset_http_only().unset_secure().unset_attribute(std::string(left));
	return 0;
}
