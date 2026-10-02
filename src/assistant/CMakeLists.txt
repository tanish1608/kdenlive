# SPDX-License-Identifier: BSD-2-Clause
# SPDX-FileCopyrightText: 2026 Montaz contributors
set(kdenlive_SRCS ${kdenlive_SRCS}
    assistant/assistantwidget.cpp
    assistant/chatview.cpp
    assistant/editoradapter.cpp
    assistant/editbatch.cpp
    assistant/montage.cpp
    assistant/proposal.cpp
    assistant/workerclient.cpp
    PARENT_SCOPE)

# Reuse MLT/OpenCV from the editor SDK; no separate Python computer-vision stack.
add_executable(montaz-tracker matchtracker.cpp scaleregistration.cpp)
target_link_libraries(montaz-tracker PRIVATE Qt6::Gui Mlt7::mlt++)
set_target_properties(montaz-tracker PROPERTIES MACOSX_BUNDLE FALSE RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
