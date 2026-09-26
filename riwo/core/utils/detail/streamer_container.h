// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_CXX_DETAIL_STREAMER_CONTAINER_H
#define RIWO_CORE_CXX_DETAIL_STREAMER_CONTAINER_H

#include <riwo/core/utils/string_tools.h>
#include <filesystem>
#include <iterator>

#include <vector>
#include <bitset>
#include <array>
#include <tuple>
#include <deque>
#include <list>
#include <map>
#include <set>

#include <unordered_map>
#include <unordered_set>
#include <forward_list>

namespace riwo
{

template <concepts::any_string T>
struct streamer<T>
{
	using char_t = strtls::get_char_t<T>;

	static auto encode(const T &v)
	{
		std::vector<std::byte> buf;
		auto view = strtls::to_view(v);
		auto size = view.size() * sizeof(char_t);
		buf.resize(8 + size);

		detail::streamer_write_u64(buf.data(), size);
		std::memcpy(buf.data() + 8, view.data(), size);
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
		auto c_buf = buf.data() + offset;
		auto exp_size = detail::streamer_read_u64(c_buf);
		auto rem_size = buf.size() - offset - 8;

		if( rem_size < exp_size )
		{
			runtime_error::loc_throw(std::format (
				"bad packet: {} / {} bytes", rem_size, exp_size
			));
		}
		else if( exp_size % sizeof(char_t) )
		{
			runtime_error::loc_throw(std::format (
				"bad packet: {} % {} != 0", exp_size, sizeof(char_t)
			));
		}
		auto byte_size = static_cast<size_t>(exp_size);
		auto char_num = byte_size / sizeof(char_t);
		std::basic_string<char_t> str(char_num, '\0');

		std::memcpy(str.data(), c_buf + 8, byte_size);
		return decoder_data<std::basic_string<char_t>> { std::move(str), 8 + byte_size };
	}
};

template <typename T, size_t N>
struct streamer<std::array<T, N>>
{
	static auto encode(const std::array<T,N> &v)
	{
		std::vector<std::byte> buf;
		buf.resize(8);

		detail::streamer_write_u64(buf.data(), N);
		for(auto &n : v)
		{
			auto sub = streamer<T>::encode(n);
			buf.insert(buf.end(),
				std::make_move_iterator(sub.begin()),
				std::make_move_iterator(sub.end())
			);
		}
		return buf;
	}

	[[nodiscard]] static decoder_data<std::array<T,N>>
	decode(const std::vector<std::byte> &buf, size_t offset = 0)
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
				"bad packet: {} / {} bytes", size, N
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
		return { std::move(arr), sum };
	}
};

template <typename T, typename Alloc>
struct streamer<std::vector<T,Alloc>>
{
	using vector_t = std::vector<T>;

	static auto encode(const vector_t &v)
	{
		std::vector<std::byte> buf;
		buf.resize(8);

		detail::streamer_write_u64(buf.data(), v.size());
		for(auto &n : v)
		{
			auto sub = streamer<T>::encode(n);
			buf.insert(buf.end(),
				std::make_move_iterator(sub.begin()),
				std::make_move_iterator(sub.end())
			);
		}
		return buf;
	}

	[[nodiscard]] static decoder_data<vector_t>
	decode(const std::vector<std::byte> &buf, size_t offset = 0)
	{
		if( buf.size() < offset + 8 )
		{
			runtime_error::loc_throw(std::format (
				"bad packet: {} / {} bytes", buf.size(), offset + 8
			));
		}
		auto size = detail::streamer_read_u64(buf.data() + offset);
		offset += 8;
		vector_t vector;

		if( size > vector.max_size() )
			runtime_error::loc_throw("bad packet: container size is too large");

		vector.resize(static_cast<size_t>(size));
		size_t sum = 8;

		for(auto &n : vector)
		{
			auto data = streamer<T>::decode(buf, offset);
			n = std::move(*data);

			offset += data.size;
			sum += data.size;
		}
		return { std::move(vector), sum };
	}
};

template <typename T, typename Alloc>
struct streamer<std::deque<T,Alloc>>
{
	using deque_t = std::deque<T,Alloc>;

