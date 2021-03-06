/* 
 * Copyright 2017 Hewlett Packard Enterprise Development LP
 * 
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions 
 * are met:
 * 
 *   1. Redistributions of source code must retain the above copyright 
 *      notice, this list of conditions and the following disclaimer.
 *
 *   2. Redistributions in binary form must reproduce the above copyright 
 *      notice, this list of conditions and the following disclaimer 
 *      in the documentation and/or other materials provided with the 
 *      distribution.
 *   
 *   3. Neither the name of the copyright holder nor the names of its 
 *      contributors may be used to endorse or promote products derived 
 *      from this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS 
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT 
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR 
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT 
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, 
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED 
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR 
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF 
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING 
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS 
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef NVWAL_TEST_COMMON_HPP_
#define NVWAL_TEST_COMMON_HPP_

#include <string>
#include <memory>
#include <thread>
#include <vector>

#include "nvwal_fwd.h"
#include "nvwal_types.h"

namespace nvwaltest {

struct WalResource {
  WalResource() : flusher_exit_code_(0), fsyncer_exit_code_(0) {}

  NvwalContext wal_instance_;

  std::thread flusher_;
  std::thread fsyncer_;

  nvwal_error_t flusher_exit_code_;
  nvwal_error_t fsyncer_exit_code_;

  void launch_flusher();
  void launch_fsyncer();

  void join_flusher();
  void join_fsyncer();

  std::vector< std::unique_ptr< nvwal_byte_t[] > > writer_buffers_;
};

/**
 * Each unit test holds one TestContext throughout the test execution.
 * See test_nvwal_example.cpp for how to use this class.
 */
class TestContext {
 public:
  enum InstanceSize {
    /**
     * Use this for most testcases to reduce resource consumption.
     * \li writer's buffer size : 4 KB
     * \li writers per WAL : 2
     * \li block_seg_size_, nv_seg_size_ : 4 KB
     * \li nv_quota_ : 64 KB
     */
    kTiny = 0,

    /**
     * Use this for testcases that intentionally trigger blocking,
     * buffer wrap-around, etc.
     * \li writer's buffer size : 512b
     * \li writers per WAL : 2
     * \li block_seg_size_, nv_seg_size_ : 512b
     * \li nv_quota_ : 2 KB
     */
    kExtremelyTiny = 1,

    /**In some cases we might need this.. */
    kBig,
  };

  /** This does not invoke complex initialization. Call init_all() next. */
  TestContext(int wal_count, InstanceSize sizing)
    : wal_count_(wal_count), sizing_(sizing) {
  }
  TestContext(int wal_count)
    : wal_count_(wal_count), sizing_(kTiny) {
  }
  ~TestContext() {
    uninit_all();
  }

  /**
   * Most initialization happens here. Don't forget to check the return code!
   */
  nvwal_error_t init_all();

  /**
   * This is idempotent and the destructor automatically calls it.
   * Still, you should call this so that you can sanity-check the return value.
   */
  nvwal_error_t uninit_all() {
    return impl_shutdown(true);
  }

  /**
   * Restart testcase calls this to stop the instance and restart with
   * durable logs.
   */
  nvwal_error_t restart_clean();

  /**
   * A convenience function to wait until the given epoch becomes durable.
   * Of course you must invoke nvwal_advance_stable_epoch() beforehand!!
   * This method doesn't care about performance.
   * It sleeps with some interval \a sleep_duration_ms to minimize wasted CPU.
   */
  static nvwal_error_t wait_until_durable(
    NvwalContext* wal,
    nvwal_epoch_t expected_durable_epoch,
    uint64_t sleep_duration_ms = 5);

  int get_wal_count() const { return wal_count_; }
  WalResource* get_resource(int wal_id) { return &wal_resources_[wal_id]; }
  NvwalContext* get_wal(int wal_id) { return &wal_resources_[wal_id].wal_instance_; }

 private:
  TestContext(const TestContext&) = delete;
  TestContext& operator=(const TestContext&) = delete;

  nvwal_error_t impl_startup(bool create_files);
  nvwal_error_t impl_shutdown(bool remove_files);

  /**
   * Returns one randomly generated name in "%%%%_%%%%_%%%%_%%%%" format.
   * It should be used as the root path so that all file paths are unique random.
   * This makes it possible to run an arbitrary number of tests in parallel.
   */
  static std::string get_random_name();

  const int wal_count_;
  const InstanceSize sizing_;
  std::string unique_root_path_;
  std::vector< WalResource > wal_resources_;
};

/**
  * Register signal handlers to capture signals during testcase execution.
  */
void register_signal_handlers(
  const char* test_case_name,
  int argc,
  char** argv);

/**
  * As the name suggests, we write out an gtest's result xml file with error state so that
  * jenkins will get aware of some error if the process disappears without any trace,
  * for example ctest killed it (via SIGSTOP, which can't be captured) for timeout.
  */
void            pre_populate_error_result_xml();

}  // namespace nvwaltest

#define TEST_QUOTE(str) #str
#define TEST_EXPAND_AND_QUOTE(str) TEST_QUOTE(str)

/**
 * Put this macro to define a main() that registers signal handlers.
 * This is required to convert assertion failures (crashes) to failed tests and provide more
 * detailed information in google-test's result xml file.
 * This really should be a built-in feature in gtest...
 *
 * But, I'm not sure if I should blame ctest, jenkins, or gtest (or all of them).
 * Related URLs:
 *   https://groups.google.com/forum/#!topic/googletestframework/NK5cAEqsioY
 *   https://code.google.com/p/googletest/issues/detail?id=342
 *   https://code.google.com/p/googletest/issues/detail?id=311
 */
#define TEST_MAIN_CAPTURE_SIGNALS(test_case_name) \
  int main(int argc, char **argv) { \
    nvwaltest::register_signal_handlers( \
      TEST_EXPAND_AND_QUOTE(test_case_name), \
      argc, \
      argv); \
    nvwaltest::pre_populate_error_result_xml(); \
    ::testing::InitGoogleTest(&argc, argv); \
    return RUN_ALL_TESTS(); \
  }

#endif  // NVWAL_TEST_COMMON_HPP_
