// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "request_access.h"

namespace riwo::http_exp::detail
{

body_source_ptr request_access::source(const request &value) noexcept
{
	return value.m_impl->m_source;
}

optional<const_buffer> request_access::body(const request &value) noexcept
{
	return value.m_impl->m_body;
}

fields request_access::trailers(const request &value)
{
	return value.m_impl->m_trailers;
}

size_t request_access::retry_attempt(const request &value) noexcept
{
	return value.m_impl->m_retry_attempt;
}

size_t request_access::redirect_count(const request &value) noexcept
{
	return value.m_impl->m_redirect_count;
}

client_config request_access::effective_config(const request &value, const client_config &base)
{
	auto result = base;
	const auto &options = value.m_impl->m_options;

	if( options.proxy )
		result.proxy = *options.proxy;

	if( options.redirects )
		result.redirects = *options.redirects;

	if( options.retries )
		result.retries = *options.retries;

	if( options.auto_decompression )
		result.auto_decompression = *options.auto_decompression;
	return result;
}

progress_observer request_access::upload_progress(const request &value)
{
	return value.m_impl->m_options.upload_progress;
}

progress_observer request_access::download_progress(const request &value)
{
	return value.m_impl->m_options.download_progress;
}

void request_access::increment_retry(request &value) noexcept
{
	++value.m_impl->m_retry_attempt;
}

void request_access::increment_redirect(request &value) noexcept
{
	++value.m_impl->m_redirect_count;
}

error_code request_access::prepare(request &value) noexcept
{
	if( value.m_impl->m_file.empty() )
		return {};
	try {
		file_body_source source(value.m_impl->m_file);
		const auto size = source.size();

		value.m_impl->m_body.reset();
		value.m_impl->m_source = make_body_source(std::move(source));

		value.m_impl->m_body_spec = body_spec::known (
			*size, value.m_impl->m_body_spec.trailers()
		);
		value.m_impl->m_file.clear();
		return {};
	}
	catch(...) {
		return exception_error(std::current_exception());
	}
}

request request_access::clone(const request &value, const optional<url> &target,
	const optional<method> &request_method, bool drop_body, bool strip_credentials)
{
	request result (
		request_method ? *request_method : value.method(),
		target ? *target : value.url()
	);
	result.m_impl->m_head.headers = value.m_impl->m_head.headers;
	result.m_impl->m_head.connect_protocol = value.m_impl->m_head.connect_protocol;
	result.m_impl->m_head.upgrade_protocol = value.m_impl->m_head.upgrade_protocol;

	if( strip_credentials )
	{
		result.m_impl->m_head.headers.erase("Authorization");
		result.m_impl->m_head.headers.erase("Proxy-Authorization");
		result.m_impl->m_head.headers.erase("Cookie");
	}
	result.m_impl->m_retry_attempt = value.m_impl->m_retry_attempt;
	result.m_impl->m_redirect_count = value.m_impl->m_redirect_count;
	result.m_impl->m_options = value.m_impl->m_options;

	if( not drop_body )
	{
		result.m_impl->m_body_spec = value.m_impl->m_body_spec;
		result.m_impl->m_body = value.m_impl->m_body;
		result.m_impl->m_source = value.m_impl->m_source;
		result.m_impl->m_trailers = value.m_impl->m_trailers;
	}
	return result;
}

} //namespace riwo::http_exp::detail
