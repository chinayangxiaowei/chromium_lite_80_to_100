// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/format_macros.h"

#include <string>

#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "build/build_config.h"
#include "chrome/browser/autocomplete/chrome_autocomplete_scheme_classifier.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/extensions/extension_apitest.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/location_bar/location_bar.h"
#include "chrome/browser/ui/view_ids.h"
#include "chrome/test/base/interactive_test_utils.h"
#include "chrome/test/base/search_test_utils.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/omnibox/browser/autocomplete_controller.h"
#include "components/omnibox/browser/autocomplete_input.h"
#include "components/omnibox/browser/autocomplete_match.h"
#include "components/omnibox/browser/autocomplete_result.h"
#include "components/omnibox/browser/omnibox_controller_emitter.h"
#include "components/omnibox/browser/omnibox_edit_model.h"
#include "components/omnibox/browser/omnibox_view.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/test_utils.h"
#include "extensions/test/extension_test_message_listener.h"
#include "extensions/test/result_catcher.h"
#include "extensions/test/test_extension_dir.h"
#include "third_party/metrics_proto/omnibox_event.pb.h"
#include "ui/base/window_open_disposition.h"

namespace {

using base::ASCIIToUTF16;
using extensions::ResultCatcher;
using metrics::OmniboxEventProto;
using ui_test_utils::WaitForAutocompleteDone;

void InputKeys(Browser* browser, const std::vector<ui::KeyboardCode>& keys) {
  for (auto key : keys) {
    // Note that sending key presses can be flaky at times.
    ASSERT_TRUE(ui_test_utils::SendKeyPressSync(browser, key, false, false,
                                                false, false));
  }
}

LocationBar* GetLocationBar(Browser* browser) {
  return browser->window()->GetLocationBar();
}

AutocompleteController* GetAutocompleteController(Browser* browser) {
  return GetLocationBar(browser)
      ->GetOmniboxView()
      ->model()
      ->autocomplete_controller();
}

std::u16string AutocompleteResultAsString(const AutocompleteResult& result) {
  std::string output(base::StringPrintf("{%" PRIuS "} ", result.size()));
  for (size_t i = 0; i < result.size(); ++i) {
    AutocompleteMatch match = result.match_at(i);
    std::string provider_name = match.provider->GetName();
    output.append(base::StringPrintf("[\"%s\" by \"%s\"] ",
                                     base::UTF16ToUTF8(match.contents).c_str(),
                                     provider_name.c_str()));
  }
  return base::UTF8ToUTF16(output);
}

struct ExpectedMatchComponent {
  std::u16string text;
  ACMatchClassification::Style style;
};
using ExpectedMatchComponents = std::vector<ExpectedMatchComponent>;

// A helper method to verify the expected styled components of an autocomplete
// match.
// TODO(devlin): Update other tests to use this handy check.
void VerifyMatchComponents(const ExpectedMatchComponents& expected,
                           const AutocompleteMatch& match) {
  std::u16string expected_string;
  for (const auto& component : expected)
    expected_string += component.text;

  EXPECT_EQ(expected_string, match.contents);

  // Check if we have the right number of components. If we don't, safely bail
  // so that we don't access out-of-bounds elements.
  if (expected.size() != match.contents_class.size()) {
    ADD_FAILURE() << "Improper number of components: " << expected.size()
                  << " vs " << match.contents_class.size();
    return;
  }

  // Iterate over the string and match each component.
  size_t curr_offset = 0;
  for (size_t i = 0; i < expected.size(); ++i) {
    SCOPED_TRACE(expected[i].text);

    EXPECT_EQ(curr_offset, match.contents_class[i].offset);
    EXPECT_EQ(expected[i].style, match.contents_class[i].style);
    curr_offset += expected[i].text.size();
  }
}

using OmniboxApiTest = extensions::ExtensionApiTest;

}  // namespace

