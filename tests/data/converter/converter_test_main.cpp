#include <gtest/gtest.h>

#include "nova/utils/log.h"

int main(int argc, char **argv) {
  nova::LogConfig log_config;
  log_config.set_file_sink_name("");
  log_config.set_console_sink_name("converter_test_console");
  nova::InitializeLogging(log_config);
  ::testing::InitGoogleTest(&argc, argv);
  const auto result = RUN_ALL_TESTS();
  nova::StopLogging();
  return result;
}
