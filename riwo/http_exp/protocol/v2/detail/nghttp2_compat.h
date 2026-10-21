// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_PROTOCOL_V2_DETAIL_NGHTTP2_COMPAT_H
#define RIWO_HTTP_EXP_PROTOCOL_V2_DETAIL_NGHTTP2_COMPAT_H

#include <riwo/http_exp/cxx/configs.h>

#if RIWO_HTTP_EXP_NGHTTP2_USE_MODERN_API
# define NGHTTP2_NO_SSIZE_T
#elif defined(_MSC_VER) && !defined(_SSIZE_T_DEFINED)
# include <BaseTsd.h>
typedef SSIZE_T ssize_t;
# define _SSIZE_T_DEFINED
#endif

#include <nghttp2/nghttp2.h>

namespace riwo::http_exp::v2::detail::nghttp2_api {

#if RIWO_HTTP_EXP_NGHTTP2_USE_MODERN_API

using ssize_type = nghttp2_ssize;
using data_provider = nghttp2_data_provider2;

inline ssize_type session_mem_recv(nghttp2_session *session,
	const uint8_t *input, size_t size)
{
	return nghttp2_session_mem_recv2(session, input, size);
}

inline ssize_type session_mem_send(nghttp2_session *session,
	const uint8_t **output)
{
	return nghttp2_session_mem_send2(session, output);
}

inline int32_t submit_request(nghttp2_session *session,
	const nghttp2_priority_spec *priority, const nghttp2_nv *headers,
	size_t count, const data_provider *provider, void *user_data)
{
	return nghttp2_submit_request2(session, priority, headers, count,
		provider, user_data);
}

inline int submit_response(nghttp2_session *session, int32_t stream_id,
	const nghttp2_nv *headers, size_t count, const data_provider *provider)
{
	return nghttp2_submit_response2(session, stream_id, headers, count,
		provider);
}

#else

using ssize_type = ssize_t;
using data_provider = nghttp2_data_provider;

inline ssize_type session_mem_recv(nghttp2_session *session,
	const uint8_t *input, size_t size)
{
	return nghttp2_session_mem_recv(session, input, size);
}

inline ssize_type session_mem_send(nghttp2_session *session,
	const uint8_t **output)
{
	return nghttp2_session_mem_send(session, output);
}

inline int32_t submit_request(nghttp2_session *session,
	const nghttp2_priority_spec *priority, const nghttp2_nv *headers,
	size_t count, const data_provider *provider, void *user_data)
{
	return nghttp2_submit_request(session, priority, headers, count,
		provider, user_data);
}

inline int submit_response(nghttp2_session *session, int32_t stream_id,
	const nghttp2_nv *headers, size_t count, const data_provider *provider)
{
	return nghttp2_submit_response(session, stream_id, headers, count,
		provider);
}

#endif

} //namespace riwo::http_exp::v2::detail::nghttp2_api

#endif //RIWO_HTTP_EXP_PROTOCOL_V2_DETAIL_NGHTTP2_COMPAT_H
