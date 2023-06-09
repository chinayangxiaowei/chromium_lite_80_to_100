// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ios/public/provider/chrome/browser/signin/chrome_identity_service.h"

#include "base/run_loop.h"
#include "base/test/metrics/histogram_tester.h"
#include "base/test/scoped_mock_clock_override.h"
#include "components/signin/internal/identity_manager/account_capabilities_constants.h"
#include "components/signin/public/base/signin_metrics.h"
#import "ios/public/provider/chrome/browser/signin/fake_chrome_identity.h"
#import "testing/gmock/include/gmock/gmock.h"
#import "testing/gtest/include/gtest/gtest.h"
#include "testing/gtest_mac.h"
#import "testing/platform_test.h"

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

using signin_metrics::FetchAccountCapabilitiesFromSystemLibraryResult;

namespace ios {
namespace {

// A wrapper for the API to fetch Capability results for a specific Capability
// type.
using CapabilityFetcherBlock = void (^)(ChromeIdentityCapabilityResult*);

class TestChromeIdentityService : public ChromeIdentityService {
 public:
  struct FetchCapabilitiesRequest {
    NSArray* capabilities;
    ChromeIdentity* identity;
    ChromeIdentityCapabilitiesFetchCompletionBlock completion;
  };

  TestChromeIdentityService() = default;
  ~TestChromeIdentityService() override = default;

  // Sets the capability and its corresponding fetcher in the test service.
  void SetCapabilityUnderTest(NSString* capability_name,
                              CapabilityFetcherBlock capability_fetcher_block) {
    capability_name_ = capability_name;
    capability_fetcher_block_ = capability_fetcher_block;
  }

  // Retrieves the capability tribool result for the capability under test.
  ChromeIdentityCapabilityResult FetchCapability(NSNumber* capability_value,
                                                 NSError* error) {
    base::HistogramTester histogramTester;
    base::ScopedMockClockOverride clock;
    ChromeIdentityCapabilityResult result;
    capability_fetcher_block_(&result);

    clock.Advance(base::Minutes(1));
    // Capability result is set after completion.
    RunFinishCapabilitiesCompletion(capability_value, error);

    histogramTester.ExpectUniqueTimeSample(
        "Signin.AccountCapabilities.GetFromSystemLibraryDuration",
        base::Minutes(1),
        /*expected_bucket_count=*/1);

    return result;
  }

 protected:
  void FetchCapabilities(
      NSArray* capabilities,
      ChromeIdentity* identity,
      ChromeIdentityCapabilitiesFetchCompletionBlock completion) override {
    EXPECT_FALSE(fetch_capabilities_request_.has_value());
    FetchCapabilitiesRequest request;
    request.capabilities = capabilities;
    request.identity = identity;
    request.completion = completion;
    fetch_capabilities_request_ = request;
  }

 private:
  void RunFinishCapabilitiesCompletion(NSNumber* capability_value,
                                       NSError* error) {
    NSDictionary* capabilities =
        capability_value ? @{capability_name_ : capability_value} : nil;
    EXPECT_TRUE(fetch_capabilities_request_.has_value());
    EXPECT_TRUE(fetch_capabilities_request_.value().completion);
    fetch_capabilities_request_.value().completion(capabilities, error);
    fetch_capabilities_request_.reset();
  }

  absl::optional<FetchCapabilitiesRequest> fetch_capabilities_request_;
  NSString* capability_name_ = nil;
  CapabilityFetcherBlock capability_fetcher_block_ = nil;
};

class ChromeIdentityServiceTest : public PlatformTest {
 public:
  ChromeIdentityServiceTest() {
    identity_ = [FakeChromeIdentity identityWithEmail:@"foo@bar.com"
                                               gaiaID:@"foo_bar_id"
                                                 name:@"Foo"];
  }
  ~ChromeIdentityServiceTest() override = default;

 protected:
  void RunCapabilitySmokeTests() {
    CheckChromeIdentityCapabilityResult();
    CheckMissingCapability();
    CheckCapabilityValueOutOfRange();
    CheckCapabillityFetcherWithError();
  }

  FakeChromeIdentity* identity_;
  TestChromeIdentityService service_;

