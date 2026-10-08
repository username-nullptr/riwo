// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_UTILS_FORMATTER_H
#define RIWO_CORE_UTILS_FORMATTER_H

#include <riwo/core/utils/string_tools.h>
#include <riwo/core/cxx/formatter.h>
#include <riwo/core/cxx/expected.h>
#include <riwo/core/cxx/tools.h>
#include <riwo/core/cxx/asio.h>

#include <filesystem>
#include <optional>
#include <thread>
#include <atomic>
#include <memory>
#include <vector>

namespace riwo { namespace detail
{

inline uint64_t thread_id_helper(void *id) {
	return reinterpret_cast<uint64_t>(id);
}
inline uint64_t thread_id_helper(uint64_t id) {
	return id;
}

} //namespace detail

namespace concepts
{

template <typename CharT, typename...Args>
concept formatter = sizeof...(Args) > 0 and requires(Args&&...args) {
	(std::format(l_str(CharT,"{}"), std::forward<Args>(args)), ...);
};

template <typename...Args>
concept any_formatter =
	formatter<char,Args...> or formatter<wchar_t,Args...> or
	formatter<char8_t,Args...> or formatter<char16_t,Args...> or formatter<char32_t,Args...>;

}} //namespace riwo::concepts

namespace std
{

template <riwo::concepts::enumerate T, riwo::concepts::character CharT>
struct RIWO_CORE_TAPI formatter<T,CharT>
{
	auto format(T e, auto &context) const {
		return m_formatter.format(static_cast<uint32_t>(e), context);
	}
	constexpr auto parse(auto &context) noexcept {
		return m_formatter.parse(context);
	}

private:
	formatter<uint32_t, CharT> m_formatter;
};

template <riwo::concepts::pointer T, riwo::concepts::character CharT>
requires (not is_same_v<std::remove_const_t<T>,void*>)
struct RIWO_CORE_TAPI formatter<T,CharT>
{
	auto format(T p, auto &context) const {
		return m_formatter.format(static_cast<void*>(p), context);
	}
	constexpr auto parse(auto &context) noexcept {
		return m_formatter.parse(context);
	}

private:
	formatter<void*, CharT> m_formatter;
};

#if !defined(_MSC_VER) || !_HAS_CXX23

template <riwo::concepts::character CharT>
struct RIWO_CORE_TAPI formatter<thread::id, CharT>
{
	auto format(const thread::id &tid, auto &context) const
	{
		auto handle = *reinterpret_cast<const thread::native_handle_type*>(&tid);
		return m_formatter.format(riwo::detail::thread_id_helper(handle), context);
	}

	constexpr auto parse(auto &context) noexcept {
		return m_formatter.parse(context);
	}

private:
	formatter<uint64_t, CharT> m_formatter;
};

#endif //_MSC_VER && _HAS_CXX23

template <typename T, riwo::concepts::character CharT>
struct RIWO_CORE_TAPI formatter<optional<T>, CharT>
{
	auto format(const optional<T> &ov, auto &context) const
	{
		if( ov )
			return m_formatter.format(*ov, context);
		return format_to(context.out(), l_str(CharT,"optional(null)"));
	}

	constexpr auto parse(auto &context) noexcept {
		return m_formatter.parse(context);
	}

private:
	formatter<T, CharT> m_formatter;
};

template <typename T, riwo::concepts::character CharT>
struct RIWO_CORE_TAPI formatter<riwo::optional<T>, CharT>
{
	auto format(const riwo::optional<T> &ov, auto &context) const
	{
		if( ov )
			return m_formatter.format(*ov, context);
		return format_to(context.out(), l_str(CharT,"optional(null)"));
	}

	constexpr auto parse(auto &context) noexcept {
		return m_formatter.parse(context);
	}

private:
	formatter<T, CharT> m_formatter;
};

template <typename E, riwo::concepts::character CharT>
struct RIWO_CORE_TAPI formatter<riwo::unexpected<E>, CharT>
{
	auto format(const riwo::unexpected<E> &ov, auto &context) {
		return m_formatter.format(ov.error(), context);
	}
	constexpr auto parse(auto &context) noexcept {
		return m_formatter.parse(context);
	}

private:
	formatter<E, CharT> m_formatter;
};

template <typename T, typename E, riwo::concepts::character CharT>
struct RIWO_CORE_TAPI formatter<riwo::expected<T,E>, CharT>
{
	auto format(const riwo::expected<T,E> &ov, auto &context) const
	{
		m_has_formatter = ov.has_value();
		return m_has_formatter ?
			m_value_formatter.format(*ov, context) :
			m_error_formatter.format(ov.error(), context);
	}

	constexpr auto parse(auto &context) noexcept
	{
		return m_has_formatter ?
			m_value_formatter.parse(context) :
			m_error_formatter.parse(context);
	}

private:
	formatter<T, CharT> m_value_formatter;
	formatter<E, CharT> m_error_formatter;
	mutable bool m_has_formatter = false;
};

template <typename E, riwo::concepts::character CharT>
struct RIWO_CORE_TAPI formatter<riwo::expected<void,E>, CharT>
{
	auto format(const riwo::expected<void,E> &ov, auto &context) const
	{
		m_has_formatter = ov.has_value();
		return m_has_formatter ?
			std::basic_string<CharT>(l_str(CharT,"OK")) :
			m_formatter.format(ov.error(), context);
	}

