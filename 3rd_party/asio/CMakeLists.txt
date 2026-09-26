install(FILES asio.hpp DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(DIRECTORY asio DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
	# Riwo provides Asio's separately compiled implementation.  Keep the
	# impl/*.hpp template headers required by the public API, but do not expose
	# the implementation units that would let consumers compile another copy.
	PATTERN "*.ipp" EXCLUDE
	PATTERN "*/impl/src.hpp" EXCLUDE
)
