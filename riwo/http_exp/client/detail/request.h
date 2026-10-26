// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_CLIENT_DETAIL_REQUEST_H
#define RIWO_HTTP_EXP_CLIENT_DETAIL_REQUEST_H

#include <riwo/http_exp/body/detail/source.h>
#include <riwo/core/utils/string_tools.h>

namespace riwo::http_exp
{

class RIWO_HTTP_EXP_API request::impl
{
public:
	impl(http_exp::method request_method, riwo::url request_url);

	request_head m_head {};
	body_spec m_body_spec = body_spec::empty();

	optional<const_buffer> m_body {};
	detail::body_source_ptr m_source {};
	std::filesystem::path m_file {};

	fields m_trailers {};
	request_options m_options {};

	size_t m_retry_attempt = 1;
	size_t m_redirect_count = 0;
};

request &request::set_connect_protocol(core_concepts::text_p<char> auto &&protocol)
{
	return set_connect_protocol_impl (
		strtls::to_string(std::forward<decltype(protocol)>(protocol))
	);
}

request &request::set_upgrade_protocol(core_concepts::text_p<char> auto &&protocol)
{
	return set_upgrade_protocol_impl (
		strtls::to_string(std::forward<decltype(protocol)>(protocol))
	);
}

request &request::set_source(concepts::body_source_model_p auto &&source_value)
{
	auto value = detail::make_body_source (
		std::forward<decltype(source_value)>(source_value)
	);
	const auto size = value->size();

	m_impl->m_body.reset();
	m_impl->m_source = std::move(value);
	m_impl->m_file.clear();

	m_impl->m_body_spec = size ?
		body_spec::known(*size) : body_spec::streaming();

	m_impl->m_trailers = {};
	return *this;
}

} //namespace riwo::http_exp


#endif //RIWO_HTTP_EXP_CLIENT_DETAIL_REQUEST_H