// http://crbug.com/167158
IN_PROC_BROWSER_TEST_F(OmniboxApiTest, DISABLED_Basic) {
  ASSERT_TRUE(RunExtensionTest("omnibox")) << message_;

  // The results depend on the TemplateURLService being loaded. Make sure it is
  // loaded so that the autocomplete results are consistent.
  Profile* profile = browser()->profile();
  search_test_utils::WaitForTemplateURLServiceToLoad(
      TemplateURLServiceFactory::GetForProfile(profile));

  AutocompleteController* autocomplete_controller =
      GetAutocompleteController(browser());

  // Test that our extension's keyword is suggested to us when we partially type
  // it.
  {
    AutocompleteInput input(u"keywor", metrics::OmniboxEventProto::NTP,
                            ChromeAutocompleteSchemeClassifier(profile));
    autocomplete_controller->Start(input);
    WaitForAutocompleteDone(browser());
    EXPECT_TRUE(autocomplete_controller->done());

    // Now, peek into the controller to see if it has the results we expect.
    // First result should be to search for what was typed, second should be to
    // enter "extension keyword" mode.
    const AutocompleteResult& result = autocomplete_controller->result();
    ASSERT_EQ(2U, result.size()) << AutocompleteResultAsString(result);
    AutocompleteMatch match = result.match_at(0);
    EXPECT_EQ(AutocompleteMatchType::SEARCH_WHAT_YOU_TYPED, match.type);
    EXPECT_FALSE(match.deletable);

    match = result.match_at(1);
    EXPECT_EQ(u"kw", match.keyword);
  }

  // Test that our extension can send suggestions back to us.
  {
    AutocompleteInput input(u"kw suggestio", metrics::OmniboxEventProto::NTP,
                            ChromeAutocompleteSchemeClassifier(profile));
    autocomplete_controller->Start(input);
    WaitForAutocompleteDone(browser());
    EXPECT_TRUE(autocomplete_controller->done());

    // Now, peek into the controller to see if it has the results we expect.
    // First result should be to invoke the keyword with what we typed, 2-4
    // should be to invoke with suggestions from the extension, and the last
    // should be to search for what we typed.
    const AutocompleteResult& result = autocomplete_controller->result();
    ASSERT_EQ(5U, result.size()) << AutocompleteResultAsString(result);

    EXPECT_EQ(u"kw", result.match_at(0).keyword);
    EXPECT_EQ(u"kw suggestio", result.match_at(0).fill_into_edit);
    EXPECT_EQ(AutocompleteMatchType::SEARCH_OTHER_ENGINE,
              result.match_at(0).type);
    EXPECT_EQ(AutocompleteProvider::TYPE_KEYWORD,
              result.match_at(0).provider->type());
    EXPECT_EQ(u"kw", result.match_at(1).keyword);
    EXPECT_EQ(u"kw suggestion1", result.match_at(1).fill_into_edit);
    EXPECT_EQ(AutocompleteProvider::TYPE_KEYWORD,
              result.match_at(1).provider->type());
    EXPECT_EQ(u"kw", result.match_at(2).keyword);
    EXPECT_EQ(u"kw suggestion2", result.match_at(2).fill_into_edit);
    EXPECT_EQ(AutocompleteProvider::TYPE_KEYWORD,
              result.match_at(2).provider->type());
    EXPECT_EQ(u"kw", result.match_at(3).keyword);
    EXPECT_EQ(u"kw suggestion3", result.match_at(3).fill_into_edit);
    EXPECT_EQ(AutocompleteProvider::TYPE_KEYWORD,
              result.match_at(3).provider->type());

    std::u16string description =
        u"Description with style: <match>, [dim], (url till end)";
    EXPECT_EQ(description, result.match_at(1).contents);
    ASSERT_EQ(6u, result.match_at(1).contents_class.size());

    EXPECT_EQ(0u, result.match_at(1).contents_class[0].offset);
    EXPECT_EQ(ACMatchClassification::NONE,
              result.match_at(1).contents_class[0].style);

    EXPECT_EQ(description.find('<'),
              result.match_at(1).contents_class[1].offset);
    EXPECT_EQ(ACMatchClassification::MATCH,
              result.match_at(1).contents_class[1].style);

    EXPECT_EQ(description.find('>') + 1u,
              result.match_at(1).contents_class[2].offset);
    EXPECT_EQ(ACMatchClassification::NONE,
              result.match_at(1).contents_class[2].style);

    EXPECT_EQ(description.find('['),
              result.match_at(1).contents_class[3].offset);
    EXPECT_EQ(ACMatchClassification::DIM,
              result.match_at(1).contents_class[3].style);

    EXPECT_EQ(description.find(']') + 1u,
              result.match_at(1).contents_class[4].offset);
    EXPECT_EQ(ACMatchClassification::NONE,
              result.match_at(1).contents_class[4].style);

    EXPECT_EQ(description.find('('),
              result.match_at(1).contents_class[5].offset);
    EXPECT_EQ(ACMatchClassification::URL,
              result.match_at(1).contents_class[5].style);

    AutocompleteMatch match = result.match_at(4);
    EXPECT_EQ(AutocompleteMatchType::SEARCH_WHAT_YOU_TYPED, match.type);
    EXPECT_EQ(AutocompleteProvider::TYPE_SEARCH,
              result.match_at(4).provider->type());
    EXPECT_FALSE(match.deletable);
  }

  // Flaky, see http://crbug.com/167158
  /*
  {
    LocationBar* location_bar = GetLocationBar(browser());
    ResultCatcher catcher;
    OmniboxView* omnibox_view = location_bar->GetOmniboxView();
    omnibox_view->OnBeforePossibleChange();
    omnibox_view->SetUserText(u"kw command");
    omnibox_view->OnAfterPossibleChange(true);
    location_bar->AcceptInput();
    // This checks that the keyword provider (via javascript)
    // gets told to navigate to the string "command".
    EXPECT_TRUE(catcher.GetNextResult()) << catcher.message();
  }
  */
}

