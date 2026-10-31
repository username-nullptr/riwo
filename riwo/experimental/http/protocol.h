// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_EXP_HTTP_PROTOCOL_H
#define RIWO_EXP_HTTP_PROTOCOL_H

#include <riwo/experimental/http/protocol/authentication.h>
#include <riwo/experimental/http/protocol/duplex_channel.h>
#include <riwo/experimental/http/protocol/response_head.h>
#include <riwo/experimental/http/protocol/capabilities.h>
#include <riwo/experimental/http/protocol/request_head.h>
#include <riwo/experimental/http/protocol/compression.h>
#include <riwo/experimental/http/protocol/conditional.h>
#include <riwo/experimental/http/protocol/parameters.h>
#include <riwo/experimental/http/protocol/body_spec.h>
#include <riwo/experimental/http/protocol/multipart.h>
#include <riwo/experimental/http/protocol/engine.h>
#include <riwo/experimental/http/protocol/method.h>
#include <riwo/experimental/http/protocol/status.h>
#include <riwo/experimental/http/protocol/fields.h>
#include <riwo/experimental/http/protocol/cookie.h>
#include <riwo/experimental/http/protocol/policy.h>
#include <riwo/experimental/http/protocol/limits.h>
#include <riwo/experimental/http/protocol/range.h>
#include <riwo/experimental/http/protocol/event.h>

#include <riwo/experimental/http/protocol/v1/engine.h>
#include <riwo/experimental/http/protocol/v1/codec.h>

#if RIWO_EXP_HTTP_HTTP2_SUPPORT
# include <riwo/experimental/http/protocol/v2/extension.h>
# include <riwo/experimental/http/protocol/v2/engine.h>
# include <riwo/experimental/http/protocol/v2/config.h>
#endif //RIWO_EXP_HTTP_HTTP2_SUPPORT


#endif //RIWO_EXP_HTTP_PROTOCOL_H
