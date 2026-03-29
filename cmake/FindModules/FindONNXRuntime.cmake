# Copyright The Mumble Developers. All rights reserved.
# Use of this source code is governed by a BSD-style license
# that can be found in the LICENSE file at the root of the
# Mumble source tree or at <https://www.mumble.info/LICENSE>.

set(_ONNXRUNTIME_HINTS "")

if(DEFINED ONNXRUNTIME_ROOT AND NOT "${ONNXRUNTIME_ROOT}" STREQUAL "")
	list(APPEND _ONNXRUNTIME_HINTS "${ONNXRUNTIME_ROOT}")
endif()

if(DEFINED ENV{ONNXRUNTIME_ROOT} AND NOT "$ENV{ONNXRUNTIME_ROOT}" STREQUAL "")
	list(APPEND _ONNXRUNTIME_HINTS "$ENV{ONNXRUNTIME_ROOT}")
endif()

find_path(ONNXRUNTIME_INCLUDE_DIR
	NAMES onnxruntime_cxx_api.h
	HINTS ${_ONNXRUNTIME_HINTS}
	PATH_SUFFIXES include
)

find_library(ONNXRUNTIME_LIBRARY
	NAMES onnxruntime
	HINTS ${_ONNXRUNTIME_HINTS}
	PATH_SUFFIXES lib lib64
)

if(WIN32)
	find_file(ONNXRUNTIME_RUNTIME
		NAMES onnxruntime.dll
		HINTS ${_ONNXRUNTIME_HINTS}
		PATH_SUFFIXES lib lib64 bin
	)
elseif(APPLE)
	find_file(ONNXRUNTIME_RUNTIME
		NAMES libonnxruntime.dylib
		HINTS ${_ONNXRUNTIME_HINTS}
		PATH_SUFFIXES lib lib64 bin
	)
else()
	find_file(ONNXRUNTIME_RUNTIME
		NAMES libonnxruntime.so
		HINTS ${_ONNXRUNTIME_HINTS}
		PATH_SUFFIXES lib lib64 bin
	)
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(ONNXRuntime
	REQUIRED_VARS ONNXRUNTIME_INCLUDE_DIR ONNXRUNTIME_LIBRARY
)

if(ONNXRUNTIME_FOUND AND NOT TARGET ONNXRuntime::ONNXRuntime)
	if(WIN32)
		add_library(ONNXRuntime::ONNXRuntime SHARED IMPORTED)
		set_target_properties(ONNXRuntime::ONNXRuntime PROPERTIES
			IMPORTED_IMPLIB "${ONNXRUNTIME_LIBRARY}"
			IMPORTED_LOCATION "${ONNXRUNTIME_RUNTIME}"
			INTERFACE_INCLUDE_DIRECTORIES "${ONNXRUNTIME_INCLUDE_DIR}"
		)
	else()
		add_library(ONNXRuntime::ONNXRuntime SHARED IMPORTED)
		set_target_properties(ONNXRuntime::ONNXRuntime PROPERTIES
			IMPORTED_LOCATION "${ONNXRUNTIME_LIBRARY}"
			INTERFACE_INCLUDE_DIRECTORIES "${ONNXRUNTIME_INCLUDE_DIR}"
		)
	endif()
endif()

mark_as_advanced(
	ONNXRUNTIME_INCLUDE_DIR
	ONNXRUNTIME_LIBRARY
	ONNXRUNTIME_RUNTIME
)