IN_PROC_BROWSER_TEST_F(OmniboxApiTest, OnInputEntered) {
  ASSERT_TRUE(RunExtensionTest("omnibox")) << message_;
  Profile* profile = browser()->profile();
  search_test_utils::WaitForTemplateURLServiceToLoad(
      TemplateURLServiceFactory::GetForProfile(profile));

  LocationBar* location_bar = GetLocationBar(browser());
  OmniboxView* omnibox_view = location_bar->GetOmniboxView();
  ResultCatcher catcher;
  AutocompleteController* autocomplete_controller =
      GetAutocompleteController(browser());
  omnibox_view->OnBeforePossibleChange();
  omnibox_view->SetUserText(u"kw command");
  omnibox_view->OnAfterPossibleChange(true);

  {
    AutocompleteInput input(u"kw command", metrics::OmniboxEventProto::NTP,
                            ChromeAutocompleteSchemeClassifier(profile));
    autocomplete_controller->Start(input);
  }
  omnibox_view->model()->AcceptInput(WindowOpenDisposition::CURRENT_TAB);
  WaitForAutocompleteDone(browser());
  EXPECT_TRUE(autocomplete_controller->done());
  EXPECT_TRUE(catcher.GetNextResult()) << catcher.message();

  omnibox_view->OnBeforePossibleChange();
  omnibox_view->SetUserText(u"kw newtab");
  omnibox_view->OnAfterPossibleChange(true);
  WaitForAutocompleteDone(browser());
  EXPECT_TRUE(autocomplete_controller->done());

  {
    AutocompleteInput input(u"kw newtab", metrics::OmniboxEventProto::NTP,
                            ChromeAutocompleteSchemeClassifier(profile));
    autocomplete_controller->Start(input);
  }
  omnibox_view->model()->AcceptInput(WindowOpenDisposition::NEW_FOREGROUND_TAB);
  WaitForAutocompleteDone(browser());
  EXPECT_TRUE(autocomplete_controller->done());
  EXPECT_TRUE(catcher.GetNextResult()) << catcher.message();
}

