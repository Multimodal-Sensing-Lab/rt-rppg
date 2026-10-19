cmake_minimum_required(VERSION 3.16)
project(rt_rppg VERSION 1.0.0 LANGUAGES CXX)

# Real-time rPPG application (Qt GUI, OpenCV, dlib).
# The original project was built with qmake; this file reproduces its sources and settings.

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Qt: moc for MainWindow and uic for mainwindow.ui
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt5 5.9 REQUIRED COMPONENTS Core Gui Widgets)
find_package(Threads REQUIRED)

# cv::FaceDetectorYN (YuNet) needs OpenCV >= 4.5.4 with the dnn and objdetect modules.
find_package(OpenCV 4.5.4 REQUIRED COMPONENTS
  core imgproc imgcodecs videoio highgui objdetect dnn video features2d calib3d)

# dlib: CMake package if available, otherwise pkg-config (dlib-1.pc, e.g. Ubuntu's libdlib-dev).
find_package(dlib CONFIG QUIET)
if(TARGET dlib::dlib)
  set(RT_RPPG_DLIB dlib::dlib)
else()
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(DLIB REQUIRED IMPORTED_TARGET dlib-1)
  set(RT_RPPG_DLIB PkgConfig::DLIB)
endif()

set(RT_RPPG_SOURCES
  src/main.cpp
  src/mainwindow.cpp        src/mainwindow.h       src/mainwindow.ui
  src/cameras.cpp           src/cameras.h
  src/process.cpp           src/process.h
  src/healthsignals.cpp     src/healthsignals.h
  src/dataobjectclass.cpp   src/dataobjectclass.h
  src/uihealth.cpp          src/uihealth.h
  src/facelandmarks.cpp     src/facelandmarks.h
  src/normal.cpp            src/normal.h
  src/filters.cpp           src/filters.h
  src/mtfilterstabilizer.cpp src/mtfilterstabilizer.h
  src/network.cpp           src/network.h
  src/timinglog.h
  src/plot.cpp              src/plot.h             src/precomp.hpp
  src/easylogging++.cc      src/easylogging++.h
)

add_executable(rt_rppg ${RT_RPPG_SOURCES})
target_include_directories(rt_rppg PRIVATE src)
target_link_libraries(rt_rppg PRIVATE
  Qt5::Core Qt5::Gui Qt5::Widgets
  ${OpenCV_LIBS}
  ${RT_RPPG_DLIB}
  Threads::Threads)
target_compile_options(rt_rppg PRIVATE $<$<CONFIG:Release>:-O3>)

# Runtime files. The application opens models/, images/ and log.conf with relative paths,
# so they are placed next to the executable and the program is started from that folder.
set(RT_RPPG_LANDMARKS shape_predictor_68_face_landmarks_gtx.dat)
set(RT_RPPG_LANDMARKS_BZ2 ${CMAKE_SOURCE_DIR}/models/shape_predictor_68_face_landmarks_GTX.dat.bz2)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/models)
if(NOT EXISTS ${CMAKE_BINARY_DIR}/models/${RT_RPPG_LANDMARKS})
  find_program(BUNZIP2 bunzip2 REQUIRED)
  message(STATUS "Decompressing the facial landmark model")
  execute_process(
    COMMAND ${BUNZIP2} -kc ${RT_RPPG_LANDMARKS_BZ2}
    OUTPUT_FILE ${CMAKE_BINARY_DIR}/models/${RT_RPPG_LANDMARKS}
    RESULT_VARIABLE RT_RPPG_BUNZIP_RESULT)
  if(NOT RT_RPPG_BUNZIP_RESULT EQUAL 0)
    file(REMOVE ${CMAKE_BINARY_DIR}/models/${RT_RPPG_LANDMARKS})
    message(FATAL_ERROR "Could not decompress ${RT_RPPG_LANDMARKS_BZ2}")
  endif()
endif()
file(COPY ${CMAKE_SOURCE_DIR}/models/face_detection_yunet_2022mar_int8.onnx
     DESTINATION ${CMAKE_BINARY_DIR}/models)
file(COPY ${CMAKE_SOURCE_DIR}/images DESTINATION ${CMAKE_BINARY_DIR})
file(COPY ${CMAKE_SOURCE_DIR}/log.conf DESTINATION ${CMAKE_BINARY_DIR})
