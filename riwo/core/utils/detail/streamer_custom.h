// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_CXX_DETAIL_STREAMER_CUSTOM_H
#define RIWO_CORE_CXX_DETAIL_STREAMER_CUSTOM_H

namespace riwo { namespace concepts
{

template <class T>
concept streamer_custom_type = requires(T &v) {
	v.meta_fields();
};

template <class T>
concept streamer_type = requires(T &v)
{
	streamer<T>::encode(v);
	v = *streamer<T>::decode(std::declval<std::vector<std::byte>>());
};

template <class T>
concept streamer_type_p = streamer_type<std::remove_cvref_t<T>>;

} //namespace concepts

template <concepts::streamer_custom_type T>
struct streamer<T>
{
	[[nodiscard]] static auto encode(const T &v) noexcept
	{
		std::vector<std::byte> buf;
		std::apply([&]<typename...F>(const F&...xs) {
			(helper(buf, streamer<std::remove_cvref_t<F>>::encode(xs)), ...);
		}, v.meta_fields());
		return buf;
	}

	[[nodiscard]] static decoder_data<T> decode(const std::vector<std::byte> &buf, size_t offset = 0)
	{
		size_t sum = 0;
		T v;
		std::apply([&](auto&...xs) {
			(helper(offset, xs, sum, buf), ...);
		}, v.meta_fields());
		return { v, sum };
	}

private:
	static void helper(std::vector<std::byte> &total, std::vector<std::byte> &&sub)
	{
		total.insert(total.end(),
			std::make_move_iterator(sub.begin()),
			std::make_move_iterator(sub.end())
		);
	}

	template <typename F>
	static void helper(size_t &offset, F &fields, size_t &sum, const std::vector<std::byte> &buf)
	{
		auto data = streamer<F>::decode(buf, offset);
		fields = std::move(*data);
		offset += data.size;
		sum += data.size;
	}
};

} //namespace riwo


#endif //RIWO_CORE_CXX_DETAIL_STREAMER_CUSTOM_H
