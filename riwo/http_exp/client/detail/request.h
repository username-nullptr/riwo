// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_CLIENT_DETAIL_REQUEST_H
#define RIWO_HTTP_EXP_CLIENT_DETAIL_REQUEST_H

#include <riwo/http_exp/body/detail/source.h>

namespace riwo::http_exp
{

class request::impl
{
public:
	impl(http_exp::method request_method, riwo::url request_url) :
		m_head {std::move(request_method), std::move(request_url)}
	{
	}

	http_exp::request_head m_head;
	http_exp::body_spec m_body_spec = http_exp::body_spec::empty();
	optional<const_buffer> m_body;
	std::shared_ptr<detail::body_source> m_source;
	std::filesystem::path m_file;
	http_exp::fields m_trailers;
	http_exp::request_options m_options;
	size_t m_retry_attempt = 1;
	size_t m_redirect_count = 0;
};

template <typename Source>
requires concepts::body_source_model<std::remove_cvref_t<Source>>
request &request::source(Source &&source_value)
{
	auto value = detail::make_body_source(std::forward<Source>(source_value));
	const auto size = value->size();
	m_impl->m_body.reset();
	m_impl->m_source = std::move(value);
	m_impl->m_file.clear();
	m_impl->m_body_spec = size ? http_exp::body_spec::known(*size) :
		http_exp::body_spec::streaming();
	m_impl->m_trailers = {};
	return *this;
}

} //namespace riwo::http_exp

#endif //RIWO_HTTP_EXP_CLIENT_DETAIL_REQUEST_H
