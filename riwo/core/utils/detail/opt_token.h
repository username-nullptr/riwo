// SPDX-FileCopyrightText: 2024-2025 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_UTILS_DETAIL_OPT_TOKEN_H
#define RIWO_CORE_UTILS_DETAIL_OPT_TOKEN_H

namespace riwo
{

template <typename Token>
template <typename Rep, typename Period>
redirect_time_t<Token>::redirect_time_t(auto &&completion_token, const duration<Rep,Period> &rtime) :
	token(std::forward<decltype(completion_token)>(completion_token)), time(std::chrono::duration_cast<milliseconds>(rtime))
{

}

template <typename Token>
template <typename Clock, typename Duration>
redirect_time_t<Token>::redirect_time_t(auto &&completion_token, const time_point<Clock,Duration> &atime) :
	token(std::forward<decltype(completion_token)>(completion_token))
{
	auto now = std::chrono::system_clock::now();
	auto rtime = std::chrono::time_point_cast<decltype(now)>(atime) - now;

	this->time = rtime.count() < 0 ? milliseconds(0) :
		std::chrono::duration_cast<milliseconds>(rtime);
}

template <typename Token, typename Rep, typename Period>
auto redirect_time(Token &&token, const duration<Rep,Period> &timeout)
{
	return redirect_time_t<std::decay_t<Token>>(std::forward<Token>(token), timeout);
}

template <typename Token, typename Clock, typename Duration>
auto redirect_time(Token &&token, const time_point<Clock,Duration> &timeout)
{
	return redirect_time_t<std::decay_t<Token>>(std::forward<Token>(token), timeout);
}

} //namespace riwo


#endif //RIWO_CORE_UTILS_DETAIL_OPT_TOKEN_H
