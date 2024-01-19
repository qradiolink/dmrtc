
QT       += core gui network widgets


TARGET = dmrtc
TEMPLATE = app

CONFIG  += qt thread

#QMAKE_CXXFLAGS += -Werror
QMAKE_CXXFLAGS += $$(CXXFLAGS)
QMAKE_CFLAGS += $$(CFLAGS)
QMAKE_LFLAGS += $$(LDFLAGS)

message($$QMAKESPEC)

linux-g++ {
    message(Building for GNU/Linux)
}


SOURCES += src/main.cpp \
    src/rc4.cpp \
    src/channelviewmodel.cpp \
    src/dmridlookup.cpp \
    src/dmrrewrite.cpp \
    src/gatewayrouter.cpp \
    src/logicalchannel.cpp \
    src/settings.cpp \
    src/logger.cpp \
    src/signalling.cpp \
    src/udpclient.cpp \
    src/controller.cpp \
    src/mainwindow.cpp \
    src/utils.cpp
SOURCES += $$files(src/MMDVM/*.cpp)


HEADERS += src/settings.h \
    src/channelviewmodel.h \
    src/dmridlookup.h \
    src/dmrrewrite.h \
    src/gatewayrouter.h \
    src/logger.h \
    src/logicalchannel.h \
    src/signalling.h \
    src/standard_PDU.h \
    src/udpclient.h \
    src/controller.h \
    src/mainwindow.h \
    src/rc4.h \
    src/utils.h
HEADERS += $$files(src/MMDVM/*.h)

INCLUDEPATH += $$_PRO_FILE_PWD_/src/MMDVM/

!isEmpty(LIBDIR) {
    LIBS += -L$$LIBDIR
}
!isEmpty(INCDIR) {
    INCLUDEPATH += $$INCDIR
}

FORMS    += \
    src/mainwindow.ui


LIBS += -lrt -lpthread # need to include on some distros
LIBS += -lconfig++ -llog4cpp


RESOURCES += src/resources.qrc

!isEmpty(INSTALL_PREFIX) {
    target.path = $$INSTALL_PREFIX
    INSTALLS += target
}

