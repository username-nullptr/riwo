// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "status.h"

namespace riwo::http_exp
{

status::status(well_known value) noexcept : code_(value) {}

status::status(uint16_t code) : code_(code)
{
	if( code < 100 || code > 599 )
		throw std::invalid_argument("HTTP status must be in the range 100..599");
}

uint16_t status::code() const noexcept {
	return code_;
}

} //namespace riwo::http_exp
