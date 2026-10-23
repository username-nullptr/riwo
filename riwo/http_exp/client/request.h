// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_CLIENT_REQUEST_H
#define RIWO_HTTP_EXP_CLIENT_REQUEST_H

#include <riwo/http_exp/body/spec.h>
#include <riwo/http_exp/body/source.h>
#include <riwo/http_exp/client/config.h>
#include <riwo/http_exp/protocol/request_head.h>

namespace riwo::http_exp::detail
{
struct request_access;
}

namespace riwo::http_exp
{

// total is zero when the transfer length is unknown.
using progress_observer = std::function<void(
	uint64_t transferred, uint64_t total)>;

struct request_options
{
	optional<proxy_policy> proxy;
	optional<redirect_policy> redirects;
	optional<retry_policy> retries;
	optional<bool> automatic_decompression;
	progress_observer upload_progress;
	progress_observer download_progress;
};

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
	request &connect_protocol(std::string protocol);
	[[nodiscard]] const optional<std::string> &connect_protocol() const noexcept;
	request &upgrade_protocol(std::string protocol);
	[[nodiscard]] const optional<std::string> &upgrade_protocol() const noexcept;

	// The buffer is borrowed until the send/fetch completion.
	request &body(const const_buffer &bytes);
	request &borrowed_body(const_buffer body);
	request &file(std::filesystem::path path);
	request &trailers(fields values);
	request &options(request_options value);
	request &proxy(proxy_policy value);
	request &redirects(redirect_policy value);
	request &retries(retry_policy value);
	request &automatic_decompression(bool value = true);
	request &on_upload_progress(progress_observer observer);
	request &on_download_progress(progress_observer observer);
	[[nodiscard]] const request_options &options() const noexcept;

	template <typename Source>
	requires concepts::body_source_model<std::remove_cvref_t<Source>>
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