// Tests that we get suggestions from and send input to the incognito context
// of an incognito split mode extension.
// http://crbug.com/100927
// Test is flaky: http://crbug.com/101219
IN_PROC_BROWSER_TEST_F(OmniboxApiTest, DISABLED_IncognitoSplitMode) {
  Profile* profile = browser()->profile();
  ResultCatcher catcher_incognito;
  catcher_incognito.RestrictToBrowserContext(
      profile->GetPrimaryOTRProfile(/*create_if_needed=*/true));

  ASSERT_TRUE(RunExtensionTest("omnibox", {}, {.allow_in_incognito = true}))
      << message_;

  // Open an incognito window and wait for the incognito extension process to
  // respond.
  Browser* incognito_browser = CreateIncognitoBrowser();
  ASSERT_TRUE(catcher_incognito.GetNextResult()) << catcher_incognito.message();

  // The results depend on the TemplateURLService being loaded. Make sure it is
  // loaded so that the autocomplete results are consistent.
  search_test_utils::WaitForTemplateURLServiceToLoad(
      TemplateURLServiceFactory::GetForProfile(browser()->profile()));

  LocationBar* location_bar = GetLocationBar(incognito_browser);
  AutocompleteController* autocomplete_controller =
      GetAutocompleteController(incognito_browser);

  // Test that we get the incognito-specific suggestions.
  {
    AutocompleteInput input(u"kw suggestio", metrics::OmniboxEventProto::NTP,
                            ChromeAutocompleteSchemeClassifier(profile));
    autocomplete_controller->Start(input);
    WaitForAutocompleteDone(browser());
    EXPECT_TRUE(autocomplete_controller->done());

    // First result should be to invoke the keyword with what we typed, 2-4
    // should be to invoke with suggestions from the extension, and the last
    // should be to search for what we typed.
    const AutocompleteResult& result = autocomplete_controller->result();
    ASSERT_EQ(5U, result.size()) << AutocompleteResultAsString(result);
    ASSERT_FALSE(result.match_at(0).keyword.empty());
    EXPECT_EQ(u"kw suggestion3 incognito", result.match_at(3).fill_into_edit);
  }

  // Test that our input is sent to the incognito context. The test will do a
  // text comparison and succeed only if "command incognito" is sent to the
  // incognito context.
  {
    ResultCatcher catcher;
    AutocompleteInput input(u"kw command incognito",
                            metrics::OmniboxEventProto::NTP,
                            ChromeAutocompleteSchemeClassifier(profile));
    autocomplete_controller->Start(input);
    location_bar->AcceptInput();
    EXPECT_TRUE(catcher.GetNextResult()) << catcher.message();
  }
}

// The test is flaky on Win10. crbug.com/1045731.
#if BUILDFLAG(IS_WIN)
#define MAYBE_PopupStaysClosed DISABLED_PopupStaysClosed
#else
#define MAYBE_PopupStaysClosed PopupStaysClosed
#endif
// Tests that the autocomplete popup doesn't reopen after accepting input for
// a given query.
// http://crbug.com/88552
IN_PROC_BROWSER_TEST_F(OmniboxApiTest, MAYBE_PopupStaysClosed) {
  ASSERT_TRUE(RunExtensionTest("omnibox")) << message_;

  // The results depend on the TemplateURLService being loaded. Make sure it is
  // loaded so that the autocomplete results are consistent.
  Profile* profile = browser()->profile();
  search_test_utils::WaitForTemplateURLServiceToLoad(
      TemplateURLServiceFactory::GetForProfile(profile));

  LocationBar* location_bar = GetLocationBar(browser());
  OmniboxView* omnibox_view = location_bar->GetOmniboxView();
  AutocompleteController* autocomplete_controller =
      GetAutocompleteController(browser());

  // Input a keyword query and wait for suggestions from the extension.
  omnibox_view->OnBeforePossibleChange();
  omnibox_view->SetUserText(u"kw comman");
  omnibox_view->OnAfterPossibleChange(true);
  WaitForAutocompleteDone(browser());
  EXPECT_TRUE(autocomplete_controller->done());
  EXPECT_TRUE(omnibox_view->model()->PopupIsOpen());

  // Quickly type another query and accept it before getting suggestions back
  // for the query. The popup will close after accepting input - ensure that it
  // does not reopen when the extension returns its suggestions.
  extensions::ResultCatcher catcher;

  // TODO: Rather than send this second request by talking to the controller
  // directly, figure out how to send it via the proper calls to
  // location_bar or location_bar->().
  AutocompleteInput input(u"kw command", metrics::OmniboxEventProto::NTP,
                          ChromeAutocompleteSchemeClassifier(profile));
  autocomplete_controller->Start(input);
  location_bar->AcceptInput();
  WaitForAutocompleteDone(browser());
  EXPECT_TRUE(autocomplete_controller->done());
  // This checks that the keyword provider (via javascript)
  // gets told to navigate to the string "command".
  EXPECT_TRUE(catcher.GetNextResult()) << catcher.message();
  EXPECT_FALSE(omnibox_view->model()->PopupIsOpen());
}

