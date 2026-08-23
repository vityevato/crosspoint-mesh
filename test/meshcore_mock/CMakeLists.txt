add_executable(MockChannelInjectTest
  MockChannelInjectTest.cpp
)

target_include_directories(MockChannelInjectTest PRIVATE
  ${REPO_ROOT}/src/simulator
)

target_link_libraries(MockChannelInjectTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(MockChannelInjectTest)
