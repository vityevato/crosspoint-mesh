add_executable(MeshCoreRecentSendersTest
  MeshCoreRecentSendersTest.cpp
)

target_include_directories(MeshCoreRecentSendersTest PRIVATE
  ${REPO_ROOT}/lib/MeshCore/MeshCore
)

target_link_libraries(MeshCoreRecentSendersTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(MeshCoreRecentSendersTest)
