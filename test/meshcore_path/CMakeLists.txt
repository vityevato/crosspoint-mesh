add_executable(MeshCorePathTest
  MeshCorePathTest.cpp
)

target_include_directories(MeshCorePathTest PRIVATE
  ${REPO_ROOT}/lib/MeshCore/MeshCore
)

target_link_libraries(MeshCorePathTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(MeshCorePathTest)
