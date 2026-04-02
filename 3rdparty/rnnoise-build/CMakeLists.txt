# Copyright The Mumble Developers. All rights reserved.
# Use of this source code is governed by a BSD-style license
# that can be found in the LICENSE file at the root of the
# Mumble source tree or at <https://www.mumble.info/LICENSE>.

include(FetchContent)

set(RNNOISE_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../rnnoise-src")
set(RNNOISE_MODEL_URL "https://media.xiph.org/rnnoise/models/rnnoise_data-")

if(NOT EXISTS "${RNNOISE_SRC_DIR}/COPYING")
	message(FATAL_ERROR
		"${RNNOISE_SRC_DIR} was not found.\n"
		"Please checkout the submodule:\n"
		"git submodule update --init --recursive"
	)
endif()

if(WIN32)
	add_library(rnnoise SHARED)
	set_target_properties(rnnoise PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
	if(MINGW)
		# Remove "lib" prefix.
		set_target_properties(rnnoise PROPERTIES PREFIX "")
	endif()
	target_compile_definitions(rnnoise
		PRIVATE
			"WIN32"
			"DLL_EXPORT"
	)
else()
	add_library(rnnoise STATIC)
endif()

target_compile_definitions(rnnoise PRIVATE "HAVE_CONFIG_H")

target_include_directories(rnnoise
	PRIVATE SYSTEM
		${CMAKE_CURRENT_SOURCE_DIR}
		"${CMAKE_CURRENT_BINARY_DIR}/generated"
	PUBLIC SYSTEM
		"${RNNOISE_SRC_DIR}/include"
		"${RNNOISE_SRC_DIR}/src"
)

file(READ "${RNNOISE_SRC_DIR}/model_version" rnnoise_model_hash)
string(STRIP ${rnnoise_model_hash} rnnoise_model_hash)
string(CONCAT rnnoise_model_url "${RNNOISE_MODEL_URL}" "${rnnoise_model_hash}" ".tar.gz")

FetchContent_Declare(
  rnnoise_model
  URL      "${rnnoise_model_url}"
  URL_HASH SHA256=${rnnoise_model_hash}
  DOWNLOAD_EXTRACT_TIMESTAMP 1
)

FetchContent_MakeAvailable(rnnoise_model)

set(RNNOISE_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(RNNOISE_ASSET_OUTPUT_DIR "${CMAKE_BINARY_DIR}/rnnoise")
file(MAKE_DIRECTORY "${RNNOISE_GENERATED_DIR}")

add_custom_command(
    OUTPUT
        "${RNNOISE_GENERATED_DIR}/rnnoise_data.c"
        "${RNNOISE_GENERATED_DIR}/rnnoise_data.h"
        "${RNNOISE_GENERATED_DIR}/rnnoise_data_little.c"
        "${RNNOISE_GENERATED_DIR}/rnnoise_data_little.h"

    WORKING_DIRECTORY "${RNNOISE_SRC_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E copy
        "${rnnoise_model_SOURCE_DIR}/src/rnnoise_data.c"
        "${rnnoise_model_SOURCE_DIR}/src/rnnoise_data.h"
        "${rnnoise_model_SOURCE_DIR}/src/rnnoise_data_little.c"
        "${rnnoise_model_SOURCE_DIR}/src/rnnoise_data_little.h"
        "${RNNOISE_GENERATED_DIR}"
    COMMENT "Downloading RNNoise model files"
)

add_custom_target(rnnoise_generated_model_files
	DEPENDS
		"${RNNOISE_GENERATED_DIR}/rnnoise_data.c"
		"${RNNOISE_GENERATED_DIR}/rnnoise_data.h"
		"${RNNOISE_GENERATED_DIR}/rnnoise_data_little.c"
		"${RNNOISE_GENERATED_DIR}/rnnoise_data_little.h"
)

set(RNNOISE_CORE_SOURCES
	"${RNNOISE_SRC_DIR}/src/rnnoise_tables.c"
	"${RNNOISE_SRC_DIR}/src/rnn.c"
	"${RNNOISE_SRC_DIR}/src/pitch.c"
	"${RNNOISE_SRC_DIR}/src/nnet.c"
	"${RNNOISE_SRC_DIR}/src/nnet_default.c"
	"${RNNOISE_SRC_DIR}/src/parse_lpcnet_weights.c"
	"${RNNOISE_SRC_DIR}/src/kiss_fft.c"
	"${RNNOISE_SRC_DIR}/src/denoise.c"
	"${RNNOISE_SRC_DIR}/src/celt_lpc.c"
)

target_sources(rnnoise PRIVATE
	"${RNNOISE_GENERATED_DIR}/rnnoise_data.c"
	${RNNOISE_CORE_SOURCES}
)

add_dependencies(rnnoise rnnoise_generated_model_files)

file(READ "${RNNOISE_SRC_DIR}/src/write_weights.c" RNNOISE_WRITE_WEIGHTS_SOURCE)
string(REPLACE "\"rnnoise_data.c\"" "\"rnnoise_data_little.c\""
	RNNOISE_WRITE_WEIGHTS_LITTLE_SOURCE "${RNNOISE_WRITE_WEIGHTS_SOURCE}")
string(REPLACE "\"weights_blob.bin\"" "\"rnnoise_little.weights_blob.bin\""
	RNNOISE_WRITE_WEIGHTS_LITTLE_SOURCE "${RNNOISE_WRITE_WEIGHTS_LITTLE_SOURCE}")
string(REPLACE "fopen(\"rnnoise_little.weights_blob.bin\", \"w\")" "fopen(\"rnnoise_little.weights_blob.bin\", \"wb\")"
	RNNOISE_WRITE_WEIGHTS_LITTLE_SOURCE "${RNNOISE_WRITE_WEIGHTS_LITTLE_SOURCE}")
file(WRITE "${RNNOISE_GENERATED_DIR}/write_weights_little.c" "${RNNOISE_WRITE_WEIGHTS_LITTLE_SOURCE}")

add_executable(rnnoise_write_little_weights EXCLUDE_FROM_ALL
	"${RNNOISE_GENERATED_DIR}/write_weights_little.c"
	${RNNOISE_CORE_SOURCES}
)
add_dependencies(rnnoise_write_little_weights rnnoise_generated_model_files)
target_compile_definitions(rnnoise_write_little_weights PRIVATE "HAVE_CONFIG_H")
target_include_directories(rnnoise_write_little_weights
	PRIVATE SYSTEM
		${CMAKE_CURRENT_SOURCE_DIR}
		"${RNNOISE_GENERATED_DIR}"
		"${RNNOISE_SRC_DIR}/include"
		"${RNNOISE_SRC_DIR}/src"
)
set_target_properties(rnnoise_write_little_weights PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
target_disable_warnings(rnnoise_write_little_weights)

set(RNNOISE_LITTLE_WEIGHTS_BLOB "${RNNOISE_ASSET_OUTPUT_DIR}/rnnoise_little.weights_blob.bin")
add_custom_command(
	OUTPUT "${RNNOISE_LITTLE_WEIGHTS_BLOB}"
	COMMAND "${CMAKE_COMMAND}" -E make_directory "${RNNOISE_ASSET_OUTPUT_DIR}"
	COMMAND rnnoise_write_little_weights
	WORKING_DIRECTORY "${RNNOISE_ASSET_OUTPUT_DIR}"
	DEPENDS rnnoise_write_little_weights
	COMMENT "Generating RNNoise little weights blob"
)
add_custom_target(rnnoise_little_weights_blob ALL DEPENDS "${RNNOISE_LITTLE_WEIGHTS_BLOB}")

set(RNNOISE_LITTLE_WEIGHTS_BLOB "${RNNOISE_LITTLE_WEIGHTS_BLOB}" PARENT_SCOPE)

target_disable_warnings(rnnoise)
