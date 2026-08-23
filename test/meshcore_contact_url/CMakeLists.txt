add_executable(MeshCoreContactUrlParserTest
  MeshCoreContactUrlParserTest.cpp
)

target_include_directories(MeshCoreContactUrlParserTest PRIVATE
  ${REPO_ROOT}/lib/MeshCore
  ${REPO_ROOT}/src/activities/meshcore/utils
)

target_link_libraries(MeshCoreContactUrlParserTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(MeshCoreContactUrlParserTest)
