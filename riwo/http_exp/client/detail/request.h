// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_CLIENT_DETAIL_REQUEST_H
#define RIWO_HTTP_EXP_CLIENT_DETAIL_REQUEST_H

#include <riwo/http_exp/body/detail/source.h>

namespace riwo::http_exp
{

template <typename Bytes>
request &request::body(Bytes &&bytes)
	requires requires(Bytes &&value) {
		std::data(value);
		std::size(value);
	}
{
	using value_type = std::remove_cv_t<std::remove_reference_t<
		decltype(*std::data(bytes))>>;
	static_assert(std::is_trivially_copyable_v<value_type>);
	const auto values = std::span(std::data(bytes), std::size(bytes));
	const auto view = std::as_bytes(values);
	std::vector<std::byte> copy(view.begin(), view.end());
	source_ = detail::make_body_source(memory_body_source(std::move(copy)));
	file_.clear();
	body_spec_ = http_exp::body_spec::known(view.size());
	trailers_ = {};
	return *this;
}

template <typename Source>
requires body_source_model<std::remove_cvref_t<Source>>
request &request::source(Source &&source_value)
{
	auto value = detail::make_body_source(std::forward<Source>(source_value));
	const auto size = value->size();
	source_ = std::move(value);
	file_.clear();
	body_spec_ = size ? http_exp::body_spec::known(*size) :
		http_exp::body_spec::streaming();
	trailers_ = {};
	return *this;
}

} //namespace riwo::http_exp

#endif //RIWO_HTTP_EXP_CLIENT_DETAIL_REQUEST_H
