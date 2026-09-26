// SPDX-FileCopyrightText: 2024 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_ALGORITHM_SHA1_H
#define RIWO_CORE_ALGORITHM_SHA1_H

#include <riwo/core/global.h>

namespace riwo
{

class RIWO_CORE_API sha1
{
public:
	sha1();
	sha1(std::string_view text);
	sha1(const sha1 &other);
	sha1(sha1 &&other) noexcept;
	~sha1();

public:
	sha1 &operator=(const sha1 &other);
	sha1 &operator=(sha1 &&other) noexcept;

public:
	sha1 &append(uint8_t x);
	sha1 &append(char c);
	sha1 &append(const void *data, size_t size);
	sha1 &append(std::string_view text);

public:
	void operator+=(uint8_t x);
	void operator+=(char c);
	void operator+=(wchar_t c);
	void operator+=(const std::string &text);

public:
	sha1 &finalize();
	[[nodiscard]] std::string hex(bool upper_case = true) const;
	[[nodiscard]] std::string base64() const;

private:
	class impl;
	impl *m_impl;
};

} //namespace riwo


#endif //RIWO_CORE_ALGORITHM_SHA1_H