// Tests deleting a deletable omnibox extension suggestion result.
// Flaky on Windows and Linux TSan. https://crbug.com/1287949
#if BUILDFLAG(IS_WIN) || (BUILDFLAG(IS_LINUX) && defined(THREAD_SANITIZER))
#define MAYBE_DeleteOmniboxSuggestionResult \
  DISABLED_DeleteOmniboxSuggestionResult
#else
#define MAYBE_DeleteOmniboxSuggestionResult DeleteOmniboxSuggestionResult
#endif
IN_PROC_BROWSER_TEST_F(OmniboxApiTest, MAYBE_DeleteOmniboxSuggestionResult) {
  ASSERT_TRUE(RunExtensionTest("omnibox")) << message_;

  // The results depend on the TemplateURLService being loaded. Make sure it is
  // loaded so that the autocomplete results are consistent.
  Profile* profile = browser()->profile();
  search_test_utils::WaitForTemplateURLServiceToLoad(
      TemplateURLServiceFactory::GetForProfile(profile));

  AutocompleteController* autocomplete_controller =
      GetAutocompleteController(browser());

  chrome::FocusLocationBar(browser());
  ASSERT_TRUE(ui_test_utils::IsViewFocused(browser(), VIEW_ID_OMNIBOX));

  // Input a keyword query and wait for suggestions from the extension.
  InputKeys(browser(), {ui::VKEY_K, ui::VKEY_W, ui::VKEY_TAB, ui::VKEY_D});

  WaitForAutocompleteDone(browser());
  EXPECT_TRUE(autocomplete_controller->done());

  // Peek into the controller to see if it has the results we expect.
  const AutocompleteResult& result = autocomplete_controller->result();
  ASSERT_EQ(4U, result.size()) << AutocompleteResultAsString(result);

  EXPECT_EQ(u"kw d", result.match_at(0).fill_into_edit);
  EXPECT_EQ(AutocompleteProvider::TYPE_KEYWORD,
            result.match_at(0).provider->type());
  EXPECT_FALSE(result.match_at(0).deletable);

  EXPECT_EQ(u"kw n1", result.match_at(1).fill_into_edit);
  EXPECT_EQ(AutocompleteProvider::TYPE_KEYWORD,
            result.match_at(1).provider->type());
  // Verify that the first omnibox extension suggestion is deletable.
  EXPECT_TRUE(result.match_at(1).deletable);

  EXPECT_EQ(u"kw n2", result.match_at(2).fill_into_edit);
  EXPECT_EQ(AutocompleteProvider::TYPE_KEYWORD,
            result.match_at(2).provider->type());
  // Verify that the second omnibox extension suggestion is not deletable.
  EXPECT_FALSE(result.match_at(2).deletable);

  EXPECT_EQ(u"kw d", result.match_at(3).fill_into_edit);
  EXPECT_EQ(AutocompleteMatchType::SEARCH_WHAT_YOU_TYPED,
            result.match_at(3).type);
  EXPECT_FALSE(result.match_at(3).deletable);

// This test portion is excluded from Mac because the Mac key combination
// FN+SHIFT+DEL used to delete an omnibox suggestion cannot be reproduced.
// This is because the FN key is not supported in interactive_test_util.h.
#if !BUILDFLAG(IS_MAC)
  ExtensionTestMessageListener delete_suggestion_listener(
      "onDeleteSuggestion: des1", false);

  // Skip the first suggestion result.
  EXPECT_TRUE(ui_test_utils::SendKeyPressSync(browser(), ui::VKEY_DOWN, false,
                                              false, false, false));
  // Delete the second suggestion result. On Linux, this is done via SHIFT+DEL.
  EXPECT_TRUE(ui_test_utils::SendKeyPressSync(browser(), ui::VKEY_DELETE, false,
                                              true, false, false));
  // Verify that the onDeleteSuggestion event was fired.
  ASSERT_TRUE(delete_suggestion_listener.WaitUntilSatisfied());

  // Verify that the first suggestion result was deleted. There should be one
  // less suggestion result, 3 now instead of 4.
  ASSERT_EQ(3U, result.size());
  EXPECT_EQ(u"kw d", result.match_at(0).fill_into_edit);
  EXPECT_EQ(u"kw n2", result.match_at(1).fill_into_edit);
  EXPECT_EQ(u"kw d", result.match_at(2).fill_into_edit);
#endif
}

