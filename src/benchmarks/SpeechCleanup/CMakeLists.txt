# Copyright The Mumble Developers. All rights reserved.
# Use of this source code is governed by a BSD-style license
# that can be found in the LICENSE file at the root of the
# Mumble source tree or at <https://www.mumble.info/LICENSE>.

add_executable(speech_cleanup_benchmark "speech_cleanup_benchmark.cpp")

target_link_libraries(speech_cleanup_benchmark
	PRIVATE
		mumble_client_object_lib
)

target_include_directories(speech_cleanup_benchmark
	PRIVATE
		"${CMAKE_SOURCE_DIR}/src/mumble"
)

set_target_properties(speech_cleanup_benchmark
	PROPERTIES
		RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
