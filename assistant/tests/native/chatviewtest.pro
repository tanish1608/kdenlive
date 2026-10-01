# SPDX-License-Identifier: BSD-2-Clause
# SPDX-FileCopyrightText: 2026 Montaz contributors
QT += core widgets testlib
CONFIG += console c++17 testcase
CONFIG -= app_bundle
macx:contains(QT_ARCH, arm64): QMAKE_CXXFLAGS += -include arm_acle.h
TEMPLATE = app
TARGET = montaz-chat-test
INCLUDEPATH += ../../../src/assistant
SOURCES += chatviewtest.cpp ../../../src/assistant/chatview.cpp
HEADERS += ../../../src/assistant/chatview.h
