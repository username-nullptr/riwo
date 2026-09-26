// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_WEBSOCKET_PROTOCOL_DETAIL_UTF8_H
#define RIWO_WEBSOCKET_PROTOCOL_DETAIL_UTF8_H

#include <riwo/websocket/cxx/attributes.h>

namespace riwo::websocket::detail
{

class RIWO_WEBSOCKET_API utf8_validator
{
public:
	utf8_validator();
	~utf8_validator();

	utf8_validator(const utf8_validator &other);
	utf8_validator(utf8_validator &&other) noexcept;

	utf8_validator &operator=(const utf8_validator &other);
	utf8_validator &operator=(utf8_validator &&other) noexcept;

	[[nodiscard]] bool consume(std::string_view text) noexcept;
	[[nodiscard]] bool complete() const noexcept;

private:
	class impl;
	impl *m_impl;
};

[[nodiscard]] RIWO_WEBSOCKET_API
bool is_valid_utf8(std::string_view text) noexcept;

} //namespace riwo::websocket::detail


#endif //RIWO_WEBSOCKET_PROTOCOL_DETAIL_UTF8_H
