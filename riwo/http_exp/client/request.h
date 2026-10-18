// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_CLIENT_REQUEST_H
#define RIWO_HTTP_EXP_CLIENT_REQUEST_H

#include <riwo/http_exp/body/source.h>
#include <riwo/http_exp/message.h>

namespace riwo::http_exp
{

class RIWO_HTTP_EXP_API request
{
	RIWO_DISABLE_COPY(request)

public:
	request(http_exp::method request_method, riwo::url request_url);
	~request();

	request(request &&) noexcept;
	request &operator=(request &&) noexcept;

	[[nodiscard]] http_exp::method method() const noexcept;
	[[nodiscard]] const riwo::url &url() const noexcept;
	[[nodiscard]] http_exp::fields &headers() noexcept;
	[[nodiscard]] const http_exp::fields &headers() const noexcept;

	template <typename Bytes>
	request &body(Bytes &&bytes)
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
		owned_body_.assign(view.begin(), view.end());
		body_owner_.reset();
		borrowed_body_ = const_buffer(nullptr, 0);
		file_.clear();
		body_spec_ = http_exp::body_spec::known(owned_body_.size());
		trailers_ = {};
		return *this;
	}

	request &borrowed_body(const_buffer body);
	request &file(std::filesystem::path path);
	request &trailers(fields values);

	template <typename Source>
	requires body_source_model<std::remove_cvref_t<Source>>
	request &source(Source &&source)
	{
		using source_type = std::remove_cvref_t<Source>;
		auto holder = std::make_shared<source_type>(std::forward<Source>(source));
		const auto size = holder->size();
		body_owner_ = std::move(holder);
		owned_body_.clear();
		borrowed_body_ = const_buffer(nullptr, 0);
		file_.clear();
		body_spec_ = size ? http_exp::body_spec::known(*size) :
			http_exp::body_spec::streaming();
		trailers_ = {};
		return *this;
	}

	[[nodiscard]] const body_spec &body_description() const noexcept;
	[[nodiscard]] bool has_body_source() const noexcept;

private:
	http_exp::method method_;
	riwo::url url_;
	http_exp::fields headers_;
	http_exp::body_spec body_spec_ = http_exp::body_spec::empty();
	std::vector<std::byte> owned_body_;
	const_buffer borrowed_body_ {};
	std::filesystem::path file_;
	std::shared_ptr<void> body_owner_;
	http_exp::fields trailers_;
};

} //namespace riwo::http_exp

#endif //RIWO_HTTP_EXP_CLIENT_REQUEST_H
