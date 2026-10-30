// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_CLIENT_DETAIL_FACTORY_H
#define RIWO_HTTP_EXP_CLIENT_DETAIL_FACTORY_H

#include <riwo/http_exp/client/detail/connector.h>
#include <riwo/http_exp/session/client_session.h>

namespace riwo::http_exp::detail
{

class RIWO_DECL_HIDDEN session_factory
{
	RIWO_DISABLE_COPY_MOVE(session_factory)

public:
	session_factory() = default;
	virtual ~session_factory() = default;

	virtual sys_expected<client_session_ptr> create (
		const session_key &key, negotiated_transport transport, const client_config &config
	) = 0;
};

using session_factory_ptr = std::shared_ptr<session_factory>;

[[nodiscard]] RIWO_HTTP_EXP_API
session_factory_ptr make_default_session_factory();

} //namespace riwo::http_exp::detail


#endif //RIWO_HTTP_EXP_CLIENT_DETAIL_FACTORY_H
