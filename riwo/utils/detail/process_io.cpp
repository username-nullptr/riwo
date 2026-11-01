// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "process_io.h"

namespace riwo::utils::detail
{

std::shared_ptr<std::string> copy_process_buffer(const const_buffer &buffer)
{
	auto result = std::make_shared<std::string>();
	if( buffer.size() > 0 )
	{
		result->assign(
			static_cast<const char*>(buffer.data()), buffer.size()
		);
	}
	return result;
}

} //namespace riwo::utils::detail
