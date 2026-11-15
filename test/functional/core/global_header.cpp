// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/core/global.h>

#ifndef RIWO_CORE_CXX_H
# error "core/global.h must provide the public C++ support chain"
#endif

#ifndef RIWO_CORE_CXX_FUNCTION_TRAITS_H
# error "core/global.h must preserve the transitive C++ support chain"
#endif

#ifndef RIWO_CORE_CXX_MEMORY_CONCEPTS_H
# error "core/global.h must provide the C++ concept roots"
#endif

#ifndef RIWO_CORE_ASIO_H
# error "core/global.h must provide the public Asio support chain"
#endif

#ifndef RIWO_CORE_ASIO_ASIO_H
# error "core/global.h must provide the configured Asio provider"
#endif

#ifndef RIWO_CORE_ASIO_ENDPOINT_H
# error "core/global.h must preserve the transitive Asio support chain"
#endif

#ifdef RIWO_CORE_OPTIONAL_H
# error "core/optional.h must be included explicitly"
#endif

#ifdef RIWO_CORE_EXPECTED_H
# error "core/expected.h must be included explicitly"
#endif

#ifdef RIWO_CORE_SYS_EXPECTED_H
# error "core/sys_expected.h must be included explicitly"
#endif

#ifdef RIWO_CORE_FORMATTERS_H
# error "core/formatters.h must be included explicitly"
#endif

int main() {}
