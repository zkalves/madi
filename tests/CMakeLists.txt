# Madí Tests Configuration
add_executable(madi_smoke_test SmokeTest.cpp)

target_link_libraries(madi_smoke_test
  PRIVATE
    madi-core
    madi-routing
    madi-io
)

target_include_directories(madi_smoke_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

add_test(NAME SmokeTest COMMAND madi_smoke_test)