	static auto encode(const deque_t &v)
	{
		std::vector<std::byte> buf;
		buf.resize(8);

		detail::streamer_write_u64(buf.data(), v.size());
		for(auto &n : v)
		{
			auto sub = streamer<T>::encode(n);
			buf.insert(buf.end(),
				std::make_move_iterator(sub.begin()),
				std::make_move_iterator(sub.end())
			);
		}
		return buf;
	}

	[[nodiscard]] static decoder_data<deque_t>
	decode(const std::vector<std::byte> &buf, size_t offset = 0)
	{
		if( buf.size() < offset + 8 )
		{
			runtime_error::loc_throw(std::format (
				"bad packet: {} / {} bytes", buf.size(), offset + 8
			));
		}
		auto size = detail::streamer_read_u64(buf.data() + offset);
		offset += 8;
		deque_t deque;

		deque.resize(size);
		size_t sum = 8;

		for(auto &n : deque)
		{
			auto data = streamer<T>::decode(buf, offset);
			n = std::move(*data);

			offset += data.size;
			sum += data.size;
		}
		return { std::move(deque), sum };
	}
};

template <typename T, typename Alloc>
struct streamer<std::list<T,Alloc>>
{
	using list_t = std::list<T,Alloc>;

	static auto encode(const list_t &v)
	{
		std::vector<std::byte> buf;
		buf.resize(8);

		detail::streamer_write_u64(buf.data(), v.size());
		for(auto &n : v)
		{
			auto sub = streamer<T>::encode(n);
			buf.insert(buf.end(),
				std::make_move_iterator(sub.begin()),
				std::make_move_iterator(sub.end())
			);
		}
		return buf;
	}

	[[nodiscard]] static decoder_data<list_t>
	decode(const std::vector<std::byte> &buf, size_t offset = 0)
	{
		if( buf.size() < offset + 8 )
		{
			runtime_error::loc_throw(std::format (
				"bad packet: {} / {} bytes", buf.size(), offset + 8
			));
		}
		auto size = detail::streamer_read_u64(buf.data() + offset);
		offset += 8;
		list_t list;

		list.resize(size);
		size_t sum = 8;

		for(auto &n : list)
		{
			auto data = streamer<T>::decode(buf, offset);
			n = std::move(*data);

			offset += data.size;
			sum += data.size;
		}
		return { std::move(list), sum };
	}
};

template <typename T, typename Alloc>
struct streamer<std::forward_list<T,Alloc>>
{
	using list_t = std::forward_list<T,Alloc>;

	[[nodiscard]] static auto encode(const list_t &v)
	{
		std::vector<std::byte> buf;
		auto size = static_cast<uint64_t>(std::ranges::distance(v));

		buf.resize(8);
		detail::streamer_write_u64(buf.data(), size);
		for(auto &n : v)
		{
			auto sub = streamer<T>::encode(n);
			buf.insert(buf.end(),
				std::make_move_iterator(sub.begin()),
				std::make_move_iterator(sub.end())
			);
		}
		return buf;
	}

	[[nodiscard]] static decoder_data<list_t>
	decode(const std::vector<std::byte> &buf, size_t offset = 0)
	{
		if( buf.size() < offset + 8 )
		{
			runtime_error::loc_throw(std::format (
				"bad packet: {} / {} bytes", buf.size(), offset + 8
			));
		}
		auto size = detail::streamer_read_u64(buf.data() + offset);
		offset += 8;

		size_t sum = 8;
		list_t list;
		auto iter = list.before_begin();

		for(size_t i=0; i<size; i++)
		{
			auto data = streamer<T>::decode(buf, offset);
			iter = list.insert_after(iter, std::move(*data));

			offset += data.size;
			sum += data.size;
		}
		return { std::move(list), sum };
	}
};

template <typename F, typename S>
struct streamer<std::pair<F,S>>
{
	using pair_t = std::pair<F,S>;

	static auto encode(const pair_t &v)
	{
		std::vector<std::byte> buf;
		auto sub = streamer<F>::encode(v.first);
		buf.insert(buf.end(),
			std::make_move_iterator(sub.begin()),
			std::make_move_iterator(sub.end())
		);
		sub = streamer<S>::encode(v.second);
		buf.insert(buf.end(),
			std::make_move_iterator(sub.begin()),
			std::make_move_iterator(sub.end())
		);
		return buf;
	}

