// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "nghttp2_compat.h"

#if RIWO_EXP_HTTP_HTTP2_SUPPORT

namespace riwo::experimental::http::v2::detail::nghttp2_api
{

#if RIWO_EXP_HTTP_NGHTTP2_USE_MODERN_API

ssize_type_t session_mem_recv(nghttp2_session *session, const uint8_t *input, size_t size)
{
	return nghttp2_session_mem_recv2(session, input, size);
}

ssize_type_t session_mem_send(nghttp2_session *session, const uint8_t **output)
{
	return nghttp2_session_mem_send2(session, output);
}

int32_t submit_request(nghttp2_session *session, const nghttp2_priority_spec *priority,
	const nghttp2_nv *headers, size_t count, const data_provider_t *provider, void *user_data)
{
	return nghttp2_submit_request2 (
		session, priority, headers, count, provider, user_data
	);
}

int submit_response(nghttp2_session *session, int32_t stream_id,
	const nghttp2_nv *headers, size_t count, const data_provider_t *provider)
{
	return nghttp2_submit_response2 (
		session, stream_id, headers, count, provider
	);
}

#else //RIWO_EXP_HTTP_NGHTTP2_USE_MODERN_API

ssize_type_t session_mem_recv(nghttp2_session *session, const uint8_t *input, size_t size)
{
	return nghttp2_session_mem_recv(session, input, size);
}

ssize_type_t session_mem_send(nghttp2_session *session, const uint8_t **output)
{
	return nghttp2_session_mem_send(session, output);
}

int32_t submit_request(nghttp2_session *session, const nghttp2_priority_spec *priority,
	const nghttp2_nv *headers, size_t count, const data_provider_t *provider, void *user_data)
{
	return nghttp2_submit_request (
		session, priority, headers, count, provider, user_data
	);
}

int submit_response(nghttp2_session *session, int32_t stream_id,
	const nghttp2_nv *headers, size_t count, const data_provider_t *provider)
{
	return nghttp2_submit_response (
		session, stream_id, headers, count, provider
	);
}

#endif //RIWO_EXP_HTTP_NGHTTP2_USE_MODERN_API

} //namespace riwo::experimental::http::v2::detail::nghttp2_api

#endif //RIWO_EXP_HTTP_HTTP2_SUPPORT
