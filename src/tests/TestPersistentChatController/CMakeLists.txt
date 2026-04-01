# Copyright The Mumble Developers. All rights reserved.
# Use of this source code is governed by a BSD-style license
# that can be found in the LICENSE file at the root of the
# Mumble source tree or at <https://www.mumble.info/LICENSE>.

add_executable(TestPersistentChatController
	TestPersistentChatController.cpp
	../../mumble/PersistentChatController.cpp
	../../mumble/PersistentChatStore.cpp
)

set_target_properties(TestPersistentChatController PROPERTIES AUTOMOC ON)

target_link_libraries(TestPersistentChatController PRIVATE shared Qt6::Test)

add_test(NAME TestPersistentChatController COMMAND $<TARGET_FILE:TestPersistentChatController>)
