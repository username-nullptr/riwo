// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_PROTOCOL_FIELDS_H
#define RIWO_HTTP_EXP_PROTOCOL_FIELDS_H

#include <riwo/http_exp/global.h>

namespace riwo::http_exp
{

struct field
{
	std::string name;
	std::string value;

	friend bool operator==(const field &, const field &) = default;
};

class fields;

class RIWO_HTTP_EXP_API field_range
{
public:
	class RIWO_HTTP_EXP_API const_iterator
	{
	public:
		using iterator_category = std::forward_iterator_tag;
		using value_type = field;
		using difference_type = std::ptrdiff_t;
		using pointer = const field *;
		using reference = const field &;

		const_iterator() = default;
		reference operator*() const noexcept;
		pointer operator->() const noexcept;
		const_iterator &operator++() noexcept;
		const_iterator operator++(int) noexcept;
		friend bool operator==(const const_iterator &,
			const const_iterator &) noexcept = default;

	private:
		friend class field_range;
		using base_iterator = std::vector<field>::const_iterator;
		const_iterator(base_iterator current, base_iterator end,
			std::string_view name) noexcept;
		void skip() noexcept;

		base_iterator m_current {};
		base_iterator m_end {};
		std::string_view m_name {};
	};

	[[nodiscard]] const_iterator begin() const noexcept;
	[[nodiscard]] const_iterator end() const noexcept;

private:
	friend class fields;
	field_range(const std::vector<field> &values, std::string name);
	const std::vector<field> *m_values;
	std::string m_name;
};

class RIWO_HTTP_EXP_API fields
{
public:
	using container_type = std::vector<field>;
	using const_iterator = container_type::const_iterator;

	void add(std::string name, std::string value);
	void set(std::string name, std::string value);
	void erase(std::string_view name);

	[[nodiscard]] field_range find_all(std::string_view name) const;
	[[nodiscard]] optional<std::string_view> find_first(std::string_view name) const;

	[[nodiscard]] const_iterator begin() const noexcept;
	[[nodiscard]] const_iterator end() const noexcept;
	[[nodiscard]] bool empty() const noexcept;
	[[nodiscard]] size_t size() const noexcept;

private:
	container_type m_values;
};

} //namespace riwo::http_exp

#endif //RIWO_HTTP_EXP_PROTOCOL_FIELDS_H
