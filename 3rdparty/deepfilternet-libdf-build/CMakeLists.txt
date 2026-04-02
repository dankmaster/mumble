# Copyright The Mumble Developers. All rights reserved.
# Use of this source code is governed by a BSD-style license
# that can be found in the LICENSE file at the root of the
# Mumble source tree or at <https://www.mumble.info/LICENSE>.

set(DEEPFILTERNET_LIBDF_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../deepfilternet-libdf")
set(DEEPFILTERNET_RUNTIME_FILE "")
set(DEEPFILTERNET_RUNTIME_ENABLED OFF)

add_custom_target(deepfilternet_runtime)

find_program(CARGO_EXECUTABLE
	NAMES cargo cargo.exe
	HINTS "$ENV{USERPROFILE}/.cargo/bin"
)

if(CARGO_EXECUTABLE)
	set(DEEPFILTERNET_CARGO_TARGET_DIR "${CMAKE_CURRENT_BINARY_DIR}/cargo-target")
	set(DEEPFILTERNET_RUNTIME_FILE "${DEEPFILTERNET_CARGO_TARGET_DIR}/release/deepfilter.dll")
	set(DEEPFILTERNET_RUNTIME_ENABLED ON)

	add_custom_command(TARGET deepfilternet_runtime
		COMMAND ${CMAKE_COMMAND} -E env
			"CARGO_TARGET_DIR=${DEEPFILTERNET_CARGO_TARGET_DIR}"
			"${CARGO_EXECUTABLE}" build
				--manifest-path "${DEEPFILTERNET_LIBDF_ROOT}/Cargo.toml"
				--release
				--no-default-features
				--features capi,default-model
		WORKING_DIRECTORY "${DEEPFILTERNET_LIBDF_ROOT}"
		USES_TERMINAL
		VERBATIM
	)
else()
	message(WARNING "DeepFilterNet support was requested but cargo was not found. The runtime DLL will not be built automatically.")
endif()

set(DEEPFILTERNET_RUNTIME_FILE "${DEEPFILTERNET_RUNTIME_FILE}" PARENT_SCOPE)
set(DEEPFILTERNET_RUNTIME_ENABLED "${DEEPFILTERNET_RUNTIME_ENABLED}" PARENT_SCOPE)
