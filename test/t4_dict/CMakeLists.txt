add_executable(T4DictionaryTest
  T4DictionaryTest.cpp
)
target_include_directories(T4DictionaryTest PRIVATE
  ${REPO_ROOT}/lib/T4Dict
)
target_compile_definitions(T4DictionaryTest PRIVATE
  TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
  CMake_TEST_T4TRIEFORMAT_ONLY
)
target_link_libraries(T4DictionaryTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)
gtest_discover_tests(T4DictionaryTest)
