// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_CXX_DETAIL_STREAMER_CHRONO_H
#define RIWO_CORE_CXX_DETAIL_STREAMER_CHRONO_H

#include <chrono>
#include <ctime>

namespace riwo
{

template <typename Rep, typename Period>
struct streamer<std::chrono::duration<Rep,Period>>
{
	using duration_t = std::chrono::duration<Rep,Period>;

	[[nodiscard]] static auto encode(const duration_t &v) {
		return streamer<Rep>::encode(v.count());
	}

	[[nodiscard]] static decoder_data<duration_t>
	decode(const std::vector<std::byte> &buf, size_t offset = 0)
	{
		auto data = streamer<Rep>::decode(buf, offset);
		return { duration_t { std::move(*data) }, data.size };
	}
};

template <typename Clock, typename Duration>
struct streamer<std::chrono::time_point<Clock,Duration>>
{
	using time_point_t = std::chrono::time_point<Clock,Duration>;

	[[nodiscard]] static auto encode(const time_point_t &v) {
		return streamer<Duration>::encode(v.time_since_epoch());
	}

	[[nodiscard]] static decoder_data<time_point_t>
	decode(const std::vector<std::byte> &buf, size_t offset = 0)
	{
		auto data = streamer<Duration>::decode(buf, offset);
		return { time_point_t { std::move(*data) }, data.size };
	}
};

template <>
struct streamer<std::tm>
{
	using fields_t = std::tuple<int,int,int, int,int,int, int,int,int>;

	[[nodiscard]] static auto encode(const std::tm &v)
	{
		fields_t fields {
			v.tm_sec, v.tm_min, v.tm_hour, v.tm_mday, v.tm_mon,
			v.tm_year, v.tm_wday, v.tm_yday, v.tm_isdst
		};
		return streamer<fields_t>::encode(fields);
	}

	[[nodiscard]] static decoder_data<std::tm>
	decode(const std::vector<std::byte> &buf, size_t offset = 0)
	{
		auto data = streamer<fields_t>::decode(buf, offset);
		std::tm value {};
		std::tie (
			value.tm_sec, value.tm_min, value.tm_hour, value.tm_mday, value.tm_mon,
			value.tm_year, value.tm_wday, value.tm_yday, value.tm_isdst
		)
		= std::move(*data);
		return { value, data.size };
	}
};

template <>
struct streamer<timespec>
{
	using fields_t = std::tuple<std::time_t, long>;

	[[nodiscard]] static auto encode(const timespec &v) {
		return streamer<fields_t>::encode(fields_t { v.tv_sec, v.tv_nsec });
	}

	[[nodiscard]] static decoder_data<timespec>
	decode(const std::vector<std::byte> &buf, size_t offset = 0)
	{
		auto data = streamer<fields_t>::decode(buf, offset);
		return {
			.data = {
				.tv_sec = std::get<0>(*data),
				.tv_nsec = std::get<1>(*data)
			},
			.size = data.size
		};
	}
};

} //namespace riwo


#endif //RIWO_CORE_CXX_DETAIL_STREAMER_CHRONO_H
