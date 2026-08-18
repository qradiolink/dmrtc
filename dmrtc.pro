
QT       += core gui network widgets


TARGET = dmrtc
TEMPLATE = app

CONFIG  += qt thread

QMAKE_CXXFLAGS += -Wall -Wextra -Wpedantic -std=c++17
QMAKE_CXXFLAGS += $$(CXXFLAGS)
QMAKE_CFLAGS += $$(CFLAGS)
QMAKE_LFLAGS += $$(LDFLAGS)

message($$QMAKESPEC)

linux-g++ {
    message(Building for GNU/Linux)
}


SOURCES += $$files(src/*.cpp)
SOURCES += $$files(src/MMDVM/*.cpp)


HEADERS += $$files(src/*.h)
HEADERS += $$files(src/MMDVM/*.h)

INCLUDEPATH += $$_PRO_FILE_PWD_/src/MMDVM/

!isEmpty(LIBDIR) {
    LIBS += -L$$LIBDIR
}
!isEmpty(INCDIR) {
    INCLUDEPATH += $$INCDIR
}

FORMS    += src/mainwindow.ui


LIBS += -lrt -lpthread # need to include on some distros
LIBS += -lconfig++ -llog4cpp -luuid


RESOURCES += src/resources.qrc

!isEmpty(INSTALL_PREFIX) {
    target.path = $$INSTALL_PREFIX
    INSTALLS += target
}

