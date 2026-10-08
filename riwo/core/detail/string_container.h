// SPDX-FileCopyrightText: 2025-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_DETAIL_STRING_CONTAINER_H
#define RIWO_CORE_DETAIL_STRING_CONTAINER_H

namespace riwo
{

template <concepts::character CharT, template<typename,typename...> class Container, typename...Args>
template <concepts::text_p<CharT> Text>
basic_string_container<CharT,Container,Args...>::string_t
basic_string_container<CharT,Container,Args...>::join(const Text &splits) const
{
	string_t result;
	auto view = strtls::to_view(splits);

	size_t length = 0;
	for(const auto &str : *this)
	{
		if( str.size() > result.max_size() - length )
			length_error::loc_throw("basic_string_container::join");
		length += str.size();
	}
	if( this->size() > 1 and not view.empty() )
	{
		const auto separators = this->size() - 1;
		if( separators > (result.max_size() - length) / view.size() )
			length_error::loc_throw("basic_string_container::join");
		length += separators * view.size();
	}
	result.reserve(length);

	bool first = true;
	for(const auto &str : *this)
	{
		if( not first )
			result.append(view.data(), view.size());
		result.append(str);
		first = false;
	}
	return result;
}

template <concepts::character CharT, template<typename,typename...> class Container, typename...Args>
template <concepts::text_p<CharT> Text>
auto basic_string_container<CharT,Container,Args...>::
join(size_t index, size_t length, const Text &splits) const -> string_t
{
	string_t result;
	auto view = strtls::to_view(splits);

	auto end = index + length;
	if( end > this->size() )
	{
		end = this->size();
		if( end <= index )
			return result;
	}
	size_t output_length = 0;
	for(size_t pos=index; pos<end; ++pos)
	{
		if( (*this)[pos].size() > result.max_size() - output_length )
			length_error::loc_throw("basic_string_container::join");
		output_length += (*this)[pos].size();
	}
	const auto count = end - index;
	if( count > 1 and not view.empty() )
	{
		const auto separators = count - 1;
		if( separators > (result.max_size() - output_length) / view.size() )
			length_error::loc_throw("basic_string_container::join");
		output_length += separators * view.size();
	}
	result.reserve(output_length);

	for(size_t pos=index; pos<end; ++pos)
	{
		if( pos != index )
			result.append(view.data(), view.size());
		result.append((*this)[pos]);
	}
	return result;
}

template <concepts::character CharT, template<typename,typename...> class Container, typename...Args>
template <concepts::text_p<CharT> Text>
auto basic_string_container<CharT,Container,Args...>::join(size_t index, const Text &splits) const -> string_t
{
	return join(index, this->size(), splits);
}

template <concepts::character CharT, template<typename,typename...> class Container, typename...Args>
template <typename Iter, concepts::text_p<CharT> Text>
auto basic_string_container<CharT,Container,Args...>::join(Iter begin, Iter end, const Text &splits)
	-> string_t requires is_container_iter_v<Iter>
{
	string_t result;
	auto view = strtls::to_view(splits);

	size_t length = 0;
	size_t count = 0;

	for(auto it=begin; it!=end; ++it)
	{
		if( it->size() > result.max_size() - length )
			length_error::loc_throw("basic_string_container::join");
		length += it->size();
		++count;
	}
	if( count > 1 and not view.empty() )
	{
		const auto separators = count - 1;
		if( separators > (result.max_size() - length) / view.size() )
			length_error::loc_throw("basic_string_container::join");
		length += separators * view.size();
	}
	result.reserve(length);

	bool first = true;
	for(auto it=begin; it!=end; ++it)
	{
		if( not first )
			result.append(view.data(), view.size());
		result.append(*it);
		first = false;
	}
	return result;
}

template <concepts::character CharT, template<typename,typename...> class Container, typename...Args>
template <concepts::text_p<CharT> Text>
basic_string_container<CharT,Container,Args...>
basic_string_container<CharT,Container,Args...>::from_string
(concepts::string_p<char_t> auto &&str, const Text &splits, bool ignore_empty)
{
	basic_string_container result;
	const auto delimiter = strtls::to_view(splits);

	if( delimiter.empty() )
		return result;

	const auto input = strtls::to_view(str);
	if constexpr( requires { result.reserve(size_t{}); } )
		result.reserve(input.size() / delimiter.size() + 1);

	size_t begin = 0;
	for(;;)
	{
		const auto pos = input.find(delimiter, begin);
		const auto end = pos == string_view_t::npos ? input.size() : pos;

		string_t tmp(input.substr(begin, end - begin));
		if( not strtls::trimmed(tmp).empty() or not ignore_empty )
			result.emplace_back(std::move(tmp));

		if( pos == string_view_t::npos )
			break;
		begin = pos + delimiter.size();
	}
	return result;
}

} //namespace riwo

template <riwo::concepts::character CharT, template<typename,typename...> class Container, typename...Args>
struct RIWO_CORE_TAPI std::formatter<riwo::basic_string_container<CharT,Container,Args...>, CharT>
{
	auto format(const riwo::basic_string_container<CharT,Container,Args...> &container, auto &context) const
	{
		if( container.empty() )
			return m_formatter.format(l_str(CharT,"[]"), context);

		std::basic_string<CharT> buf = l_str(CharT,"[");
		for(auto &str : container)
			buf += l_str(CharT,"'") + str + l_str(CharT,"', ");

		buf.erase(buf.size() - 2, 2);
		buf += l_str(CharT,"]");
		return m_formatter.format(buf, context);
	}

	constexpr auto parse(auto &context) noexcept {
		return m_formatter.parse(context);
	}

private:
	formatter<std::basic_string<CharT>, CharT> m_formatter;
};


#endif //RIWO_CORE_DETAIL_STRING_CONTAINER_H
