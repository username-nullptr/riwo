// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_EXP_HTTP_CLIENT_DETAIL_REQUEST_ACCESS_H
#define RIWO_EXP_HTTP_CLIENT_DETAIL_REQUEST_ACCESS_H

#include <riwo/experimental/http/client/request.h>

namespace riwo::experimental::http::detail
{

// Private bridge owned by the request detail layer. Client workflows consume
// this interface; request.h only grants friendship and never includes it.
struct RIWO_DECL_HIDDEN request_access
{
	[[nodiscard]] static body_source_ptr source(const request &value) noexcept;
	[[nodiscard]] static optional<const_buffer> body(const request &value) noexcept;
	[[nodiscard]] static fields trailers(const request &value);

	[[nodiscard]] static size_t retry_attempt(const request &value) noexcept;
	[[nodiscard]] static size_t redirect_count(const request &value) noexcept;

	[[nodiscard]] static client_config effective_config(const request &value, const client_config &base);

	[[nodiscard]] static progress_observer upload_progress(const request &value);
	[[nodiscard]] static progress_observer download_progress(const request &value);

	static void increment_retry(request &value) noexcept;
	static void increment_redirect(request &value) noexcept;

	[[nodiscard]] static error_code prepare(request &value) noexcept;

	[[nodiscard]] static request clone(const request &value,
		const optional<url> &target = {}, const optional<method> &request_method = {},
		bool drop_body = false, bool strip_credentials = false
	);
};

} //namespace riwo::experimental::http::detail


#endif //RIWO_EXP_HTTP_CLIENT_DETAIL_REQUEST_ACCESS_H
