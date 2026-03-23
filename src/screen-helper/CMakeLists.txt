# Copyright The Mumble Developers. All rights reserved.
# Use of this source code is governed by a BSD-style license
# that can be found in the LICENSE file at the root of the
# Mumble source tree or at <https://www.mumble.info/LICENSE>.

add_executable(mumble-screen-helper
	"main.cpp"
	"ScreenShareExternalProcess.cpp"
	"ScreenShareExternalProcess.h"
	"ScreenShareHelperServer.cpp"
	"ScreenShareHelperServer.h"
	"ScreenShareMediaSupport.cpp"
	"ScreenShareMediaSupport.h"
	"ScreenShareRelayClient.cpp"
	"ScreenShareRelayClient.h"
	"ScreenShareSessionPlanner.cpp"
	"ScreenShareSessionPlanner.h"
)

set_target_properties(mumble-screen-helper
	PROPERTIES
		AUTOMOC ON
		OUTPUT_NAME "mumble-screen-helper"
		RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
		UNITY_BUILD OFF
)

target_link_libraries(mumble-screen-helper
	PRIVATE
		shared
		Qt6::Core
		Qt6::Network
)

install(TARGETS mumble-screen-helper RUNTIME DESTINATION "${MUMBLE_INSTALL_EXECUTABLEDIR}" COMPONENT mumble_client)
