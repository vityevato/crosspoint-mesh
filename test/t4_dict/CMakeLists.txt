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

# ── T4Layout tests ──────────────────────────────────────────────────────
add_executable(T4LayoutTest
  T4LayoutTest.cpp
  ${REPO_ROOT}/lib/T4Dict/T4Layout.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../stubs/HardwareSerial.cpp
)
target_include_directories(T4LayoutTest PRIVATE
  ${REPO_ROOT}/lib/T4Dict
  ${REPO_ROOT}/lib/Logging
)
target_link_libraries(T4LayoutTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)
gtest_discover_tests(T4LayoutTest)

# ── T4InputEngine tests (uses MockDictionary — no HalStorage needed) ─────
add_executable(T4InputEngineTest
  T4InputEngineTest.cpp
  ${REPO_ROOT}/lib/T4Dict/T4Layout.cpp
  ${REPO_ROOT}/lib/T4Dict/T4UserLexicon.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../stubs/HardwareSerial.cpp
)
target_include_directories(T4InputEngineTest PRIVATE
  ${REPO_ROOT}/lib/T4Dict
  ${REPO_ROOT}/lib
  ${REPO_ROOT}/lib/Logging
)
target_link_libraries(T4InputEngineTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)
gtest_discover_tests(T4InputEngineTest)

# ── T4UserLexicon tests (pure — no HalStorage, no dictionary) ────────────
add_executable(T4UserLexiconTest
  T4UserLexiconTest.cpp
  ${REPO_ROOT}/lib/T4Dict/T4Layout.cpp
  ${REPO_ROOT}/lib/T4Dict/T4UserLexicon.cpp
)
target_include_directories(T4UserLexiconTest PRIVATE
  ${REPO_ROOT}/lib/T4Dict
  ${REPO_ROOT}/lib
)
target_link_libraries(T4UserLexiconTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)
gtest_discover_tests(T4UserLexiconTest)