// Tests typing something but not staying in keyword mode.
IN_PROC_BROWSER_TEST_F(OmniboxApiTest, ExtensionSuggestionsOnlyInKeywordMode) {
  ASSERT_TRUE(RunExtensionTest("omnibox")) << message_;

  // The results depend on the TemplateURLService being loaded. Make sure it is
  // loaded so that the autocomplete results are consistent.
  Profile* profile = browser()->profile();
  search_test_utils::WaitForTemplateURLServiceToLoad(
      TemplateURLServiceFactory::GetForProfile(profile));

  AutocompleteController* autocomplete_controller =
      GetAutocompleteController(browser());

  chrome::FocusLocationBar(browser());
  ASSERT_TRUE(ui_test_utils::IsViewFocused(browser(), VIEW_ID_OMNIBOX));

  // Input a keyword query and wait for suggestions from the extension.
  InputKeys(browser(), {ui::VKEY_K, ui::VKEY_W, ui::VKEY_SPACE, ui::VKEY_D});
  WaitForAutocompleteDone(browser());
  EXPECT_TRUE(autocomplete_controller->done());

  // Peek into the controller to see if it has the results we expect.
  {
    const AutocompleteResult& result = autocomplete_controller->result();
    ASSERT_EQ(4U, result.size()) << AutocompleteResultAsString(result);

    EXPECT_EQ(u"kw d", result.match_at(0).fill_into_edit);
    EXPECT_EQ(AutocompleteProvider::TYPE_KEYWORD,
              result.match_at(0).provider->type());

    EXPECT_EQ(u"kw n1", result.match_at(1).fill_into_edit);
    EXPECT_EQ(AutocompleteProvider::TYPE_KEYWORD,
              result.match_at(1).provider->type());

    EXPECT_EQ(u"kw n2", result.match_at(2).fill_into_edit);
    EXPECT_EQ(AutocompleteProvider::TYPE_KEYWORD,
              result.match_at(2).provider->type());

    EXPECT_EQ(u"kw d", result.match_at(3).fill_into_edit);
    EXPECT_EQ(AutocompleteMatchType::SEARCH_WHAT_YOU_TYPED,
              result.match_at(3).type);
  }

  // Now clear the omnibox by pressing escape multiple times, focus the
  // omnibox, then type the same text again except with a backspace after
  // the "kw ".  This will cause leaving keyword mode so the full text will be
  // "kw d" without a keyword chip displayed.  This middle step of focussing
  // the omnibox is necessary because on Mac pressing escape can make the
  // omnibox lose focus.
  InputKeys(browser(), {ui::VKEY_ESCAPE, ui::VKEY_ESCAPE});
  chrome::FocusLocationBar(browser());
  ASSERT_TRUE(ui_test_utils::IsViewFocused(browser(), VIEW_ID_OMNIBOX));
  InputKeys(browser(), {ui::VKEY_K, ui::VKEY_W, ui::VKEY_SPACE, ui::VKEY_BACK,
                        ui::VKEY_D});

  WaitForAutocompleteDone(browser());
  EXPECT_TRUE(autocomplete_controller->done());

  // Peek into the controller to see if it has the results we expect.  Since
  // the user left keyword mode, the extension should not be the top-ranked
  // match nor should it have provided suggestions.  (It can and should provide
  // a match to query the extension for exactly what the user typed however.)
  {
    const AutocompleteResult& result = autocomplete_controller->result();
    ASSERT_EQ(2U, result.size()) << AutocompleteResultAsString(result);

    EXPECT_EQ(u"kw d", result.match_at(0).fill_into_edit);
    EXPECT_EQ(AutocompleteMatchType::SEARCH_WHAT_YOU_TYPED,
              result.match_at(0).type);

    EXPECT_EQ(u"kw d", result.match_at(1).fill_into_edit);
    EXPECT_EQ(AutocompleteProvider::TYPE_KEYWORD,
              result.match_at(1).provider->type());
  }
}

