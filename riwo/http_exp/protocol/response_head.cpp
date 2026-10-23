// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "response_head.h"

namespace riwo::http_exp { namespace
{

[[nodiscard]] optional<uint64_t> parse_content_length(
	std::string_view value) noexcept
{
	uint64_t result = 0;
	const auto parsed = std::from_chars(
		value.data(), value.data() + value.size(), result);
	if( parsed.ec != std::errc{} or
		parsed.ptr != value.data() + value.size() )
		return {};
	return result;
}

} //namespace

optional<std::string_view>
response_head::header(std::string_view name) const noexcept {
	return headers.find_first(name);
}

bool response_head::has_header(std::string_view name) const noexcept {
	return headers.find_first(name).has_value();
}

bool response_head::contains_header(std::string_view name) const noexcept {
	return has_header(name);
}

response_head &response_head::set_header(
	std::string name, riwo::value value)
{
	headers.set(std::move(name), std::move(value));
	return *this;
}

response_head &response_head::add_header(
	std::string name, riwo::value value)
{
	headers.add(std::move(name), std::move(value));
	return *this;
}

response_head &response_head::erase_header(std::string_view name) noexcept
{
	headers.erase(name);
	return *this;
}

response_head &response_head::unset_header(std::string_view name) noexcept {
	return erase_header(name);
}

optional<std::string_view> response_head::content_type() const noexcept {
	return header("Content-Type");
}

optional<uint64_t> response_head::content_length() const noexcept
{
	const auto value = header("Content-Length");
	return value ? parse_content_length(*value) : optional<uint64_t>{};
}

optional<std::string_view> response_head::location() const noexcept {
	return header("Location");
}

bool response_head::is_informational() const noexcept {
	return status.code() < 200;
}

bool response_head::is_success() const noexcept {
	return status.code() >= 200 and status.code() < 300;
}

bool response_head::is_redirection() const noexcept {
	return status.code() >= 300 and status.code() < 400;
}

bool response_head::is_client_error() const noexcept {
	return status.code() >= 400 and status.code() < 500;
}

bool response_head::is_server_error() const noexcept {
	return status.code() >= 500;
}

bool response_head::is_error() const noexcept {
	return status.code() >= 400;
}

bool response_head::allows_body() const noexcept {
	return status.allows_body();
}

} //namespace riwo::http_exp
