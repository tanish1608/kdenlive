# SPDX-License-Identifier: BSD-2-Clause
# SPDX-FileCopyrightText: 2026 Montaz contributors
QT += core testlib
QT -= gui
CONFIG += console c++17 testcase
CONFIG -= app_bundle
macx:contains(QT_ARCH, arm64): QMAKE_CXXFLAGS += -include arm_acle.h
TEMPLATE = app
TARGET = montaz-client-test
INCLUDEPATH += ../../../src/assistant
SOURCES += clienttest.cpp ../../../src/assistant/proposal.cpp ../../../src/assistant/workerclient.cpp
SOURCES += ../../../src/assistant/editbatch.cpp
HEADERS += ../../../src/assistant/proposal.h ../../../src/assistant/workerclient.h
HEADERS += ../../../src/assistant/editbatch.h