using ContextType = extensions::ExtensionBrowserTest::ContextType;

class OmniboxApiTestWithContextType
    : public extensions::ExtensionApiTest,
      public testing::WithParamInterface<ContextType> {
 public:
  OmniboxApiTestWithContextType() : extensions::ExtensionApiTest(GetParam()) {}
  ~OmniboxApiTestWithContextType() override = default;
};

IN_PROC_BROWSER_TEST_P(OmniboxApiTestWithContextType,
                       SetDefaultSuggestionFailures) {
  constexpr char kManifest[] =
      R"({
           "name": "SetDefaultSuggestion",
           "manifest_version": 2,
           "version": "0.1",
           "omnibox": { "keyword": "word" },
           "background": { "scripts": [ "background.js" ], "persistent": true }
         })";
  constexpr char kBackground[] =
      R"(chrome.test.runTests([
           function testSetDefaultSuggestionThrowsWithContentField() {
             // Note: This test is mostly for historical benefit. Previously,
             // we had manual coverage to ensure developers did not pass an
             // object with `content`; now, this is handled by the bindings
             // system. There's no special handling in this API, but given the
             // historical behavior, we add extra coverage here.
             const invalidSuggestion = {
                 description: 'description',
                 content: 'content',
             };
             const expectedError = /Unexpected property: 'content'./;
             chrome.test.assertThrows(
                 chrome.omnibox.setDefaultSuggestion,
                 [invalidSuggestion],
                 expectedError);
             chrome.test.succeed();
           },
           async function invalidXml_NoClosingTag() {
             // Service worker-based and background page-based extensions behave
             // differently here. In the background page case, the description
             // is parsed synchronously in the renderer; this results in an
             // error being emitted that must be caught with a try/catch. In
             // service worker contexts, we parse the description
             // asynchronously from the browser, and the error is returned via
             // runtime.lastError.
             // Because of this difference, getting the emitted error is a bit
             // of a pain.
             let expectedError;
             let error = await new Promise((resolve) => {
               try {
                 chrome.omnibox.setDefaultSuggestion(
                     {description: '<tag> <match>match</match> world'},
                     () => {
                       expectedError = /Failed to parse suggestion./;
                       resolve(chrome.runtime.lastError.message);
                     });
               } catch (e) {
                 expectedError = /Opening and ending tag mismatch/;
                 resolve(e.message);
               }
             });
             chrome.test.assertTrue(expectedError.test(error), error);
             chrome.test.succeed();
           }
         ]);)";
  extensions::TestExtensionDir test_dir;
  test_dir.WriteManifest(kManifest);
  test_dir.WriteFile(FILE_PATH_LITERAL("background.js"), kBackground);
  ASSERT_TRUE(RunExtensionTest(test_dir.UnpackedPath(), {}, {})) << message_;
}

