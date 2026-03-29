# Copyright The Mumble Developers. All rights reserved.
# Use of this source code is governed by a BSD-style license
# that can be found in the LICENSE file at the root of the
# Mumble source tree or at <https://www.mumble.info/LICENSE>.

add_executable(TestPersistentChatRender
	TestPersistentChatRender.cpp
	../../mumble/PersistentChatRender.cpp
)

set_target_properties(TestPersistentChatRender PROPERTIES AUTOMOC ON)

target_link_libraries(TestPersistentChatRender PRIVATE shared Qt6::Test)

add_test(NAME TestPersistentChatRender COMMAND $<TARGET_FILE:TestPersistentChatRender>)
