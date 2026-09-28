add_executable(MeshCoreGpsParseTest
  MeshCoreGpsParseTest.cpp
)

target_include_directories(MeshCoreGpsParseTest PRIVATE
  ${REPO_ROOT}/lib/MeshCore/MeshCore
)

target_link_libraries(MeshCoreGpsParseTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(MeshCoreGpsParseTest)
