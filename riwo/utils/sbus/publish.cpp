// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "publish.h"

namespace riwo::utils::sbus
{

void publish(std::string_view topic, const void *buffer, size_t size)
{
	publish<default_interface>(topic, buffer, size);
}

} //namespace riwo::utils::sbus
