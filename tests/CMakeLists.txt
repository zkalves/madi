# VECTOR Tests Configuration
add_executable(vector_smoke_test SmokeTest.cpp)

target_link_libraries(vector_smoke_test
  PRIVATE
    libvector
)

target_include_directories(vector_smoke_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

add_test(NAME SmokeTest COMMAND vector_smoke_test)
