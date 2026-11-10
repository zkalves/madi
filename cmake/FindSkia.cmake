# FindSkia.cmake - Locate Google Skia 2D Graphics Engine
# Defines:
#   Skia_FOUND
#   Skia::Skia target

find_path(SKIA_INCLUDE_DIR
  NAMES include/core/SkCanvas.h
  PATHS
    /usr/include/skia
    /usr/local/include/skia
    $ENV{SKIA_DIR}/include
)

find_library(SKIA_LIBRARY
  NAMES skia
  PATHS
    /usr/lib
    /usr/local/lib
    $ENV{SKIA_DIR}/out/Release
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Skia
  DEFAULT_MSG
  SKIA_LIBRARY
  SKIA_INCLUDE_DIR
)

if(Skia_FOUND AND NOT TARGET Skia::Skia)
  add_library(Skia::Skia UNKNOWN IMPORTED)
  set_target_properties(Skia::Skia PROPERTIES
    IMPORTED_LOCATION "${SKIA_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${SKIA_INCLUDE_DIR}"
  )
endif()
