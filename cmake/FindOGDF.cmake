# FindOGDF.cmake - Locate Open Graph Drawing Framework
# Defines:
#   OGDF_FOUND
#   OGDF::OGDF target

find_path(OGDF_INCLUDE_DIR
  NAMES ogdf/basic/Graph.h
  PATHS
    /usr/include
    /usr/local/include
    $ENV{OGDF_DIR}/include
)

find_library(OGDF_LIBRARY
  NAMES OGDF
  PATHS
    /usr/lib
    /usr/local/lib
    $ENV{OGDF_DIR}
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(OGDF
  DEFAULT_MSG
  OGDF_LIBRARY
  OGDF_INCLUDE_DIR
)

if(OGDF_FOUND AND NOT TARGET OGDF::OGDF)
  add_library(OGDF::OGDF UNKNOWN IMPORTED)
  set_target_properties(OGDF::OGDF PROPERTIES
    IMPORTED_LOCATION "${OGDF_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${OGDF_INCLUDE_DIR}"
  )
endif()
