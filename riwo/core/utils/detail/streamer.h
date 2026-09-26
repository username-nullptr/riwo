// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_CXX_DETAIL_STREAMER_H
#define RIWO_CORE_CXX_DETAIL_STREAMER_H

#include <riwo/core/utils/flags.h>
#include <riwo/core/utils/string_tools.h>
#include <riwo/core/cxx/cplusplus.h>

namespace riwo { namespace detail
{

inline void streamer_write_u64(std::byte *buffer, uint64_t value) noexcept {
	std::memcpy(buffer, &value, sizeof(value));
}

[[nodiscard]] inline uint64_t streamer_read_u64(const std::byte *buffer) noexcept
{
	uint64_t value = 0;
	std::memcpy(&value, buffer, sizeof(value));
	return value;
}

} //namespace detail

template <>
struct streamer<bool>
{
	[[nodiscard]] static auto encode(bool v) noexcept
	{
		std::vector<std::byte> buf;
		buf.emplace_back(static_cast<std::byte>(v));
		return buf;
	}

	[[nodiscard]] static decoder_data<bool>
	decode(const std::vector<std::byte> &buf, size_t offset = 0)
	{
		if( buf.size() <= offset )
		{
			runtime_error::loc_throw(std::format (
				"bad packet: {} / {} bytes", buf.size(), offset
			));
		}
		return {
			.data = std::to_integer<bool>(buf[offset]),
			.size = sizeof(bool)
		};
	}
};

template <std::integral T> requires (not std::same_as<T, bool>)
struct streamer<T>
{
	[[nodiscard]] static auto encode(T v) noexcept
	{
		std::vector<std::byte> buf;
		buf.resize(sizeof(T));
		std::memcpy(buf.data(), &v, buf.size());
		return buf;
	}

	[[nodiscard]] static decoder_data<T>
	decode(const std::vector<std::byte> &buf, size_t offset = 0)
	{
		if( buf.size() <= offset )
		{
			runtime_error::loc_throw(std::format (
				"bad packet: {} / {} bytes", buf.size(), offset
			));
		}
		else if( buf.size() - offset < sizeof(T) )
		{
			runtime_error::loc_throw(std::format (
				"bad packet: {} / {} bytes", buf.size() - offset, sizeof(T)
			));
		}
		T value = 0;
		std::memcpy(&value, buf.data() + offset, sizeof(T));
		return { value, sizeof(T) };
	}
};

template <std::floating_point T>
struct streamer<T>
{
	[[nodiscard]] static auto encode(T v) noexcept
	{
		std::vector<std::byte> buf;
		buf.resize(sizeof(T));
		std::memcpy(buf.data(), &v, buf.size());
		return buf;
	}

	[[nodiscard]] static decoder_data<T>
	decode(const std::vector<std::byte> &buf, size_t offset = 0)
	{
		if( buf.size() <= offset )
		{
			runtime_error::loc_throw(std::format (
				"bad packet: {} / {} bytes", buf.size(), offset
			));
		}
		else if( buf.size() - offset < sizeof(T) )
		{
			runtime_error::loc_throw(std::format (
				"bad packet: {} / {} bytes", buf.size() - offset, sizeof(T)
			));
		}
		T value = 0.0;
		std::memcpy(&value, buf.data() + offset, sizeof(T));
		return { value, sizeof(T) };
	};
};

template <concepts::enumerate T>
struct streamer<T>
{
	using num_t = std::underlying_type_t<T>;

	[[nodiscard]] static auto encode(T v) noexcept {
		return streamer<num_t>::encode(static_cast<num_t>(v));
	}
	[[nodiscard]] static decoder_data<T>
	decode(const std::vector<std::byte> &buf, size_t offset = 0) {
		auto data = streamer<num_t>::decode(buf, offset);
		return { static_cast<T>(*data), data.size };
	}
};

template <concepts::flag_template T>
struct streamer<flags<T>>
{
	using enum_t = T;
	using flags_t = flags<enum_t>;

	[[nodiscard]] static auto encode(flags_t v) noexcept {
		return streamer<enum_t>::encode(static_cast<enum_t>(v));
	}
	[[nodiscard]] static decoder_data<flags_t>
	decode(const std::vector<std::byte> &buf, size_t offset = 0) {
		auto data = streamer<enum_t>::decode(buf, offset);
		return { flags<T>(*data), data.size };
	}
};

template <typename T, size_t N>
struct streamer<T[N]>
{
	static auto encode(const T v[N])
	{
		std::vector<std::byte> buf;
		buf.resize(8);

		detail::streamer_write_u64(buf.data(), N);
		for(size_t i=0; i<N; i++)
		{
			auto sub = streamer<T>::encode(v[i]);
			buf.insert(buf.end(),
				std::make_move_iterator(sub.begin()),
				std::make_move_iterator(sub.end())
			);
		}
		return buf;
	}

