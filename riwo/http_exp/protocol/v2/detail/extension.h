// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_PROTOCOL_V2_DETAIL_EXTENSION_H
#define RIWO_HTTP_EXP_PROTOCOL_V2_DETAIL_EXTENSION_H

namespace riwo::http_exp::v2
{

constexpr stream_extension::stream_extension(int32_t stream_id) noexcept :
	m_stream_id(stream_id)
{

}

constexpr int32_t stream_extension::stream_id() const noexcept
{
	return m_stream_id;
}

constexpr connection_extension::connection_extension
(const connection_diagnostics &diagnostics) noexcept :
	m_diagnostics(diagnostics)
{

}

constexpr const settings_snapshot &connection_extension::local_settings() const noexcept
{
	return m_diagnostics.local_settings;
}

constexpr const settings_snapshot &connection_extension::peer_settings() const noexcept
{
	return m_diagnostics.peer_settings;
}

constexpr size_t connection_extension::pending_output() const noexcept
{
	return m_diagnostics.pending_output;
}

constexpr bool connection_extension::draining() const noexcept
{
	return m_diagnostics.draining;
}

constexpr const connection_diagnostics &connection_extension::diagnostics() const noexcept
{
	return m_diagnostics;
}

} //namespace riwo::http_exp::v2


#endif //RIWO_HTTP_EXP_PROTOCOL_V2_DETAIL_EXTENSION_H
