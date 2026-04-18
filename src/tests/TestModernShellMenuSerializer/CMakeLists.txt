# Copyright The Mumble Developers. All rights reserved.
# Use of this source code is governed by a BSD-style license
# that can be found in the LICENSE file at the root of the
# Mumble source tree or at <https://www.mumble.info/LICENSE>.

add_executable(TestModernShellMenuSerializer
	TestModernShellMenuSerializer.cpp
)

set_target_properties(TestModernShellMenuSerializer PROPERTIES AUTOMOC ON)

target_link_libraries(TestModernShellMenuSerializer PRIVATE mumble_client_object_lib shared Qt6::Test)

add_test(NAME TestModernShellMenuSerializer COMMAND $<TARGET_FILE:TestModernShellMenuSerializer>)
