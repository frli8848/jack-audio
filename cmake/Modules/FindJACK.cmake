# JACK_INCLUDE_DIR = jack.h
# JACK_LIBRARIES = libjack.a
# JACK_FOUND = true if JACK is found

if (MACOSX)
  set(CMAKE_PREFIX_PATH "/opt/miniconda3")
  set(CMAKE_PREFIX_PATH "${CMAKE_PREFIX_PATH};/usr/local/miniconda")
endif(MACOSX)

find_path (JACK_INCLUDE_DIR NAMES jack/jack.h PATHS include)
if (NOT JACK_INCLUDE_DIR)
  message (STATUS "Could not find jack.h")
endif (NOT JACK_INCLUDE_DIR)

find_library (JACK_LIBRARIES NAMES libjack jack PATHS lib)
if (NOT JACK_LIBRARIES)
  message (STATUS "Could not find the JACK library")
endif (NOT JACK_LIBRARIES)

if (JACK_INCLUDE_DIR AND JACK_LIBRARIES)
  set (JACK_FOUND TRUE)
endif (JACK_INCLUDE_DIR AND JACK_LIBRARIES)

if (JACK_FOUND)

  if (NOT JACK_FIND_QUIETLY)
    message (STATUS "Found jack.h: ${JACK_INCLUDE_DIR}")
  endif (NOT JACK_FIND_QUIETLY)

  if (NOT JACK_FIND_QUIETLY)
    message (STATUS "Found JACK: ${JACK_LIBRARIES}")
  endif (NOT JACK_FIND_QUIETLY)

else (JACK_FOUND)
  if (JACK_FIND_REQUIRED)
    message (FATAL_ERROR "Could not find JACK")
  endif (JACK_FIND_REQUIRED)
endif (JACK_FOUND)

mark_as_advanced (JACK_INCLUDE_DIR
  JACK_LIBRARIES
  JACK_FOUND)