	constexpr auto parse(auto &context) noexcept
	{
		return m_has_formatter ?
			context.begin() : m_formatter.parse(context);
	}

private:
	formatter<E, CharT> m_formatter;
	mutable bool m_has_formatter = false;
};

#if RIWO_STD_CXX >= 23
template <typename T, typename E, riwo::concepts::character CharT>
struct RIWO_CORE_TAPI formatter<expected<T,E>, CharT>
{
	auto format(const expected<T,E> &ov, auto &context)
	{
		m_has_formatter = ov.has_value();
		return m_has_formatter ?
			m_value_formatter.format(*ov, context) :
			m_error_formatter.format(ov.error(), context);
	}

	constexpr auto parse(auto &context) noexcept
	{
		return m_has_formatter ?
			m_value_formatter.parse(context) :
			m_error_formatter.parse(context);
	}

private:
	formatter<T, CharT> m_value_formatter;
	formatter<E, CharT> m_error_formatter;
	bool m_has_formatter = false;
};
#endif //RIWO_STD_CXX >= 23

template <typename T, riwo::concepts::character CharT>
struct RIWO_CORE_TAPI formatter<atomic<T>, CharT>
{
	auto format(const atomic<T> &n, auto &context) const {
		return m_formatter.format(n.load(), context);
	}
	constexpr auto parse(auto &context) noexcept {
		return m_formatter.parse(context);
	}

private:
	formatter<T, CharT> m_formatter;
};

template <riwo::concepts::character CharT>
struct RIWO_CORE_TAPI formatter<riwo::error_code, CharT> : riwo::no_parse_formatter<CharT>
{
	auto format(const riwo::error_code &error, auto &context) const {
		return format_to(context.out(), l_str(CharT,"{} ({})"), error.message(), error.value());
	}
};

template <typename Protocol, riwo::concepts::character CharT>
struct RIWO_CORE_TAPI formatter<asio::ip::basic_endpoint<Protocol>, CharT> : riwo::no_parse_formatter<CharT>
{
	auto format(const asio::ip::basic_endpoint<Protocol> &endpoint, auto &context) const
	{
		return format_to(context.out(), l_str(CharT,"{}:{}"),
			riwo::strtls::detail::ascii_transition<CharT>(endpoint.address().to_string()), endpoint.port()
		);
	}
};

template <riwo::concepts::character CharT>
struct RIWO_CORE_TAPI formatter<asio::ip::address, CharT>
{
	auto format(const asio::ip::address &addr, auto &context) const {
		return m_formatter.format(riwo::strtls::detail::ascii_transition<CharT>(addr.to_string()), context);
	}
	constexpr auto parse(auto &context) noexcept {
		return m_formatter.parse(context);
	}

private:
	formatter<basic_string<CharT>, CharT> m_formatter;
};

template <typename Fir, typename Sec, riwo::concepts::character CharT>
struct RIWO_CORE_TAPI formatter<pair<Fir,Sec>, CharT> : riwo::no_parse_formatter<CharT>
{
	auto format(const pair<Fir,Sec> &pair, auto &context) const {
		return format_to(context.out(), l_str(CharT,"'{}'-'{}'"), pair.first, pair.second);
	}
};

template <typename T, riwo::concepts::character CharT>
struct RIWO_CORE_TAPI formatter<shared_ptr<T>, CharT> : riwo::no_parse_formatter<CharT>
{
	auto format(const shared_ptr<T> &ptr, auto &context) const
	{
		return format_to(context.out(), l_str(CharT,"{}:({})"),
			riwo::type_name<T>(), reinterpret_cast<void*>(ptr.get())
		);
	}
};

template <riwo::concepts::character CharT>
struct RIWO_CORE_TAPI formatter<filesystem::path, CharT>
{
	auto format(const filesystem::path &path, auto &context) const {
		return m_formatter.format(path.string<CharT>(), context);
	}
	constexpr auto parse(auto &context) noexcept {
		return m_formatter.parse(context);
	}

private:
	formatter<basic_string<CharT>, CharT> m_formatter;
};

template <riwo::concepts::character CharT>
struct RIWO_CORE_TAPI formatter<std::vector<std::byte>, CharT> : riwo::no_parse_formatter<CharT>
{
	auto format(const std::vector<std::byte> &buffer, auto &context) const
	{
		std::basic_string<CharT> text {};
		if( buffer.empty() )
		{
			return format_to(context.out(),
				l_str(CharT,"empty[std::vector<std::byte>]")
			);
		}
		for(auto &byte : buffer)
			text += std::format(l_str(CharT,"0x{:X}, "), byte);
		text.pop_back();
		text.pop_back();

		return format_to(context.out(),
			l_str(CharT,"{}"), text
		);
	}
};

} //namespace std


#endif //RIWO_CORE_UTILS_FORMATTER_H