 private:
  void CheckChromeIdentityCapabilityResult() {
    {
      base::HistogramTester histogramTester;
      EXPECT_EQ(ChromeIdentityCapabilityResult::kFalse,
                service_.FetchCapability(/*capability_value=*/@0, nil));
      histogramTester.ExpectUniqueSample(
          "Signin.AccountCapabilities.GetFromSystemLibraryResult",
          FetchAccountCapabilitiesFromSystemLibraryResult::kSuccess, 1);
    }
    {
      base::HistogramTester histogramTester;
      EXPECT_EQ(ChromeIdentityCapabilityResult::kTrue,
                service_.FetchCapability(/*capability_value=*/@1, nil));
      histogramTester.ExpectUniqueSample(
          "Signin.AccountCapabilities.GetFromSystemLibraryResult",
          FetchAccountCapabilitiesFromSystemLibraryResult::kSuccess, 1);
    }
    {
      base::HistogramTester histogramTester;
      EXPECT_EQ(ChromeIdentityCapabilityResult::kUnknown,
                service_.FetchCapability(/*capability_value=*/@2, nil));
      histogramTester.ExpectUniqueSample(
          "Signin.AccountCapabilities.GetFromSystemLibraryResult",
          FetchAccountCapabilitiesFromSystemLibraryResult::kSuccess, 1);
    }
  }

  void CheckMissingCapability() {
    base::HistogramTester histogramTester;
    EXPECT_EQ(ChromeIdentityCapabilityResult::kUnknown,
              service_.FetchCapability(/*capability_value=*/nil, nil));
    histogramTester.ExpectUniqueSample(
        "Signin.AccountCapabilities.GetFromSystemLibraryResult",
        FetchAccountCapabilitiesFromSystemLibraryResult::
            kErrorMissingCapability,
        1);
  }

  void CheckCapabilityValueOutOfRange() {
    base::HistogramTester histogramTester;
    EXPECT_EQ(ChromeIdentityCapabilityResult::kUnknown,
              service_.FetchCapability(/*capability_value=*/@100, nil));
    histogramTester.ExpectUniqueSample(
        "Signin.AccountCapabilities.GetFromSystemLibraryResult",
        FetchAccountCapabilitiesFromSystemLibraryResult::kErrorUnexpectedValue,
        1);
  }

  void CheckCapabillityFetcherWithError() {
    NSError* error = [NSError errorWithDomain:@"test" code:-100 userInfo:nil];
    {
      base::HistogramTester histogramTester;
      EXPECT_EQ(ChromeIdentityCapabilityResult::kUnknown,
                service_.FetchCapability(
                    /*capability_value=*/nil, error));
      histogramTester.ExpectUniqueSample(
          "Signin.AccountCapabilities.GetFromSystemLibraryResult",
          FetchAccountCapabilitiesFromSystemLibraryResult::kErrorGeneric, 1);
    }
    {
      base::HistogramTester histogramTester;
      EXPECT_EQ(ChromeIdentityCapabilityResult::kUnknown,
                service_.FetchCapability(/*capability_value=*/@0, error));
      histogramTester.ExpectUniqueSample(
          "Signin.AccountCapabilities.GetFromSystemLibraryResult",
          FetchAccountCapabilitiesFromSystemLibraryResult::kErrorGeneric, 1);
    }
    {
      base::HistogramTester histogramTester;
      EXPECT_EQ(ChromeIdentityCapabilityResult::kUnknown,
                service_.FetchCapability(
                    /*capability_value=*/@1, error));
      histogramTester.ExpectUniqueSample(
          "Signin.AccountCapabilities.GetFromSystemLibraryResult",
          FetchAccountCapabilitiesFromSystemLibraryResult::kErrorGeneric, 1);
    }
  }
};

TEST_F(ChromeIdentityServiceTest, CanOfferExtendedSyncPromos) {
  service_.SetCapabilityUnderTest(
      @(kCanOfferExtendedChromeSyncPromosCapabilityName),
      ^(ChromeIdentityCapabilityResult* fetched_capability_result) {
        service_.CanOfferExtendedSyncPromos(
            identity_, ^(ChromeIdentityCapabilityResult result) {
              *fetched_capability_result = result;
            });
      });

  RunCapabilitySmokeTests();
}

TEST_F(ChromeIdentityServiceTest, IsSubjectToParentalControls) {
  service_.SetCapabilityUnderTest(
      @(kIsSubjectToParentalControlsCapabilityName),
      ^(ChromeIdentityCapabilityResult* fetched_capability_result) {
        service_.IsSubjectToParentalControls(
            identity_, ^(ChromeIdentityCapabilityResult result) {
              *fetched_capability_result = result;
            });
      });

  RunCapabilitySmokeTests();
}

}  // namespace
}  // namespace ios
