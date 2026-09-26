// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#if defined(_WIN32)
# define RIWO_EXAMPLE_EXPORT __declspec(dllexport)
#elif defined(__GNUC__)
# define RIWO_EXAMPLE_EXPORT __attribute__((visibility("default")))
#else
# define RIWO_EXAMPLE_EXPORT
#endif

extern "C" RIWO_EXAMPLE_EXPORT int riwo_example_twice(int value)
{
	return value * 2;
}
