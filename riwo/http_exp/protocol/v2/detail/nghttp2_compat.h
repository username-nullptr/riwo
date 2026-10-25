// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_PROTOCOL_V2_DETAIL_NGHTTP2_COMPAT_H
#define RIWO_HTTP_EXP_PROTOCOL_V2_DETAIL_NGHTTP2_COMPAT_H

#include <riwo/http_exp/cxx/attributes.h>
#include <riwo/http_exp/cxx/configs.h>

#if RIWO_HTTP_EXP_NGHTTP2_USE_MODERN_API
# define NGHTTP2_NO_SSIZE_T
#elif defined(_MSC_VER) && !defined(_SSIZE_T_DEFINED)
# include <BaseTsd.h>
typedef SSIZE_T ssize_t;
# define _SSIZE_T_DEFINED
#endif //RIWO_HTTP_EXP_NGHTTP2_USE_MODERN_API

#include <nghttp2/nghttp2.h>

namespace riwo::http_exp::v2::detail::nghttp2_api
{

#if RIWO_HTTP_EXP_NGHTTP2_USE_MODERN_API

using ssize_type_t = nghttp2_ssize;
using data_provider_t = nghttp2_data_provider2;

RIWO_HTTP_EXP_API ssize_type_t
session_mem_recv(nghttp2_session *session, const uint8_t *input, size_t size);

RIWO_HTTP_EXP_API ssize_type_t
session_mem_send(nghttp2_session *session, const uint8_t **output);

RIWO_HTTP_EXP_API int32_t submit_request (
	nghttp2_session *session, const nghttp2_priority_spec *priority, const nghttp2_nv *headers,
	size_t count, const data_provider_t *provider, void *user_data
);
RIWO_HTTP_EXP_API int submit_response (
	nghttp2_session *session, int32_t stream_id, const nghttp2_nv *headers,
	size_t count, const data_provider_t *provider
);

#else //RIWO_HTTP_EXP_NGHTTP2_USE_MODERN_API

using ssize_type_t = ssize_t;
using data_provider_t = nghttp2_data_provider;

RIWO_HTTP_EXP_API ssize_type_t
session_mem_recv(nghttp2_session *session, const uint8_t *input, size_t size);

RIWO_HTTP_EXP_API ssize_type_t
session_mem_send(nghttp2_session *session, const uint8_t **output);

RIWO_HTTP_EXP_API int32_t submit_request (
	nghttp2_session *session, const nghttp2_priority_spec *priority, const nghttp2_nv *headers,
	size_t count, const data_provider_t *provider, void *user_data
);
RIWO_HTTP_EXP_API int submit_response (
	nghttp2_session *session, int32_t stream_id, const nghttp2_nv *headers,
	size_t count, const data_provider_t *provider
);

#endif //RIWO_HTTP_EXP_NGHTTP2_USE_MODERN_API

} //namespace riwo::http_exp::v2::detail::nghttp2_api


#endif //RIWO_HTTP_EXP_PROTOCOL_V2_DETAIL_NGHTTP2_COMPAT_H
