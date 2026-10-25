// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_PROTOCOL_FIELDS_H
#define RIWO_HTTP_EXP_PROTOCOL_FIELDS_H

#include <riwo/http_exp/global.h>
#include <riwo/core/value.h>

namespace riwo::http_exp
{

struct field
{
	std::string name;
	riwo::value value;

	friend bool operator== (
		const field&, const field&
	) = default;
};

class fields;
class RIWO_HTTP_EXP_API field_range
{
public:
	field_range(const field_range&) = default;
	field_range &operator=(const field_range&) = default;

	field_range(field_range &&other) noexcept;
	field_range &operator=(field_range &&other) noexcept;

	class RIWO_HTTP_EXP_API const_iterator
	{
	public:
		using iterator_category = std::forward_iterator_tag;
		using value_type = field;

		using difference_type = std::ptrdiff_t;
		using pointer = const field*;

		using reference = const field&;

	public:
		const_iterator();
		~const_iterator();

		const_iterator(const const_iterator &other);
		const_iterator &operator=(const const_iterator &other);

		const_iterator(const_iterator &&other) noexcept;
		const_iterator &operator=(const_iterator &&other) noexcept;

	public:
		reference operator*() const noexcept;
		pointer operator->() const noexcept;

		const_iterator &operator++() noexcept;
		const_iterator operator++(int) noexcept;

		friend RIWO_HTTP_EXP_API bool operator== (
			const const_iterator &lhs, const const_iterator &rhs
		) noexcept;

	private:
		friend class field_range;
		using base_iterator_t = std::vector<field>::const_iterator;

		const_iterator(base_iterator_t current,
			base_iterator_t end, std::string_view name
		) noexcept;

		void skip() noexcept;

		base_iterator_t m_current {};
		base_iterator_t m_end {};

		std::string_view m_name {};
		bool m_initialized = false;
	};

	[[nodiscard]] const_iterator begin() const noexcept;
	[[nodiscard]] const_iterator end() const noexcept;

private:
	friend class fields;
	field_range(const std::vector<field> &values, std::string name);

	const std::vector<field> *m_values = nullptr;
	std::string m_name {};
};

class RIWO_HTTP_EXP_API fields
{
public:
	using container_t = std::vector<field>;
	using const_iterator = container_t::const_iterator;

	fields();
	~fields();

	fields(const fields &other);
	fields &operator=(const fields &other);

	fields(fields &&other) noexcept;
	fields &operator=(fields &&other) noexcept;

public:
	fields &add(core_concepts::text_p<char> auto &&name, riwo::value value);
	fields &set(core_concepts::text_p<char> auto &&name, riwo::value value);
	fields &erase(const core_concepts::text_p<char> auto &name);

	[[nodiscard]] field_range find_all (
		const core_concepts::text_p<char> auto &name
	) const;

	[[nodiscard]] optional<std::string_view> find_first (
		const core_concepts::text_p<char> auto &name
	) const noexcept;

	[[nodiscard]] const_iterator begin() const noexcept;
	[[nodiscard]] const_iterator end() const noexcept;

	[[nodiscard]] bool empty() const noexcept;
	[[nodiscard]] size_t size() const noexcept;

private:
	fields &add_impl(std::string name, riwo::value value);
	fields &set_impl(std::string name, riwo::value value);
	fields &erase_impl(std::string_view name);

	[[nodiscard]] field_range find_all_impl(std::string name) const;
	[[nodiscard]] optional<std::string_view> find_first_impl(std::string_view name) const noexcept;

	container_t m_values {};
};

} //namespace riwo::http_exp
#include <riwo/http_exp/protocol/detail/fields.h>


#endif //RIWO_HTTP_EXP_PROTOCOL_FIELDS_H