	[[nodiscard]] static auto decode(const std::vector<std::byte> &buf, size_t offset = 0)
	{
		auto f = streamer<F>::decode(buf, offset);
		offset += f.size;
		auto s = streamer<S>::decode(buf, offset);
		return decoder_data<pair_t>{
			{ std::move(*f), std::move(*s) }, f.size + s.size
		};
	}
};

template <typename...Ts>
struct streamer<std::tuple<Ts...>>
{
	using tuple_t = std::tuple<Ts...>;

	[[nodiscard]] static auto encode(const tuple_t &v)
	{
		std::vector<std::byte> buf;
		std::apply([&]<typename...F>(const F&...xs) {
			(helper(buf, streamer<std::remove_cvref_t<F>>::encode(xs)), ...);
		}, v);
		return buf;
	}

	[[nodiscard]] static decoder_data<tuple_t>
	decode(const std::vector<std::byte> &buf, size_t offset = 0)
	{
		size_t sum = 0;
		return { decode_impl(buf, offset, sum, std::index_sequence_for<Ts...>{}), sum };
	}

private:
	static void helper(std::vector<std::byte> &total, std::vector<std::byte> &&sub)
	{
		total.insert(total.end(),
			std::make_move_iterator(sub.begin()),
			std::make_move_iterator(sub.end())
		);
	}

	template <typename T>
	[[nodiscard]] static T decode_one(const std::vector<std::byte> &buf, size_t &offset, size_t &sum)
	{
		auto data = streamer<std::remove_cvref_t<T>>::decode(buf, offset);
		offset += data.size;
		sum += data.size;
		return std::move(*data);
	}

	template <size_t...I>
	[[nodiscard]] static tuple_t decode_impl(
		const std::vector<std::byte> &buf, size_t &offset, size_t &sum,
		std::index_sequence<I...>)
	{
		return tuple_t {
			decode_one<std::tuple_element_t<I, tuple_t>>(buf, offset, sum)...
		};
	}
};

template <typename K, typename V, typename Compare, typename Alloc>
struct streamer<std::map<K,V,Compare,Alloc>>
{
	using map_t = std::map<K,V,Compare,Alloc>;

	static auto encode(const map_t &v)
	{
		std::vector<std::byte> buf;
		buf.resize(8);

		detail::streamer_write_u64(buf.data(), v.size());
		for(auto &n : v)
		{
			auto sub = streamer<std::pair<K,V>>::encode(n);
			buf.insert(buf.end(),
				std::make_move_iterator(sub.begin()),
				std::make_move_iterator(sub.end())
			);
		}
		return buf;
	}

	[[nodiscard]] static decoder_data<map_t>
	decode(const std::vector<std::byte> &buf, size_t offset = 0)
	{
		if( buf.size() < offset + 8 )
		{
			runtime_error::loc_throw(std::format (
				"bad packet: {} / {} bytes", buf.size(), offset + 8
			));
		}
		auto size = detail::streamer_read_u64(buf.data() + offset);
		offset += 8;

		size_t sum = 8;
		map_t map;

		for(size_t i=0; i<size; i++)
		{
			auto data = streamer<std::pair<K,V>>::decode(buf, offset);
			map.emplace(std::move(*data));

			offset += data.size;
			sum += data.size;
		}
		return { std::move(map), sum };
	}
};

template<typename K, typename V, typename Hash, typename Pred, typename Alloc>
struct streamer<std::unordered_map<K,V,Hash,Pred,Alloc>>
{
	using map_t = std::unordered_map<K,V,Hash,Pred,Alloc>;

	static auto encode(const map_t &v)
	{
		std::vector<std::byte> buf;
		buf.resize(8);

		detail::streamer_write_u64(buf.data(), v.size());
		for(auto &n : v)
		{
			auto sub = streamer<std::pair<K,V>>::encode(n);
			buf.insert(buf.end(),
				std::make_move_iterator(sub.begin()),
				std::make_move_iterator(sub.end())
			);
		}
		return buf;
	}

