# FindLibAvoid.cmake - Locate Adaptagrams libavoid connector router
# Defines:
#   LibAvoid_FOUND
#   LibAvoid::LibAvoid target

find_path(LIBAVOID_INCLUDE_DIR
  NAMES libavoid/router.h
  PATHS
    /usr/include
    /usr/local/include
    $ENV{ADAPTAGRAMS_DIR}/include
)

find_library(LIBAVOID_LIBRARY
  NAMES avoid
  PATHS
    /usr/lib
    /usr/local/lib
    $ENV{ADAPTAGRAMS_DIR}/lib
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(LibAvoid
  DEFAULT_MSG
  LIBAVOID_LIBRARY
  LIBAVOID_INCLUDE_DIR
)

if(LibAvoid_FOUND AND NOT TARGET LibAvoid::LibAvoid)
  add_library(LibAvoid::LibAvoid UNKNOWN IMPORTED)
  set_target_properties(LibAvoid::LibAvoid PROPERTIES
    IMPORTED_LOCATION "${LIBAVOID_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${LIBAVOID_INCLUDE_DIR}"
  )
endif()
