#
# Copyright (C) 2023 Fredrik Lingvall

project(octave-jack-audio)

find_package (JACK)

# From http://www.coolprop.org/coolprop/wrappers/Octave/index.html
find_package (Octave)

if (OCTAVE_FOUND)

  # Octave oct flags.
  set (JACK_OCT_FLAGS "-DJACK_OCTAVE") # So that octave_idx_type is used for matrix/vector indexing.

  if (UNIX)
    set (OCT_LD_FLAGS "${CMAKE_LD_FLAGS} ${OCTAVE_LINK_FLAGS} -pthread") # We need phread to use pthread_setaffinity_np.
  else (UNIX)
    set (OCT_LD_FLAGS "${CMAKE_LD_FLAGS} ${OCTAVE_LINK_FLAGS}")
  endif (UNIX)

  #
  # Include paths
  #

  # Strip lead and trailing whitepasce
  string(STRIP "${OCTAVE_INCLUDE_DIRS}" OCTAVE_INCLUDE_DIRS)

  set (JACK_OCT_INCLUDE_DIRS
    "${PROJECT_BINARY_DIR};"
    "${PROJECT_SOURCE_DIR}/../include;"
    "${JACK_INCLUDE_DIR};"
    "${OCTAVE_INCLUDE_DIRS}"
    )
  message (STATUS "JACK_OCT_INCLUDE_DIRS: ${JACK_OCT_INCLUDE_DIRS}")

  #
  # jinfo
  #

  set (oct_jinfo_SOURCE_FILES
    oct_jinfo.cc
    )

  add_library (oct_jinfo MODULE
    ${oct_jinfo_SOURCE_FILES}
    )

  target_link_libraries (oct_jinfo
    ${OCTAVE_LIBRARIES}
    ${JACK_LIBRARIES}
    )

  set_target_properties (oct_jinfo PROPERTIES
    CXX_STANDARD 14
    COMPILE_FLAGS "${JACK_OCT_FLAGS}"
    INCLUDE_DIRECTORIES "${JACK_OCT_INCLUDE_DIRS}"
    LINK_FLAGS ${OCT_LD_FLAGS}
    SUFFIX ".oct" PREFIX "" OUTPUT_NAME "jinfo")

  #
  # jplay
  #

  set (oct_jplay_SOURCE_FILES
    oct_jplay.cc
    ../src/jaudio_play.cc
    )

  add_library (oct_jplay MODULE
    ${oct_jplay_SOURCE_FILES}
    )

  target_link_libraries (oct_jplay
    ${OCTAVE_LIBRARIES}
    ${JACK_LIBRARIES}
    )

  set_target_properties (oct_jplay PROPERTIES
    CXX_STANDARD 14
    COMPILE_FLAGS "${JACK_OCT_FLAGS}"
    INCLUDE_DIRECTORIES "${JACK_OCT_INCLUDE_DIRS}"
    LINK_FLAGS ${OCT_LD_FLAGS}
    SUFFIX ".oct" PREFIX "" OUTPUT_NAME "jplay")

  #
  # jrecord
  #

  set (oct_jrecord_SOURCE_FILES
    oct_jrecord.cc
    ../src/jaudio_record.cc
    )

  add_library (oct_jrecord MODULE
    ${oct_jrecord_SOURCE_FILES}
    )

  target_link_libraries (oct_jrecord
    ${OCTAVE_LIBRARIES}
    ${JACK_LIBRARIES}
    )

  set_target_properties (oct_jrecord PROPERTIES
    CXX_STANDARD 14
    COMPILE_FLAGS "${JACK_OCT_FLAGS}"
    INCLUDE_DIRECTORIES "${JACK_OCT_INCLUDE_DIRS}"
    LINK_FLAGS ${OCT_LD_FLAGS}
    SUFFIX ".oct" PREFIX "" OUTPUT_NAME "jrecord")

  #
  # jplayrec
  #

  set (oct_jplayrec_SOURCE_FILES
    oct_jplayrec.cc
    ../src/jaudio_playrec.cc
    )

  add_library (oct_jplayrec MODULE
    ${oct_jplayrec_SOURCE_FILES}
    )

  target_link_libraries (oct_jplayrec
    ${OCTAVE_LIBRARIES}
    ${JACK_LIBRARIES}
    )

  set_target_properties (oct_jplayrec PROPERTIES
    CXX_STANDARD 14
    COMPILE_FLAGS "${JACK_OCT_FLAGS}"
    INCLUDE_DIRECTORIES "${JACK_OCT_INCLUDE_DIRS}"
    LINK_FLAGS ${OCT_LD_FLAGS}
    SUFFIX ".oct" PREFIX "" OUTPUT_NAME "jplayrec")

endif (OCTAVE_FOUND)
