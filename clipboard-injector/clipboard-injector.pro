# Define the project name
TEMPLATE = lib
TARGET = clipboard-injector
CONFIG += shared plugin no_plugin_name_prefix
QMAKE_LFLAGS += -Wl,--no-undefined

# Configure build directories
OBJECTS_DIR = build/obj
MOC_DIR = build/moc
UI_DIR = build/ui
XOVI_DIR = build/xovi

xoviextension.target = build/xovi/xovi.c
xoviextension.commands = mkdir -p $$XOVI_DIR && python3 $$(XOVI_REPO)/util/xovigen.py -o $$XOVI_DIR/xovi.c -H $$XOVI_DIR/xovi.h clipboard-injector.xovi
xoviextension.depends = clipboard-injector.xovi clipboard-injector.qmd

QMAKE_EXTRA_TARGETS += xoviextension
PRE_TARGETDEPS += $$XOVI_DIR/xovi.c

# Define the Qt modules required
QT += quick qml

# Define the C++ standard version
CONFIG += c++17

# Specify the source files
SOURCES += \
    main.cpp entry.c $$XOVI_DIR/xovi.c \
    ClipboardInjector.cpp \
    rm_Line.cpp rm_SceneLineItem.cpp

HEADERS += ClipboardInjector.hpp rm_Line.hpp rm_SceneItem.hpp rm_SceneLineItem.hpp
INCLUDEPATH += $$XOVI_DIR

QMAKE_CXXFLAGS += -fPIC -Werror -Wno-invalid-offsetof -O3 -mfpu=neon -mfloat-abi=hard
QMAKE_CFLAGS += -fPIC -O3 -mfpu=neon -mfloat-abi=hard
