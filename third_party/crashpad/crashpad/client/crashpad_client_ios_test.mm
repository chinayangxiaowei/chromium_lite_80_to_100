// Copyright 2020 The Crashpad Authors. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "client/crashpad_client.h"

#import <Foundation/Foundation.h>

#include <vector>

#include "base/strings/sys_string_conversions.h"
#include "client/crash_report_database.h"
#include "client/ios_handler/exception_processor.h"
#include "client/simulate_crash.h"
#include "gtest/gtest.h"
#include "test/scoped_temp_dir.h"
#include "testing/platform_test.h"

namespace crashpad {
namespace test {
namespace {

class CrashpadIOSClient : public PlatformTest {
 protected:
  // testing::Test:

  void SetUp() override {
    ASSERT_TRUE(client_.StartCrashpadInProcessHandler(
        base::FilePath(database_dir.path()), "", {}));
    database_ = CrashReportDatabase::Initialize(database_dir.path());
  }

  void TearDown() override { client_.ResetForTesting(); }

  auto& Client() { return client_; }
  auto& Database() { return database_; }

 private:
  std::unique_ptr<CrashReportDatabase> database_;
  CrashpadClient client_;
  ScopedTempDir database_dir;
};

TEST_F(CrashpadIOSClient, DumpWithoutCrash) {
  std::vector<CrashReportDatabase::Report> reports;
  EXPECT_EQ(Database()->GetPendingReports(&reports),
            CrashReportDatabase::kNoError);
  ASSERT_EQ(reports.size(), 0u);
  CRASHPAD_SIMULATE_CRASH();

  EXPECT_EQ(Database()->GetPendingReports(&reports),
            CrashReportDatabase::kNoError);
  ASSERT_EQ(reports.size(), 1u);
}

TEST_F(CrashpadIOSClient, DumpWithoutCrashAndDefer) {
  std::vector<CrashReportDatabase::Report> reports;
  CRASHPAD_SIMULATE_CRASH_AND_DEFER_PROCESSING();
  EXPECT_EQ(Database()->GetPendingReports(&reports),
            CrashReportDatabase::kNoError);
  ASSERT_EQ(reports.size(), 0u);
  Client().ProcessIntermediateDumps();
  EXPECT_EQ(Database()->GetPendingReports(&reports),
            CrashReportDatabase::kNoError);
  ASSERT_EQ(reports.size(), 1u);
}

TEST_F(CrashpadIOSClient, DumpWithoutCrashAndDeferAtPath) {
  std::vector<CrashReportDatabase::Report> reports;
  ScopedTempDir crash_dir;
  UUID uuid;
  uuid.InitializeWithNew();
  CRASHPAD_SIMULATE_CRASH_AND_DEFER_PROCESSING_AT_PATH(
      crash_dir.path().Append(uuid.ToString()));
  EXPECT_EQ(Database()->GetPendingReports(&reports),
            CrashReportDatabase::kNoError);
  ASSERT_EQ(reports.size(), 0u);

  NSError* error = nil;
  NSArray* paths = [[NSFileManager defaultManager]
      contentsOfDirectoryAtPath:base::SysUTF8ToNSString(
                                    crash_dir.path().value())
                          error:&error];
  ASSERT_EQ([paths count], 1u);
  Client().ProcessIntermediateDump(
      crash_dir.path().Append([paths[0] fileSystemRepresentation]));
  reports.clear();
  EXPECT_EQ(Database()->GetPendingReports(&reports),
            CrashReportDatabase::kNoError);
  ASSERT_EQ(reports.size(), 1u);
}

// This test is covered by a similar XCUITest, but for development purposes it's
// sometimes easier and faster to run in Google Test.  However, there's no way
// to correctly run this in Google Test. Leave the test here, disabled, for use
// during development only.
TEST_F(CrashpadIOSClient, DISABLED_ThrowNSException) {
  [NSException raise:@"GoogleTestNSException" format:@"ThrowException"];
}

// This test is covered by a similar XCUITest, but for development purposes it's
// sometimes easier and faster to run in Google Test.  However, there's no way
// to correctly run this in Google Test. Leave the test here, disabled, for use
// during development only.
TEST_F(CrashpadIOSClient, DISABLED_ThrowException) {
  std::vector<int> empty_vector;
  empty_vector.at(42);
}

}  // namespace
}  // namespace test
}  // namespace crashpad
