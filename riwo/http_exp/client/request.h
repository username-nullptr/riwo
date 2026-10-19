// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_CLIENT_REQUEST_H
#define RIWO_HTTP_EXP_CLIENT_REQUEST_H

#include <riwo/http_exp/body/source.h>
#include <riwo/http_exp/protocol/request_head.h>

namespace riwo::http_exp::detail
{
struct request_access;
}

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

	// The buffer is borrowed until the send/fetch completion.
	request &body(const const_buffer &bytes);
	request &borrowed_body(const_buffer body);
	request &file(std::filesystem::path path);
	request &trailers(fields values);

	template <typename Source>
	requires body_source_model<std::remove_cvref_t<Source>>
	request &source(Source &&source);

	[[nodiscard]] const body_spec &body_description() const noexcept;
	[[nodiscard]] bool has_body_source() const noexcept;

private:
	friend struct detail::request_access;
	class impl;
	std::unique_ptr<impl> m_impl;
};

} //namespace riwo::http_exp

#include <riwo/http_exp/client/detail/request.h>

#endif //RIWO_HTTP_EXP_CLIENT_REQUEST_H
