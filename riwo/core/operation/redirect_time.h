// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_OPERATION_REDIRECT_TIME_H
#define RIWO_CORE_OPERATION_REDIRECT_TIME_H

#include <riwo/core/cxx/type_traits.h>

namespace riwo
{

template <typename Token>
class RIWO_CORE_TAPI redirect_time_t
{
public:
	using token_t = Token;

	template <typename Rep, typename Period>
	redirect_time_t(auto &&completion_token, const duration<Rep,Period> &rtime);

	template <typename Clock, typename Duration>
	redirect_time_t(auto &&completion_token, const time_point<Clock,Duration> &atime);

	token_t token;
	milliseconds time {0};
};

template <typename Token, typename Rep, typename Period>
[[nodiscard]] RIWO_CORE_TAPI auto redirect_time (
	Token &&token, const duration<Rep,Period> &timeout
);

template <typename Token, typename Clock, typename Duration>
[[nodiscard]] RIWO_CORE_TAPI auto redirect_time (
	Token &&token, const time_point<Clock,Duration> &timeout
);

} //namespace riwo
#include <riwo/core/operation/detail/redirect_time.h>


#endif //RIWO_CORE_OPERATION_REDIRECT_TIME_H
