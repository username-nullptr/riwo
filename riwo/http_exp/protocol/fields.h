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

	friend bool operator==(const field &, const field &) = default;
};

class fields;

class RIWO_HTTP_EXP_API field_range
{
public:
	field_range(const field_range &) = default;
	field_range &operator=(const field_range &) = default;
	field_range(field_range &&other) noexcept;
	field_range &operator=(field_range &&other) noexcept;

	class RIWO_HTTP_EXP_API const_iterator
	{
	public:
		using iterator_category = std::forward_iterator_tag;
		using value_type = field;
		using difference_type = std::ptrdiff_t;
		using pointer = const field *;
		using reference = const field &;

		const_iterator();
		~const_iterator();
		const_iterator(const const_iterator &other);
		const_iterator &operator=(const const_iterator &other);
		const_iterator(const_iterator &&other) noexcept;
		const_iterator &operator=(const_iterator &&other) noexcept;

		reference operator*() const noexcept;
		pointer operator->() const noexcept;
		const_iterator &operator++() noexcept;
		const_iterator operator++(int) noexcept;
		friend RIWO_HTTP_EXP_API bool operator==(const const_iterator &lhs,
			const const_iterator &rhs) noexcept;

	private:
		friend class field_range;
		using base_iterator_t = std::vector<field>::const_iterator;
		const_iterator(base_iterator_t current, base_iterator_t end,
			std::string_view name) noexcept;

		class impl;
		std::unique_ptr<impl> m_impl;
	};

	[[nodiscard]] const_iterator begin() const noexcept;
	[[nodiscard]] const_iterator end() const noexcept;

private:
	friend class fields;
	field_range(const std::vector<field> &values, std::string name);
	class impl;
	std::shared_ptr<impl> m_impl;
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

	fields &add(std::string name, riwo::value value);
	fields &set(std::string name, riwo::value value);
	fields &erase(std::string_view name);

	[[nodiscard]] field_range find_all(std::string_view name) const;
	[[nodiscard]] optional<std::string_view> find_first(std::string_view name) const;

	[[nodiscard]] const_iterator begin() const noexcept;
	[[nodiscard]] const_iterator end() const noexcept;
	[[nodiscard]] bool empty() const noexcept;
	[[nodiscard]] size_t size() const noexcept;

private:
	class impl;
	std::unique_ptr<impl> m_impl;
};

} //namespace riwo::http_exp

#endif //RIWO_HTTP_EXP_PROTOCOL_FIELDS_H
