// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/chrome/browser/web/image_fetch/image_fetch_tab_helper.h"

#import <WebKit/WebKit.h>

#include "base/strings/sys_string_conversions.h"
#import "base/test/ios/wait_util.h"
#include "base/test/metrics/histogram_tester.h"
#include "ios/chrome/browser/browser_state/test_chrome_browser_state.h"
#import "ios/chrome/browser/web/chrome_web_test.h"
#include "ios/chrome/browser/web/image_fetch/image_fetch_java_script_feature.h"
#import "ios/web/public/test/fakes/fake_web_client.h"
#include "net/http/http_util.h"
#include "services/network/public/cpp/weak_wrapper_shared_url_loader_factory.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "services/network/test/test_url_loader_factory.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "testing/gtest_mac.h"

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

using base::test::ios::WaitUntilConditionOrTimeout;

namespace {
// Timeout for calling on ImageFetchTabHelper::GetImageData.
const NSTimeInterval kWaitForGetImageDataTimeout = 1.0;

const char kImageUrl[] = "http://www.chrooooooooooome.com/";
const char kImageData[] = "abc";
}

// Test fixture for ImageFetchTabHelper class.
class ImageFetchTabHelperTest : public ChromeWebTest {
 public:
  ImageFetchTabHelperTest(const ImageFetchTabHelperTest&) = delete;
  ImageFetchTabHelperTest& operator=(const ImageFetchTabHelperTest&) = delete;

 protected:
  ImageFetchTabHelperTest()
      : ChromeWebTest(std::make_unique<web::FakeWebClient>(),
                      web::WebTaskEnvironment::Options::IO_MAINLOOP) {}

  void SetUp() override {
    ChromeWebTest::SetUp();
    SetUpTestSharedURLLoaderFactory();
    GetWebClient()->SetJavaScriptFeatures(
        {ImageFetchJavaScriptFeature::GetInstance()});

    ASSERT_TRUE(LoadHtml("<html></html>"));
    ImageFetchTabHelper::CreateForWebState(web_state());
  }

  web::FakeWebClient* GetWebClient() override {
    return static_cast<web::FakeWebClient*>(ChromeWebTest::GetWebClient());
  }

  // Sets up the network::TestURLLoaderFactory to handle download request.
  void SetUpTestSharedURLLoaderFactory() {
    GetBrowserState()->SetSharedURLLoaderFactory(
        base::MakeRefCounted<network::WeakWrapperSharedURLLoaderFactory>(
            &test_url_loader_factory_));

    network::mojom::URLResponseHeadPtr head =
        network::mojom::URLResponseHead::New();
    std::string raw_header = "HTTP/1.1 200 OK\n"
                             "Content-type: image/png\n\n";
    head->headers = base::MakeRefCounted<net::HttpResponseHeaders>(
        net::HttpUtil::AssembleRawHeaders(raw_header));
    head->mime_type = "image/png";
    network::URLLoaderCompletionStatus status;
    status.decoded_body_length = strlen(kImageData);
    test_url_loader_factory_.AddResponse(GURL(kImageUrl), std::move(head),
                                         kImageData, status);
  }

  ImageFetchTabHelper* image_fetch_tab_helper() {
    return ImageFetchTabHelper::FromWebState(web_state());
  }

  // Returns the expected image data in NSData*.
  NSData* GetExpectedData() const {
    return [base::SysUTF8ToNSString(kImageData)
        dataUsingEncoding:NSUTF8StringEncoding];
  }

  network::TestURLLoaderFactory test_url_loader_factory_;
  base::HistogramTester histogram_tester_;
};