	[[nodiscard]] static auto decode(const std::vector<std::byte> &buf, size_t offset = 0)
	{
		if( buf.size() < offset + 8 )
		{
			runtime_error::loc_throw(std::format (
				"bad packet: {} / {} bytes", buf.size(), offset + 8
			));
		}
		auto size = detail::streamer_read_u64(buf.data() + offset);
		if( size != N )
		{
			runtime_error::loc_throw(std::format (
				"bad packet: {} / {} bytes", buf.size(), N
			));
		}
		offset += 8;
		std::array<T,N> arr;
		size_t sum = 8;

		for(auto &n : arr)
		{
			auto data = streamer<T>::decode(buf, offset);
			n = std::move(*data);

			offset += data.size;
			sum += data.size;
		}
		return decoder_data<std::array<T,N>> { std::move(arr), sum };
	}
};

#define RIWO_PP_PARENS ()

#define RIWO_PP_EXPAND(...) \
	RIWO_PP_EXPAND4(RIWO_PP_EXPAND4(RIWO_PP_EXPAND4(RIWO_PP_EXPAND4(__VA_ARGS__))))

#define RIWO_PP_EXPAND4(...) \
	RIWO_PP_EXPAND3(RIWO_PP_EXPAND3(RIWO_PP_EXPAND3(RIWO_PP_EXPAND3(__VA_ARGS__))))

#define RIWO_PP_EXPAND3(...) \
	RIWO_PP_EXPAND2(RIWO_PP_EXPAND2(RIWO_PP_EXPAND2(RIWO_PP_EXPAND2(__VA_ARGS__))))

#define RIWO_PP_EXPAND2(...) \
	RIWO_PP_EXPAND1(RIWO_PP_EXPAND1(RIWO_PP_EXPAND1(RIWO_PP_EXPAND1(__VA_ARGS__))))

#define RIWO_PP_EXPAND1(...) __VA_ARGS__

#define RIWO_FIELD_MAP(m, ...) \
	__VA_OPT__(RIWO_PP_EXPAND(RIWO_FIELD_MAP_IMPL(m, __VA_ARGS__)))

#define RIWO_FIELD_MAP_IMPL(m, x, ...) \
	m(x) __VA_OPT__(RIWO_FIELD_MAP_AGAIN RIWO_PP_PARENS (m, __VA_ARGS__))

#define RIWO_FIELD_MAP_AGAIN() RIWO_FIELD_MAP_IMPL

#define RIWO_FIELD_MAP_COMMA(m, ...) \
	__VA_OPT__(RIWO_PP_EXPAND(RIWO_FIELD_MAP_COMMA_IMPL(m, __VA_ARGS__)))

#define RIWO_FIELD_MAP_COMMA_IMPL(m, x, ...) \
	m(x) __VA_OPT__(, RIWO_FIELD_MAP_COMMA_AGAIN RIWO_PP_PARENS (m, __VA_ARGS__))

#define RIWO_FIELD_MAP_COMMA_AGAIN() RIWO_FIELD_MAP_COMMA_IMPL

#define RIWO_PP_PROBE() ~, 1
#define RIWO_PP_SECOND(a, b, ...) b

#define RIWO_PP_IS_PROBE(...) RIWO_PP_SECOND(__VA_ARGS__, 0)
#define RIWO_PP_PROBE_PAREN(...) RIWO_PP_PROBE()

#define RIWO_PP_IS_PAREN(x) RIWO_PP_IS_PROBE(RIWO_PP_PROBE_PAREN x)

#define RIWO_PP_IF_0(t, f) f
#define RIWO_PP_IF_1(t, f) t

#define RIWO_PP_IF(c) RIWO_CAT(RIWO_PP_IF_, c)

#define RIWO_PP_UNPAREN(...) __VA_ARGS__

#define RIWO_PP_MAYBE_UNPAREN(x) \
	RIWO_PP_IF(RIWO_PP_IS_PAREN(x))(RIWO_PP_UNPAREN x, x)

#define RIWO_FIELD_DECL_IMPL(type, name, ...)  RIWO_PP_MAYBE_UNPAREN(type) name {__VA_ARGS__};
#define RIWO_FIELD_DECL_APPLY(...)  RIWO_FIELD_DECL_IMPL(__VA_ARGS__, )

#define RIWO_FIELD_DECL(x)  RIWO_FIELD_DECL_APPLY x

#define RIWO_FIELD_NAME_IMPL(type, name, ...)  name
#define RIWO_FIELD_NAME_APPLY(...)  RIWO_FIELD_NAME_IMPL(__VA_ARGS__, )

#define RIWO_FIELD_NAME(x)  RIWO_FIELD_NAME_APPLY x

} //namespace riwo


#endif //RIWO_CORE_CXX_DETAIL_STREAMER_H
