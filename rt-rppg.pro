# qmake project of the real-time rPPG application: an alternative to CMake for systems without CMake 3.16, such as the
# laptop of the paper, where the original application was built with qmake. Same compiler flags as the original project.
#
# OpenCV and dlib are taken from the folders where they were built (OPENCV_DIR, DLIB_DIR) or, if these are not given,
# from pkg-config:
#   qmake OPENCV_DIR=$HOME/Projects/External/opencv/opencv_4_5_5_basic DLIB_DIR=$HOME/Projects/External/dlib/dlib_19_23
#   make -j4
# The executable rt_rppg is written next to this file, where models/, images/ and log.conf are found. Decompress the
# landmark model once:
#   bunzip2 -kc models/shape_predictor_68_face_landmarks_GTX.dat.bz2 > models/shape_predictor_68_face_landmarks_gtx.dat

QT += core gui widgets
CONFIG += c++14
TARGET = rt_rppg
TEMPLATE = app
DESTDIR = $$PWD

QMAKE_CXXFLAGS += -O3 -fopenmp
LIBS += -fopenmp

isEmpty(OPENCV_DIR) {
    CONFIG += link_pkgconfig
    PKGCONFIG += opencv4
} else {
    INCLUDEPATH += $$OPENCV_DIR/include/opencv4
    LIBS += -L$$OPENCV_DIR/lib
    LIBS += -lopencv_objdetect -lopencv_features2d -lopencv_imgproc -lopencv_highgui -lopencv_imgcodecs -lopencv_core -lopencv_videoio
    QMAKE_RPATHDIR += $$OPENCV_DIR/lib
}

isEmpty(DLIB_DIR) {
    CONFIG += link_pkgconfig
    PKGCONFIG += dlib-1
} else {
    INCLUDEPATH += $$DLIB_DIR/include
    LIBS += -L$$DLIB_DIR/lib -ldlib
    QMAKE_RPATHDIR += $$DLIB_DIR/lib
}

LIBS += -llapack -lblas -lpthread

INCLUDEPATH += src

SOURCES += src/main.cpp \
    src/mainwindow.cpp \
    src/cameras.cpp \
    src/process.cpp \
    src/healthsignals.cpp \
    src/dataobjectclass.cpp \
    src/plot.cpp \
    src/uihealth.cpp \
    src/facelandmarks.cpp \
    src/normal.cpp \
    src/filters.cpp \
    src/mtfilterstabilizer.cpp \
    src/network.cpp \
    src/easylogging++.cc

HEADERS += src/mainwindow.h \
    src/cameras.h \
    src/process.h \
    src/healthsignals.h \
    src/dataobjectclass.h \
    src/uihealth.h \
    src/facelandmarks.h \
    src/normal.h \
    src/plot.h \
    src/filters.h \
    src/mtfilterstabilizer.h \
    src/network.h \
    src/timinglog.h \
    src/precomp.hpp \
    src/easylogging++.h

FORMS += src/mainwindow.ui
