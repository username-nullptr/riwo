# SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
# SPDX-License-Identifier: MIT

function(riwo_enable_boost_asio_tsan_headers boost_headers_target)
	set(relative_header boost/asio/detail/atomic_count.hpp)
	set(source_header "")

	get_target_property(boost_include_dirs ${boost_headers_target}
		INTERFACE_INCLUDE_DIRECTORIES
	)
	foreach(directory IN LISTS boost_include_dirs)
		if (EXISTS "${directory}/${relative_header}")
			set(source_header "${directory}/${relative_header}")
			break()
		endif ()
	endforeach ()

	if (NOT source_header)
		return()
	endif ()

	set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${source_header}")
	file(READ "${source_header}" header)

	string(REPLACE "\r\n" "\n" header "${header}")
	set(legacy_decrement [=[inline bool ref_count_down(atomic_count& a)
{
  if (a.fetch_sub(1, std::memory_order_release) == 1)
  {
    std::atomic_thread_fence(std::memory_order_acquire);
    return true;
  }
  return false;
}]=])
	# The last decrement must acquire the preceding releases before deletion.
	# TSan cannot see that edge when it is expressed as a separate fence.
	# Only adapt this known implementation so annotated Boost versions are used as-is.
	string(FIND "${header}" "${legacy_decrement}" legacy_position)

	if (legacy_position EQUAL -1)
		return()
	endif ()

	set(instrumentable_decrement [=[inline bool ref_count_down(atomic_count& a)
{
  return a.fetch_sub(1, std::memory_order_acq_rel) == 1;
}]=])
	string(REPLACE "${legacy_decrement}" "${instrumentable_decrement}" header "${header}")

	set(overlay "${CMAKE_CURRENT_BINARY_DIR}/boost-asio-tsan")
	set(overlay_header "${overlay}/${relative_header}")
	set(previous_header "")

	if (EXISTS "${overlay_header}")
		file(READ "${overlay_header}" previous_header)
	endif ()

	if (NOT header STREQUAL previous_header)
		file(WRITE "${overlay_header}" "${header}")
	endif ()

	# Keep the same inline definition in Riwo, its tests and installed consumers.
	set(install_overlay "${CMAKE_INSTALL_INCLUDEDIR}/riwo/detail/boost-asio-tsan")

	target_include_directories(riwo.asio BEFORE INTERFACE
		"$<BUILD_INTERFACE:${overlay}>"
		"$<INSTALL_INTERFACE:${install_overlay}>"
	)
	install(FILES "${overlay_header}" DESTINATION "${install_overlay}/boost/asio/detail")
	install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/asio/LICENSE_1_0.txt" DESTINATION "${install_overlay}")

	message(STATUS "${PRO_NAME}: Use TSan-compatible Boost.Asio reference counting.")
endfunction()
