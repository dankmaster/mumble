# Copyright The Mumble Developers. All rights reserved.
# Use of this source code is governed by a BSD-style license
# that can be found in the LICENSE file at the root of the
# Mumble source tree or at <https://www.mumble.info/LICENSE>.

set(DEEPFILTERNET_LIBDF_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../deepfilternet-libdf")
set(DEEPFILTERNET_RUNTIME_FILE "")
set(DEEPFILTERNET_RUNTIME_ENABLED OFF)
set(DEEPFILTERNET_MODEL_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../src/mumble/deepfilternet")
set(DEEPFILTERNET_EMBEDDED_MODEL_DIR "${CMAKE_CURRENT_BINARY_DIR}/models")

find_program(CARGO_EXECUTABLE
	NAMES cargo cargo.exe
	HINTS "$ENV{USERPROFILE}/.cargo/bin"
)

if(CARGO_EXECUTABLE)
	set(DEEPFILTERNET_CARGO_TARGET_DIR "${CMAKE_CURRENT_BINARY_DIR}/cargo-target")
	set(DEEPFILTERNET_RUNTIME_FILE "${DEEPFILTERNET_CARGO_TARGET_DIR}/release/deepfilter.dll")
	set(DEEPFILTERNET_RUNTIME_ENABLED ON)
	set(DEEPFILTERNET_CARGO_ENV
		"CARGO_TARGET_DIR=${DEEPFILTERNET_CARGO_TARGET_DIR}"
		"DEEPFILTERNET_MODEL_DIR=${DEEPFILTERNET_EMBEDDED_MODEL_DIR}"
	)

	if(WIN32)
		set(DEEPFILTERNET_MSVC_LINKER "")

		if(DEFINED ENV{VCTOOLSINSTALLDIR} AND NOT "$ENV{VCTOOLSINSTALLDIR}" STREQUAL "")
			file(TO_CMAKE_PATH "$ENV{VCTOOLSINSTALLDIR}" DEEPFILTERNET_VCTOOLSINSTALLDIR)
			set(DEEPFILTERNET_MSVC_LINKER "${DEEPFILTERNET_VCTOOLSINSTALLDIR}/bin/Hostx64/x64/link.exe")
		endif()

		if(EXISTS "${DEEPFILTERNET_MSVC_LINKER}")
			list(APPEND DEEPFILTERNET_CARGO_ENV
				"CARGO_TARGET_X86_64_PC_WINDOWS_MSVC_LINKER=${DEEPFILTERNET_MSVC_LINKER}"
			)
		else()
			message(WARNING "DeepFilterNet support was requested but the MSVC linker path could not be resolved. Cargo may pick the wrong link.exe from PATH.")
		endif()
	endif()

	add_custom_target(deepfilternet_runtime
		COMMAND ${CMAKE_COMMAND} -E make_directory "${DEEPFILTERNET_EMBEDDED_MODEL_DIR}"
		COMMAND ${CMAKE_COMMAND} -E copy_directory
			"${DEEPFILTERNET_MODEL_SOURCE_DIR}"
			"${DEEPFILTERNET_EMBEDDED_MODEL_DIR}"
		COMMAND ${CMAKE_COMMAND} -E env
			${DEEPFILTERNET_CARGO_ENV}
			"${CARGO_EXECUTABLE}" build
				--manifest-path "${DEEPFILTERNET_LIBDF_ROOT}/Cargo.toml"
				--release
				--no-default-features
				--features capi,default-model
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			"${DEEPFILTERNET_CARGO_TARGET_DIR}/release/df.dll"
			"${DEEPFILTERNET_RUNTIME_FILE}"
		BYPRODUCTS "${DEEPFILTERNET_RUNTIME_FILE}"
		WORKING_DIRECTORY "${DEEPFILTERNET_LIBDF_ROOT}"
		USES_TERMINAL
		VERBATIM
	)
else()
	add_custom_target(deepfilternet_runtime)
	message(WARNING "DeepFilterNet support was requested but cargo was not found. The runtime DLL will not be built automatically.")
endif()

set(DEEPFILTERNET_RUNTIME_FILE "${DEEPFILTERNET_RUNTIME_FILE}" PARENT_SCOPE)
set(DEEPFILTERNET_RUNTIME_ENABLED "${DEEPFILTERNET_RUNTIME_ENABLED}" PARENT_SCOPE)
