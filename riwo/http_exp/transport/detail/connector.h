// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_TRANSPORT_DETAIL_CONNECTOR_H
#define RIWO_HTTP_EXP_TRANSPORT_DETAIL_CONNECTOR_H

namespace riwo::http_exp
{

template<core_concepts::exec Exec>
auto basic_transport_connector<Exec>::connect_sync(const connect_request &)
	noexcept -> sys_expected<result_t>
{
	return sys_unexpected(make_error_code(errc::operation_not_supported));
}

} //namespace riwo::http_exp


#endif //RIWO_HTTP_EXP_TRANSPORT_DETAIL_CONNECTOR_H
