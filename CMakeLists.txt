#
# Copyright (C) 2023 Fredrik Lingvall
#

cmake_minimum_required(VERSION 3.14)

project (jack-audio)

# Add path to the various cmake files to detect MATLAB, Octave, JACK, etc.
set (CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/Modules/")

include(GetGitRevision)
git_version("JAUDIO")

message(STATUS "JAUDIO_VERSION_MAJOR: ${JAUDIO_VERSION_MAJOR}")
message(STATUS "JAUDIO_VERSION_MINOR: ${JAUDIO_VERSION_MINOR}")
message(STATUS "JAUDIO_VERSION_PATCH: ${JAUDIO_VERSION_PATCH}")
message(STATUS "JAUDIO_VERSION_COMMIT: ${JAUDIO_VERSION_COMMIT}")
message(STATUS "JAUDIO_VERSION_SHORT: ${JAUDIO_VERSION_SHORT}")
message(STATUS "JAUDIO_VERSION_LONG: ${JAUDIO_VERSION_LONG}")
message(STATUS "JAUDIO_VERSION_SHA1: ${JAUDIO_VERSION_SHA1}")
message(STATUS "JAUDIO_VERSION: ${JAUDIO_VERSION}")

# Version number.
#set (JAUDIO_VERSION_MAJOR 3)
#set (JAUDIO_VERSION_MINOR 0)
#set (JAUDIO_VERSION_PATCH 0)

message (STATUS "CMake system name: ${CMAKE_SYSTEM_NAME}")

option(BUILD_OCT "Enable building of the oct-files." ON)
option(BUILD_MEX "Enable building of the mex-files." OFF)
option(BUILD_PYTHON "Enable building of the Python bindings." OFF) # Work in progess
option(BUILD_JULIA "Enable building of the Julia bindings." OFF) # Work in progess
option(BUILD_USERMAN "Enable building of the user manual." OFF)

#
# Testing (TODO)
#

option(BUILD_TESTS "Determines whether to build tests." OFF)
if(BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif (BUILD_TESTS)

if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
  set(MACOSX TRUE)
endif()

if (UNIX AND NOT MACOSX)

  # Detect which Linux distro were running on
  if(EXISTS /etc/gentoo-release)
    set(LINUX_DISTRO "GENTOO" CACHE STRING "Unix OS (options: UBUNTU/ubuntu or GENTOO/gentoo).")
    execute_process (COMMAND bash -c "cat /etc/gentoo-release | awk \'{ print $5 }\' | tr -d \'\n\'"
      OUTPUT_VARIABLE LINUX_DISTRO_VERSION
      )
  elseif(EXISTS /usr/bin/lsb_release)
    set(LINUX_DISTRO "UBUNTU" CACHE STRING "Unix OS (options: UBUNTU/ubuntu or GENTOO/gentoo).")
    execute_process (COMMAND bash -c "lsb_release -r | awk \'{ print $2 }\' | tr -d \'\n\'"
      OUTPUT_VARIABLE LINUX_DISTRO_VERSION
      )
  else()
    set(LINUX_DISTRO "Unknown" CACHE STRING "Unix OS (options: UBUNTU/ubuntu or GENTOO/gentoo).")
    message(WARNING "Unknown/non-supported Linux distrubution!")
  endif()

  message(STATUS " ")
  message(STATUS "****************************")
  message(STATUS " ${LINUX_DISTRO} ${LINUX_DISTRO_VERSION} Linux detected!")
  message(STATUS "****************************")
  message(STATUS " ")

endif (UNIX AND NOT MACOSX)

#
# Octave
#

if (BUILD_OCT)
  include(octave.cmake)
endif (BUILD_OCT)

#
# Matlab (TODO)
#

if (BUILD_MEX)
  include(matlab.cmake)
endif (BUILD_MEX)

#
# Python (TODO)
#

if (BUILD_PYTHON)
  add_subdirectory(python)
endif (BUILD_PYTHON)

#
# Julia (TODO)
#

if (BUILD_JULIA)
  add_subdirectory(julia)
endif (BUILD_JULIA)

#
# User manual (N.B., we need CMake 3.x on Windows due to a MikTeX related bug).
#

if (BUILD_USERMAN)

  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/version.tex" "\\newcommand{\\version}{\\texttt{${JAUDIO_VERSION_SHA1}}\\xspace}\r")
  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/revision.tex" "\\rhead{\\date \\texttt{${JAUDIO_VERSION_SHA1}} }\r")

  include (userman/UseLATEX.cmake)

  add_latex_document(userman/jaudio_userman.tex
    BIBFILES userman/jaudio_userman.bib
    IMAGE_DIRS userman/eps
    DEFAULT_SAFEPDF # Using dvips/ps2pdf
    #DEFAULT_PS
    #DEFAULT_PDF # Messes up page breakes and fig sizes.
    )

  if(WIN32 AND NOT UNIX)
    install (FILES ${CMAKE_CURRENT_BINARY_DIR}/jaudio_userman.pdf DESTINATION documents)
  endif(WIN32 AND NOT UNIX)

endif (BUILD_USERMAN)
