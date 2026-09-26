// SPDX-FileCopyrightText: 2024-2025 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_UTILS_ERROR_CODE_ADAPTER_H
#define RIWO_CORE_UTILS_ERROR_CODE_ADAPTER_H

#include <riwo/core/utils/token_concepts.h>
#include <riwo/core/cxx/cplusplus.h>

namespace riwo
{

template <concepts::error_code_token Error>
class RIWO_CORE_TAPI error_code_adapter
{
	RIWO_DISABLE_COPY_MOVE(error_code_adapter)

public:
	explicit error_code_adapter(Error target) noexcept;
	~error_code_adapter();

	[[nodiscard]] error_code &get() noexcept;

private:
	Error m_target;
	error_code m_error;
};

template <typename Error>
[[nodiscard]] RIWO_CORE_TAPI auto adapt_error_code(Error &error) noexcept
	requires is_error_code_token_v<Error&>;

} //namespace riwo
#include <riwo/core/utils/detail/error_code_adapter.h>


#endif //RIWO_CORE_UTILS_ERROR_CODE_ADAPTER_H
