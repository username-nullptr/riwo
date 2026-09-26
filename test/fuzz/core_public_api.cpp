// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/core/algorithm/misc.h>
#include <riwo/core/url.h>
#include <riwo/core/value.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	if(size == 0 or size > RIWO_FUZZ_MAX_LENGTH)
		return 0;

	const std::string input(reinterpret_cast<const char*>(data), size);
	const size_t split = size_t(data[0]) % size;
	const auto left = std::string_view(input).substr(0, split);
	const auto right = std::string_view(input).substr(split);

	const char percent = static_cast<char>((data[0] % 94) + 33);
	const auto encoded = riwo::to_percent_encoding(input, {}, {}, percent);
	if(riwo::from_percent_encoding(encoded, percent) != input)
		std::abort();
	riwo::ignore_unused(riwo::from_percent_encoding(input, percent));
	riwo::ignore_unused(riwo::wildcard_match(left, right));

	riwo::value value(input);
	const size_t base = size_t(data[0] % 35) + 2;
	riwo::ignore_unused(value.to_bool(base));
	riwo::ignore_unused(value.to_int(base));
	riwo::ignore_unused(value.to_uint(base));
	riwo::ignore_unused(value.to_long(base));
	riwo::ignore_unused(value.to_ulong(base));
	riwo::ignore_unused(value.to_float());
	riwo::ignore_unused(value.to_double());
	riwo::ignore_unused(value.to_ldouble());
	riwo::ignore_unused(value.is_alpha());
	riwo::ignore_unused(value.is_digit());
	riwo::ignore_unused(value.is_rlnum());
	riwo::ignore_unused(value.is_alnum());
	riwo::ignore_unused(value.is_ascii());
	value.set("{}:{}", left, right);

	riwo::url parsed(input);
	parsed.set_address(std::string(left));
	parsed.set_port(static_cast<uint16_t>((size > 1 ? data[1] : data[0]) * 257U));
	try {
		parsed.set_path(right);
	}
	catch(const riwo::invalid_argument&) {}
	try {
		parsed.set_fragment(left);
	}
	catch(const riwo::invalid_argument&) {}
	parsed.set_parameter(std::string(left), std::string(right));
	riwo::ignore_unused(parsed.parameter(std::string(left)));
	riwo::ignore_unused(parsed.encoded_path());
	riwo::ignore_unused(parsed.encoded_query());
	const auto serialized = parsed.to_string();
	if(parsed.is_valid())
	{
		riwo::url reparsed(serialized);
		if(not reparsed.is_valid() or reparsed.to_string() != serialized)
			std::abort();
	}
	parsed.clear_fragment().unset_parameter(std::string(left));
	try {
		riwo::ignore_unused(riwo::url::resolve(parsed, right));
	}
	catch(const riwo::invalid_argument&) {}
	return 0;
}