	[[nodiscard]] static decoder_data<map_t>
	decode(const std::vector<std::byte> &buf, size_t offset = 0)
	{
		if( buf.size() < offset + 8 )
		{
			runtime_error::loc_throw(std::format (
				"bad packet: {} / {} bytes", buf.size(), offset + 8
			));
		}
		auto size = detail::streamer_read_u64(buf.data() + offset);
		offset += 8;

		size_t sum = 8;
		map_t map;

		for(size_t i=0; i<size; i++)
		{
			auto data = streamer<std::pair<K,V>>::decode(buf, offset);
			map.emplace(std::move(*data));

			offset += data.size;
			sum += data.size;
		}
		return { std::move(map), sum };
	}
};

template<typename T, typename Compare, typename Alloc>
struct streamer<std::set<T,Compare,Alloc>>
{
	using set_t = std::set<T,Compare,Alloc>;

	static auto encode(const set_t &v)
	{
		std::vector<std::byte> buf;
		buf.resize(8);

		detail::streamer_write_u64(buf.data(), v.size());
		for(auto &n : v)
		{
			auto sub = streamer<T>::encode(n);
			buf.insert(buf.end(),
				std::make_move_iterator(sub.begin()),
				std::make_move_iterator(sub.end())
			);
		}
		return buf;
	}

	[[nodiscard]] static decoder_data<set_t>
	decode(const std::vector<std::byte> &buf, size_t offset = 0)
	{
		if( buf.size() < offset + 8 )
		{
			runtime_error::loc_throw(std::format (
				"bad packet: {} / {} bytes", buf.size(), offset + 8
			));
		}
		auto size = detail::streamer_read_u64(buf.data() + offset);
		offset += 8;

		size_t sum = 8;
		set_t set;

		for(size_t i=0; i<size; i++)
		{
			auto data = streamer<T>::decode(buf, offset);
			set.emplace(std::move(*data));

			offset += data.size;
			sum += data.size;
		}
		return { std::move(set), sum };
	}
};

template<typename T, typename Hash, typename Pred, typename Alloc>
struct streamer<std::unordered_set<T,Hash,Pred,Alloc>>
{
	using set_t = std::unordered_set<T,Hash,Pred,Alloc>;

	static auto encode(const set_t &v)
	{
		std::vector<std::byte> buf;
		buf.resize(8);

		detail::streamer_write_u64(buf.data(), v.size());
		for(auto &n : v)
		{
			auto sub = streamer<T>::encode(n);
			buf.insert(buf.end(),
				std::make_move_iterator(sub.begin()),
				std::make_move_iterator(sub.end())
			);
		}
		return buf;
	}

	[[nodiscard]] static decoder_data<set_t>
	decode(const std::vector<std::byte> &buf, size_t offset = 0)
	{
		if( buf.size() < offset + 8 )
		{
			runtime_error::loc_throw(std::format (
				"bad packet: {} / {} bytes", buf.size(), offset + 8
			));
		}
		auto size = detail::streamer_read_u64(buf.data() + offset);
		offset += 8;

		size_t sum = 8;
		set_t set;

		for(size_t i=0; i<size; i++)
		{
			auto data = streamer<T>::decode(buf, offset);
			set.emplace(std::move(*data));

			offset += data.size;
			sum += data.size;
		}
		return { std::move(set), sum };
	}
};

template <>
struct streamer<std::filesystem::path>
{
	using path_t = std::filesystem::path;

	static auto encode(const path_t &v)
	{
		std::vector<std::byte> buf;
		auto str = v.string();
		buf.resize(8 + str.size());

		detail::streamer_write_u64(buf.data(), str.size());
		std::memcpy(buf.data() + 8, str.data(), str.size());
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
		auto c_buf = buf.data() + offset;
		auto serialized_size = detail::streamer_read_u64(c_buf);
		auto rem_size = buf.size() - offset - 8;

		if( rem_size < serialized_size )
		{
			runtime_error::loc_throw(std::format (
				"bad packet: {} / {} bytes", rem_size, serialized_size
			));
		}
		auto byte_size = static_cast<size_t>(serialized_size);
		std::string str(byte_size, '\0');
		std::memcpy(str.data(), c_buf + 8, byte_size);
		return decoder_data { std::move(str), 8 + byte_size };
	}
};

} //namespace riwo


#endif //RIWO_CORE_CXX_DETAIL_STREAMER_CONTAINER_H