IN_PROC_BROWSER_TEST_P(OmniboxApiTestWithContextType, SetDefaultSuggestion) {
  constexpr char kManifest[] =
      R"({
           "name": "SetDefaultSuggestion",
           "manifest_version": 2,
           "version": "0.1",
           "omnibox": { "keyword": "word" },
           "background": { "scripts": [ "background.js" ], "persistent": true }
         })";
  constexpr char kBackground[] =
      R"(chrome.test.runTests([
           function setDefaultSuggestion() {
             chrome.omnibox.setDefaultSuggestion(
                 {description: 'hello <match>match</match> world'},
                 () => {
                   chrome.test.succeed();
                 });
           }
         ]);)";
  extensions::TestExtensionDir test_dir;
  test_dir.WriteManifest(kManifest);
  test_dir.WriteFile(FILE_PATH_LITERAL("background.js"), kBackground);
  ASSERT_TRUE(RunExtensionTest(test_dir.UnpackedPath(), {}, {})) << message_;

  // The results depend on the TemplateURLService being loaded. Make sure it is
  // loaded so that the autocomplete results are consistent.
  // TODO(devlin): Hoist this into a SetUp() method?
  search_test_utils::WaitForTemplateURLServiceToLoad(
      TemplateURLServiceFactory::GetForProfile(profile()));

  AutocompleteController* autocomplete_controller =
      GetAutocompleteController(browser());

  chrome::FocusLocationBar(browser());
  ASSERT_TRUE(ui_test_utils::IsViewFocused(browser(), VIEW_ID_OMNIBOX));

  // Input a keyword query and wait for suggestions from the extension.
  // Note that we need to add a character after the keyword for the service to
  // trigger the extension.
  InputKeys(browser(), {ui::VKEY_W, ui::VKEY_O, ui::VKEY_R, ui::VKEY_D,
                        ui::VKEY_SPACE, ui::VKEY_D});
  WaitForAutocompleteDone(browser());
  EXPECT_TRUE(autocomplete_controller->done());

  const AutocompleteResult& result = autocomplete_controller->result();
  ASSERT_EQ(2u, result.size()) << AutocompleteResultAsString(result);

  {
    const AutocompleteMatch& match = result.match_at(0);
    EXPECT_EQ(AutocompleteMatchType::SEARCH_OTHER_ENGINE, match.type);
    EXPECT_EQ(AutocompleteProvider::TYPE_KEYWORD, match.provider->type());

    // The "description" given by the extension is shown as the "contents" in
    // the AutocompleteMatch. The XML-marked string is
    // "hello <match>match</match> world", which is then shown as
    // "hello match world".
    // It should have 3 "components": an unstyled "hello ", a match-styled
    // "match", and an unstyled " world".
    const ExpectedMatchComponents expected_components = {
        {u"hello ", ACMatchClassification::NONE},
        {u"match", ACMatchClassification::MATCH},
        {u" world", ACMatchClassification::NONE},
    };
    VerifyMatchComponents(expected_components, match);
  }

  {
    const AutocompleteMatch& match = result.match_at(1);
    EXPECT_EQ(u"word d", match.fill_into_edit);
    EXPECT_EQ(AutocompleteMatchType::SEARCH_WHAT_YOU_TYPED, match.type);
  }
}

INSTANTIATE_TEST_SUITE_P(ServiceWorker,
                         OmniboxApiTestWithContextType,
                         testing::Values(ContextType::kServiceWorker));
INSTANTIATE_TEST_SUITE_P(PersistentBackground,
                         OmniboxApiTestWithContextType,
                         testing::Values(ContextType::kPersistentBackground));
