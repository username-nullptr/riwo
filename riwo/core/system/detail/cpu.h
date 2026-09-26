// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_SYSTEM_DETAIL_CPU_H
#define RIWO_CORE_SYSTEM_DETAIL_CPU_H

#ifdef _MSC_VER
# include <winnt.h>
#endif

namespace riwo
{

inline void none_instruction() noexcept
{
#ifdef _MSC_VER
	_mm_pause();
#elif defined(__GNUC__)
# if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
	asm volatile("pause" : : : "memory");
# elif defined(__aarch64__) || defined(__arm64__) || defined(__arm__)
	asm volatile("yield" : : : "memory");
# elif defined(__powerpc__) || defined(__ppc__)
	asm volatile("or 0, 0, 0" : : : "memory");
# elif defined(__riscv)
	asm volatile("fence w, 0" : : : "memory");
# else // Unknown
	asm volatile("" : : : "memory");
# endif // CPU Architecture
#endif //_MSC_VER
}

} //namespace riwo


#endif //RIWO_CORE_SYSTEM_DETAIL_CPU_H
