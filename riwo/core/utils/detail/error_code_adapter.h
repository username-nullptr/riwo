// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_UTILS_DETAIL_ERROR_CODE_ADAPTER_H
#define RIWO_CORE_UTILS_DETAIL_ERROR_CODE_ADAPTER_H

namespace riwo
{

template <concepts::error_code_token Error>
error_code_adapter<Error>::error_code_adapter(Error target) noexcept :
	m_target(target), m_error(target)
{

}

template <concepts::error_code_token Error>
error_code_adapter<Error>::~error_code_adapter()
{
	if constexpr( std::is_same_v<Error,std::error_code&> )
		m_target = static_cast<std::error_code&>(m_error);
	else
		m_target = m_error;
}

template <concepts::error_code_token Error>
error_code &error_code_adapter<Error>::get() noexcept
{
	return m_error;
}

template <typename Error>
auto adapt_error_code(Error &error) noexcept
	requires is_error_code_token_v<Error&>
{
	return error_code_adapter<Error&>(error);
}

} //namespace riwo


#endif //RIWO_CORE_UTILS_DETAIL_ERROR_CODE_ADAPTER_H
