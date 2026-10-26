// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_CLIENT_REQUEST_H
#define RIWO_HTTP_EXP_CLIENT_REQUEST_H

#include <riwo/http_exp/protocol/request_head.h>
#include <riwo/http_exp/client/config.h>

#include <riwo/http_exp/body/source.h>
#include <riwo/http_exp/body/spec.h>

namespace riwo::http_exp { namespace detail {
struct request_access;
} //namespace detail

// total is zero when the transfer length is unknown.
using progress_observer = std::function <
	void(uint64_t transferred, uint64_t total)
>;

struct request_options
{
	optional<proxy_policy> proxy;
	optional<redirect_policy> redirects;
	optional<retry_policy> retries;

	optional<bool> auto_decompression;
	progress_observer upload_progress;
	progress_observer download_progress;
};

class RIWO_HTTP_EXP_API request
{
	RIWO_DISABLE_COPY(request)

public:
	using method_t = http_exp::method;
	using url_t = riwo::url;

	request(method_t method, url_t url);
	~request();

	request(request &&other) noexcept;
	request &operator=(request &&other) noexcept;

public:
	request &set_connect_protocol(core_concepts::text_p<char> auto &&protocol);
	[[nodiscard]] optional<std::string> connect_protocol() const;

	request &set_upgrade_protocol(core_concepts::text_p<char> auto &&protocol);
	[[nodiscard]] optional<std::string> upgrade_protocol() const;

	// The buffer is borrowed until the send/fetch completion.
	request &set_body(const const_buffer &bytes);
	request &set_borrowed_body(const const_buffer &body);

	request &set_file(std::filesystem::path path);
	request &set_trailers(fields values);

	request &set_options(request_options value);
	request &set_proxy(proxy_policy value);

	request &set_redirects(redirect_policy value);
	request &set_retries(retry_policy value);

	request &set_auto_decompression(bool value = true);
	request &set_source(concepts::body_source_model_p auto &&source);

	request &on_upload_progress(progress_observer observer);
	request &on_download_progress(progress_observer observer);

public:
	[[nodiscard]] method_t method() const noexcept;
	[[nodiscard]] const url_t &url() const noexcept;

	[[nodiscard]] fields &headers() noexcept;
	[[nodiscard]] const fields &headers() const noexcept;

	[[nodiscard]] body_spec body_description() const;
	[[nodiscard]] bool has_body_source() const noexcept;

	[[nodiscard]] const request_options &options() const noexcept;

private:
	request &set_connect_protocol_impl(std::string protocol);
	request &set_upgrade_protocol_impl(std::string protocol);

	friend struct detail::request_access;
	class impl;
	std::unique_ptr<impl> m_impl {};
};

} //namespace riwo::http_exp
#include <riwo/http_exp/client/detail/request.h>


#endif //RIWO_HTTP_EXP_CLIENT_REQUEST_H
