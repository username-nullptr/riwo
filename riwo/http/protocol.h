// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_PROTOCOL_H
#define RIWO_HTTP_PROTOCOL_H

#include <riwo/http/protocol/authentication.h>
#include <riwo/http/protocol/duplex_channel.h>
#include <riwo/http/protocol/response_head.h>
#include <riwo/http/protocol/capabilities.h>
#include <riwo/http/protocol/request_head.h>
#include <riwo/http/protocol/compression.h>
#include <riwo/http/protocol/conditional.h>
#include <riwo/http/protocol/parameters.h>
#include <riwo/http/protocol/body_spec.h>
#include <riwo/http/protocol/multipart.h>
#include <riwo/http/protocol/engine.h>
#include <riwo/http/protocol/method.h>
#include <riwo/http/protocol/status.h>
#include <riwo/http/protocol/fields.h>
#include <riwo/http/protocol/cookie.h>
#include <riwo/http/protocol/policy.h>
#include <riwo/http/protocol/limits.h>
#include <riwo/http/protocol/range.h>
#include <riwo/http/protocol/event.h>

#include <riwo/http/protocol/v1/engine.h>
#include <riwo/http/protocol/v1/codec.h>

#if RIWO_HTTP_HTTP2_SUPPORT
# include <riwo/http/protocol/v2/extension.h>
# include <riwo/http/protocol/v2/engine.h>
# include <riwo/http/protocol/v2/config.h>
#endif //RIWO_HTTP_HTTP2_SUPPORT


#endif //RIWO_HTTP_PROTOCOL_H