// Tests that ImageFetchTabHelper::GetImageData can get image data from Js.
TEST_F(ImageFetchTabHelperTest, GetImageDataWithJsSucceedFromCanvas) {
  // Inject fake |__gCrWeb.imageFetch.getImageData| that returns |kImageData|
  // in base64 format.
  id script_result = ExecuteJavaScriptForFeature(
      [NSString
          stringWithFormat:
              @"__gCrWeb.imageFetch = {}; __gCrWeb.imageFetch.getImageData = "
               "function(id, url) { "
               "__gCrWeb.common.sendWebKitMessage('ImageFetchMessageHandler', "
               "{'id': id, 'data': btoa('%s'), 'from':'canvas'}); }; true;",
              kImageData],
      ImageFetchJavaScriptFeature::GetInstance());
  ASSERT_NSEQ(@YES, script_result);

  __block bool callback_invoked = false;
  image_fetch_tab_helper()->GetImageData(GURL(kImageUrl), web::Referrer(),
                                         ^(NSData* data) {
                                           ASSERT_TRUE(data);
                                           EXPECT_NSEQ(GetExpectedData(), data);
                                           callback_invoked = true;
                                         });

  EXPECT_TRUE(WaitUntilConditionOrTimeout(kWaitForGetImageDataTimeout, ^{
    base::RunLoop().RunUntilIdle();
    return callback_invoked;
  }));
  histogram_tester_.ExpectUniqueSample(
      kUmaGetImageDataByJsResult,
      ContextMenuGetImageDataByJsResult::kCanvasSucceed, 1);
}

// Tests that ImageFetchTabHelper::GetImageData can get image data from Js.
TEST_F(ImageFetchTabHelperTest, GetImageDataWithJsSucceedFromXmlHttpRequest) {
  // Inject fake |__gCrWeb.imageFetch.getImageData| that returns |kImageData|
  // in base64 format.
  id script_result = ExecuteJavaScriptForFeature(
      [NSString
          stringWithFormat:
              @"__gCrWeb.imageFetch = {}; __gCrWeb.imageFetch.getImageData = "
               "function(id, url) { "
               "__gCrWeb.common.sendWebKitMessage('ImageFetchMessageHandler', "
               "{'id': id, 'data': btoa('%s'), 'from':'xhr'}); }; true;",
              kImageData],
      ImageFetchJavaScriptFeature::GetInstance());
  ASSERT_NSEQ(@YES, script_result);

  __block bool callback_invoked = false;
  image_fetch_tab_helper()->GetImageData(GURL(kImageUrl), web::Referrer(),
                                         ^(NSData* data) {
                                           ASSERT_TRUE(data);
                                           EXPECT_NSEQ(GetExpectedData(), data);
                                           callback_invoked = true;
                                         });

  EXPECT_TRUE(WaitUntilConditionOrTimeout(kWaitForGetImageDataTimeout, ^{
    base::RunLoop().RunUntilIdle();
    return callback_invoked;
  }));
  histogram_tester_.ExpectUniqueSample(
      kUmaGetImageDataByJsResult,
      ContextMenuGetImageDataByJsResult::kXMLHttpRequestSucceed, 1);
}

// Tests that ImageFetchTabHelper::GetImageData gets image data from server when
// Js fails.
TEST_F(ImageFetchTabHelperTest, GetImageDataWithJsFail) {
  id script_result = ExecuteJavaScriptForFeature(
      @"__gCrWeb.imageFetch = {}; __gCrWeb.imageFetch.getImageData = "
       "function(id, url) { "
       "__gCrWeb.common.sendWebKitMessage('ImageFetchMessageHandler', "
       "{'id': id}); }; true;",
      ImageFetchJavaScriptFeature::GetInstance());
  ASSERT_NSEQ(@YES, script_result);

  __block bool callback_invoked = false;
  image_fetch_tab_helper()->GetImageData(GURL(kImageUrl), web::Referrer(),
                                         ^(NSData* data) {
                                           ASSERT_TRUE(data);
                                           EXPECT_NSEQ(GetExpectedData(), data);
                                           callback_invoked = true;
                                         });

  EXPECT_TRUE(WaitUntilConditionOrTimeout(kWaitForGetImageDataTimeout, ^{
    base::RunLoop().RunUntilIdle();
    return callback_invoked;
  }));
  histogram_tester_.ExpectUniqueSample(
      kUmaGetImageDataByJsResult, ContextMenuGetImageDataByJsResult::kFail, 1);
}

