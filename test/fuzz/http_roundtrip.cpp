// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/http/protocol/utils/client/generator.h>
#include <riwo/http/protocol/utils/client/parser.h>
#include <riwo/http/protocol/utils/server/generator.h>
#include <riwo/http/protocol/utils/server/parser.h>

namespace
{

[[nodiscard]] riwo::const_buffer buffer(std::string_view value) noexcept
{
	return {value.data(), value.size()};
}

[[nodiscard]] std::string header_value(const uint8_t *data, size_t size)
{
	static constexpr char hex[] = "0123456789abcdef";
	const auto count = std::min<size_t>(size, 256);
	std::string result;
	result.reserve(count * 2);
	for(size_t index = 0; index < count; ++index)
	{
		result.push_back(hex[data[index] >> 4U]);
		result.push_back(hex[data[index] & 0x0fU]);
	}
	return result;
}

template <typename Parser>
void append_fragmented(Parser &parser, std::string_view wire,
	const uint8_t *control, size_t control_size)
{
	size_t offset = 0;
	size_t control_offset = 0;
	while(offset < wire.size())
	{
		const auto requested = size_t(control[control_offset++ % control_size] % 31U) + 1;
		const auto available = std::min(requested, wire.size() - offset);
		auto result = parser.append(buffer(wire.substr(offset, available)));
		if(not result)
			std::abort();
		offset += available;
	}
}

} //namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	if(size < 4 or size > RIWO_FUZZ_MAX_LENGTH)
		return 0;

	using namespace riwo::http;
	static constexpr std::array request_methods {
		method::post, method::put, method::patch,
	};
	static constexpr std::array response_statuses {
		status::ok, status::created, status::bad_request,
		status::internal_server_error,
	};
	const auto request_method = request_methods[data[0] % request_methods.size()];
	const auto response_status = response_statuses[data[1] % response_statuses.size()];
	const std::string body(reinterpret_cast<const char*>(data + 4), size - 4);
	const auto marker = header_value(data + 2, size - 2);

	request_arg argument;
	argument.set_header("X-Riwo-Fuzz", marker)
		.set_cookie("fuzz", marker);
	client_generator request(version::v11,
		riwo::url("http://example.test/fuzz?q=roundtrip"), argument,
		(data[2] & 1U) != 0 ? request_target_form::absolute :
			request_target_form::origin);
	std::string request_wire = request.header_data(request_method, body.size());
	request_wire += request.body_data(buffer(body));
	if(request.pro_state() != generator_state::finish)
		std::abort();

	server_parser contiguous_request(size_t(data[2] % 32U) + 1);
	auto request_result = contiguous_request.append(buffer(request_wire));
	if(not request_result or contiguous_request.method() != request_method or
		contiguous_request.header("X-Riwo-Fuzz").value_or("").to_string() != marker or
		contiguous_request.cookie("fuzz").value_or("").to_string() != marker or
		contiguous_request.take_body() != body)
		std::abort();

	server_parser fragmented_request(size_t(data[3] % 32U) + 1);
	append_fragmented(fragmented_request, request_wire, data, size);
	if(fragmented_request.method() != request_method or
		fragmented_request.take_body() != body)
		std::abort();
	fragmented_request.reset();
	request_result = fragmented_request.append(buffer(request_wire));
	if(not request_result or fragmented_request.take_body() != body)
		std::abort();

	server_generator response(version::v11);
	response.set_status(response_status).set_header("X-Riwo-Fuzz", marker);
	std::string response_wire = response.header_data(body.size(), method::get);
	response_wire += response.body_data(buffer(body));
	if(response.pro_state() != generator_state::finish)
		std::abort();

	client_parser contiguous_response(size_t(data[2] % 32U) + 1);
	contiguous_response.set_request_method(method::get);
	auto response_result = contiguous_response.append(buffer(response_wire));
	if(not response_result or contiguous_response.status() != response_status or
		contiguous_response.header("X-Riwo-Fuzz").value_or("").to_string() != marker or
		contiguous_response.take_body() != body)
		std::abort();

	client_parser fragmented_response(size_t(data[3] % 32U) + 1);
	fragmented_response.set_request_method(method::get);
	append_fragmented(fragmented_response, response_wire, data, size);
	if(fragmented_response.status() != response_status or
		fragmented_response.take_body() != body)
		std::abort();
	fragmented_response.reset().set_request_method(method::get);
	response_result = fragmented_response.append(buffer(response_wire));
	if(not response_result or fragmented_response.take_body() != body)
		std::abort();
	return 0;
}
