// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_TEST_EXP_HTTP_NGHTTP2_COMPAT_H
#define RIWO_TEST_EXP_HTTP_NGHTTP2_COMPAT_H

#include <riwo/experimental/http/cxx/configs.h>

#if RIWO_EXP_HTTP_NGHTTP2_USE_MODERN_API
# define NGHTTP2_NO_SSIZE_T
#elif defined(_MSC_VER) && !defined(_SSIZE_T_DEFINED)
# include <BaseTsd.h>
typedef SSIZE_T ssize_t;
# define _SSIZE_T_DEFINED
#endif //RIWO_EXP_HTTP_NGHTTP2_USE_MODERN_API

#include <nghttp2/nghttp2.h>

namespace riwo::test::nghttp2_api
{

#if RIWO_EXP_HTTP_NGHTTP2_USE_MODERN_API

using ssize_type_t = nghttp2_ssize;
using data_provider_t = nghttp2_data_provider2;

inline ssize_type_t session_mem_recv(
	nghttp2_session *session, const uint8_t *input, size_t size)
{
	return nghttp2_session_mem_recv2(session, input, size);
}

inline ssize_type_t session_mem_send(
	nghttp2_session *session, const uint8_t **output)
{
	return nghttp2_session_mem_send2(session, output);
}

inline int submit_response(
	nghttp2_session *session, int32_t stream_id, const nghttp2_nv *headers,
	size_t count, const data_provider_t *provider)
{
	return nghttp2_submit_response2(
		session, stream_id, headers, count, provider
	);
}

#else //RIWO_EXP_HTTP_NGHTTP2_USE_MODERN_API

using ssize_type_t = ssize_t;
using data_provider_t = nghttp2_data_provider;

inline ssize_type_t session_mem_recv(
	nghttp2_session *session, const uint8_t *input, size_t size)
{
	return nghttp2_session_mem_recv(session, input, size);
}

inline ssize_type_t session_mem_send(
	nghttp2_session *session, const uint8_t **output)
{
	return nghttp2_session_mem_send(session, output);
}

inline int submit_response(
	nghttp2_session *session, int32_t stream_id, const nghttp2_nv *headers,
	size_t count, const data_provider_t *provider)
{
	return nghttp2_submit_response(
		session, stream_id, headers, count, provider
	);
}

#endif //RIWO_EXP_HTTP_NGHTTP2_USE_MODERN_API

} //namespace riwo::test::nghttp2_api


#endif //RIWO_TEST_EXP_HTTP_NGHTTP2_COMPAT_H