// Tests that ImageFetchTabHelper::GetImageData gets image data from server when
// Js does not send a message back.
TEST_F(ImageFetchTabHelperTest, GetImageDataWithJsTimeout) {
  // Inject fake |__gCrWeb.imageFetch.getImageData| that does not do anything.
  id script_result = ExecuteJavaScriptForFeature(
      @"__gCrWeb.imageFetch = {}; __gCrWeb.imageFetch.getImageData = "
      @"function(id, url) {}; true;",
      ImageFetchJavaScriptFeature::GetInstance());
  ASSERT_NSEQ(@YES, script_result);

  __block bool callback_invoked = false;
  image_fetch_tab_helper()->GetImageData(GURL(kImageUrl), web::Referrer(),
                                         ^(NSData* data) {
                                           ASSERT_TRUE(data);
                                           EXPECT_NSEQ(GetExpectedData(), data);
                                           callback_invoked = true;
                                         });

  EXPECT_TRUE(WaitUntilConditionOrTimeout(kWaitForGetImageDataTimeout, ^{
    base::RunLoop().RunUntilIdle();
    return callback_invoked;
  }));
  histogram_tester_.ExpectUniqueSample(
      kUmaGetImageDataByJsResult, ContextMenuGetImageDataByJsResult::kTimeout,
      1);
}

// Tests that ImageFetchTabHelper::GetImageData gets image data from server when
// WebState is destroyed.
TEST_F(ImageFetchTabHelperTest, GetImageDataWithWebStateDestroy) {
  // Inject fake |__gCrWeb.imageFetch.getImageData| that does not do anything.
  id script_result = ExecuteJavaScriptForFeature(
      @"__gCrWeb.imageFetch = {}; __gCrWeb.imageFetch.getImageData = "
      @"function(id, url) {}; true;",
      ImageFetchJavaScriptFeature::GetInstance());
  ASSERT_NSEQ(@YES, script_result);

  __block bool callback_invoked = false;
  image_fetch_tab_helper()->GetImageData(GURL(kImageUrl), web::Referrer(),
                                         ^(NSData* data) {
                                           ASSERT_TRUE(data);
                                           EXPECT_NSEQ(GetExpectedData(), data);
                                           callback_invoked = true;
                                         });

  DestroyWebState();

  EXPECT_TRUE(WaitUntilConditionOrTimeout(kWaitForGetImageDataTimeout, ^{
    base::RunLoop().RunUntilIdle();
    return callback_invoked;
  }));
  histogram_tester_.ExpectTotalCount(kUmaGetImageDataByJsResult, 0);
}

// Tests that ImageFetchTabHelper::GetImageData gets image data from server when
// WebState navigates to a new web page.
TEST_F(ImageFetchTabHelperTest, GetImageDataWithWebStateNavigate) {
  // Inject fake |__gCrWeb.imageFetch.getImageData| that does not do anything.
  id script_result = ExecuteJavaScriptForFeature(
      @"__gCrWeb.imageFetch = {}; __gCrWeb.imageFetch.getImageData = "
      @"function(id, url) {}; true;",
      ImageFetchJavaScriptFeature::GetInstance());
  ASSERT_NSEQ(@YES, script_result);

  __block bool callback_invoked = false;
  image_fetch_tab_helper()->GetImageData(GURL(kImageUrl), web::Referrer(),
                                         ^(NSData* data) {
                                           ASSERT_TRUE(data);
                                           EXPECT_NSEQ(GetExpectedData(), data);
                                           callback_invoked = true;
                                         });

  LoadHtml(@"<html>new</html>"), GURL("http://new.webpage.com/");

  EXPECT_TRUE(WaitUntilConditionOrTimeout(kWaitForGetImageDataTimeout, ^{
    base::RunLoop().RunUntilIdle();
    return callback_invoked;
  }));
  histogram_tester_.ExpectTotalCount(kUmaGetImageDataByJsResult, 0);
}
