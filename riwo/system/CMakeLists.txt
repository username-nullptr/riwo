# SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
# SPDX-License-Identifier: MIT

set(target_name riwo.system)
add_project(${target_name} riwo.core)

if (WIN32)
	target_link_libraries(${target_name} PUBLIC advapi32 ole32 shell32)
endif ()

install(FILES ../system.h
	DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/riwo
)
install(DIRECTORY ./
	DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/riwo/system
	FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp" PATTERN "*.ipp"
)
