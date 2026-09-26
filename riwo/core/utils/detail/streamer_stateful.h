// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_CXX_DETAIL_STREAMER_STATEFUL_H
#define RIWO_CORE_CXX_DETAIL_STREAMER_STATEFUL_H

#include <optional>
#include <variant>

namespace riwo
{

template <>
struct streamer<std::monostate>
{
	[[nodiscard]] static auto encode(std::monostate) noexcept {
		return std::vector<std::byte> {};
	}
	[[nodiscard]] static decoder_data<std::monostate>
	decode(const std::vector<std::byte>&, size_t = 0) noexcept {
		return { .data = {}, .size = 0 };
	}
};

template <typename T>
struct streamer<std::optional<T>>
{
	using optional_t = std::optional<T>;

	[[nodiscard]] static auto encode(const optional_t &v)
	{
		std::vector<std::byte> buf;
		buf.emplace_back(static_cast<std::byte>(v.has_value()));
		if( v )
		{
			auto sub = streamer<T>::encode(*v);
			buf.insert(buf.end(),
				std::make_move_iterator(sub.begin()),
				std::make_move_iterator(sub.end())
			);
		}
		return buf;
	}

	[[nodiscard]] static decoder_data<optional_t>
	decode(const std::vector<std::byte> &buf, size_t offset = 0)
	{
		if( buf.size() <= offset )
		{
			runtime_error::loc_throw(std::format (
				"bad packet: {} / {} bytes", buf.size(), offset
			));
		}
		auto has_value = std::to_integer<bool>(buf[offset++]);
		if( not has_value )
			return { std::nullopt, 1 };

		auto data = streamer<T>::decode(buf, offset);
		return { optional_t { std::move(*data) }, 1 + data.size };
	}
};

template <typename T>
struct streamer<optional<T>>
{
	using optional_t = optional<T>;

	[[nodiscard]] static auto encode(const optional_t &v)
	{
		std::vector<std::byte> buf;
		buf.emplace_back(static_cast<std::byte>(v.has_value()));
		if( v )
		{
			auto sub = streamer<T>::encode(*v);
			buf.insert(buf.end(),
				std::make_move_iterator(sub.begin()),
				std::make_move_iterator(sub.end())
			);
		}
		return buf;
	}

	[[nodiscard]] static decoder_data<optional_t>
	decode(const std::vector<std::byte> &buf, size_t offset = 0)
	{
		if( buf.size() <= offset )
		{
			runtime_error::loc_throw(std::format (
				"bad packet: {} / {} bytes", buf.size(), offset
			));
		}
		auto has_value = std::to_integer<bool>(buf[offset++]);
		if( not has_value )
			return { nullopt, 1 };

		auto data = streamer<T>::decode(buf, offset);
		return { optional_t { std::move(*data) }, 1 + data.size };
	}
};

template <typename...Ts>
struct streamer<std::variant<Ts...>>
{
	using variant_t = std::variant<Ts...>;

	[[nodiscard]] static auto encode(const variant_t &v)
	{
		if( v.valueless_by_exception() )
			runtime_error::loc_throw("bad variant");

		std::vector<std::byte> buf;
		buf.resize(8);
		detail::streamer_write_u64(buf.data(), v.index());

		std::visit([&]<typename T>(const T &value) {
			auto sub = streamer<std::remove_cvref_t<T>>::encode(value);
			buf.insert(buf.end(),
				std::make_move_iterator(sub.begin()),
				std::make_move_iterator(sub.end())
			);
		}, v);
		return buf;
	}

	[[nodiscard]] static decoder_data<variant_t>
	decode(const std::vector<std::byte> &buf, size_t offset = 0)
	{
		if( buf.size() < offset + 8 )
		{
			runtime_error::loc_throw(std::format (
				"bad packet: {} / {} bytes", buf.size(), offset + 8
			));
		}
		auto index = detail::streamer_read_u64(buf.data() + offset);
		if( index >= sizeof...(Ts) )
		{
			runtime_error::loc_throw(std::format (
				"bad packet: {} [{}]", sizeof...(Ts), index
			));
		}
		offset += 8;
		size_t sum = 8;
		return { decode_impl(index, buf, offset, sum), sum };
	}

private:
	template <size_t I = 0>
	[[nodiscard]] static variant_t decode_impl(
		size_t index, const std::vector<std::byte> &buf, size_t &offset, size_t &sum)
	{
		if constexpr( I >= sizeof...(Ts) )
		{
			runtime_error::loc_throw(std::format (
				"bad packet: {} [{}]", sizeof...(Ts), I
			));
		}
		else
		{
			if( index == I )
			{
				using T = std::variant_alternative_t<I, variant_t>;
				auto data = streamer<T>::decode(buf, offset);

				offset += data.size;
				sum += data.size;

				return variant_t { std::in_place_index<I>, std::move(*data) };
			}
			return decode_impl<I + 1>(index, buf, offset, sum);
		}
	}
};

} //namespace riwo


#endif //RIWO_CORE_CXX_DETAIL_STREAMER_STATEFUL_H
