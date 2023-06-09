// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/profiles/profile_picker_view.h"

#include "base/barrier_closure.h"
#include "base/callback_helpers.h"
#include "base/json/values_util.h"
#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/bind.h"
#include "base/test/metrics/histogram_tester.h"
#include "base/test/mock_callback.h"
#include "base/test/scoped_feature_list.h"
#include "base/time/time.h"
#include "build/build_config.h"
#include "build/chromeos_buildflags.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/feature_engagement/tracker_factory.h"
#include "chrome/browser/interstitials/chrome_settings_page_helper.h"
#include "chrome/browser/metrics/first_web_contents_profiler_base.h"
#include "chrome/browser/policy/cloud/user_policy_signin_service.h"
#include "chrome/browser/policy/cloud/user_policy_signin_service_factory.h"
#include "chrome/browser/profiles/profile_attributes_entry.h"
#include "chrome/browser/profiles/profile_attributes_storage.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/signin/chrome_signin_client_factory.h"
#include "chrome/browser/signin/chrome_signin_client_test_util.h"
#include "chrome/browser/signin/dice_tab_helper.h"
#include "chrome/browser/signin/identity_manager_factory.h"
#include "chrome/browser/sync/sync_encryption_keys_tab_helper.h"
#include "chrome/browser/sync/sync_service_factory.h"
#include "chrome/browser/themes/theme_service.h"
#include "chrome/browser/themes/theme_service_factory.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/sync/profile_signin_confirmation_helper.h"
#include "chrome/browser/ui/tab_dialogs.h"
#include "chrome/browser/ui/ui_features.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/profiles/avatar_toolbar_button.h"
#include "chrome/browser/ui/views/profiles/profile_picker_test_base.h"
#include "chrome/browser/ui/webui/signin/enterprise_profile_welcome_handler.h"
#include "chrome/browser/ui/webui/signin/enterprise_profile_welcome_ui.h"
#include "chrome/browser/ui/webui/signin/login_ui_service.h"
#include "chrome/browser/ui/webui/signin/login_ui_service_factory.h"
#include "chrome/browser/ui/webui/signin/login_ui_test_utils.h"
#include "chrome/browser/ui/webui/signin/profile_picker_handler.h"
#include "chrome/browser/ui/webui/signin/profile_picker_ui.h"
#include "chrome/browser/ui/webui/signin/signin_url_utils.h"
#include "chrome/common/pref_names.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/profile_deletion_observer.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/feature_engagement/public/feature_constants.h"
#include "components/feature_engagement/public/tracker.h"
#include "components/feature_engagement/test/test_tracker.h"
#include "components/keyed_service/content/browser_context_dependency_manager.h"
#include "components/policy/core/browser/browser_policy_connector.h"
#include "components/policy/core/common/mock_configuration_policy_provider.h"
#include "components/policy/policy_constants.h"
#include "components/prefs/pref_service.h"
#include "components/signin/public/identity_manager/account_info.h"
#include "components/signin/public/identity_manager/accounts_in_cookie_jar_info.h"
#include "components/signin/public/identity_manager/identity_manager.h"
#include "components/signin/public/identity_manager/identity_test_utils.h"
#include "components/signin/public/identity_manager/primary_account_mutator.h"
#include "components/startup_metric_utils/browser/startup_metric_utils.h"
#include "components/sync/base/sync_prefs.h"
#include "components/sync/driver/sync_service.h"
#include "components/sync/driver/sync_user_settings.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "google_apis/gaia/gaia_urls.h"
#include "services/network/test/test_url_loader_factory.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/views/widget/widget_delegate.h"
#include "url/gurl.h"

#if BUILDFLAG(IS_CHROMEOS_LACROS)
#include "components/account_manager_core/chromeos/account_manager.h"
#include "components/account_manager_core/chromeos/account_manager_facade_factory.h"
#include "components/account_manager_core/chromeos/account_manager_mojo_service.h"
#include "components/account_manager_core/chromeos/fake_account_manager_ui.h"
#endif  // BUILDFLAG(IS_CHROMEOS_LACROS)

namespace {
const SkColor kProfileColor = SK_ColorRED;

#if !BUILDFLAG(IS_CHROMEOS_LACROS)
// State of the the ForceEphemeralProfiles policy.
enum class ForceEphemeralProfilesPolicy { kUnset, kEnabled, kDisabled };

const char16_t kOriginalProfileName[] = u"OriginalProfile";
const char16_t kWork[] = u"Work";
#endif  // !BUILDFLAG(IS_CHROMEOS_LACROS)

AccountInfo FillAccountInfo(
    const CoreAccountInfo& core_info,
    const std::string& given_name,
    const std::string& hosted_domain = kNoHostedDomainFound) {
  AccountInfo account_info;
  account_info.email = core_info.email;
  account_info.gaia = core_info.gaia;
  account_info.account_id = core_info.account_id;
  account_info.is_under_advanced_protection =
      core_info.is_under_advanced_protection;
  account_info.full_name = "Test Full Name";
  account_info.given_name = given_name;
  account_info.hosted_domain = hosted_domain;
  account_info.locale = "en";
  account_info.picture_url = "https://get-avatar.com/foo";
  return account_info;
}

GURL GetSyncConfirmationURL(SkColor profile_color = kProfileColor) {
  return AppendSyncConfirmationQueryParams(
      GURL("chrome://sync-confirmation/"),
      {/*is_modal=*/false, SyncConfirmationUI::DesignVersion::kColored,
       profile_color});
}

#if BUILDFLAG(IS_CHROMEOS_LACROS)

class FakeAccountManagerUIAddAccountDialogWaiter
    : public FakeAccountManagerUI::Observer {
 public:
  explicit FakeAccountManagerUIAddAccountDialogWaiter(
      FakeAccountManagerUI* account_manager_ui) {
    scoped_observation_.Observe(account_manager_ui);
  }
  ~FakeAccountManagerUIAddAccountDialogWaiter() override = default;

  void Wait() { run_loop_.Run(); }

  // FakeAccountManagerUI::Observer:
  void OnAddAccountDialogShown() override { run_loop_.Quit(); }

 private:
  base::RunLoop run_loop_;
  base::ScopedObservation<FakeAccountManagerUI, FakeAccountManagerUI::Observer>
      scoped_observation_{this};
};

#endif  // BUILDFLAG(IS_CHROMEOS_LACROS)

class BrowserAddedWaiter : public BrowserListObserver {
 public:
  explicit BrowserAddedWaiter(size_t total_count) : total_count_(total_count) {
    BrowserList::AddObserver(this);
  }

  BrowserAddedWaiter(const BrowserAddedWaiter&) = delete;
  BrowserAddedWaiter& operator=(const BrowserAddedWaiter&) = delete;

  ~BrowserAddedWaiter() override { BrowserList::RemoveObserver(this); }

  Browser* Wait() {
    if (BrowserList::GetInstance()->size() == total_count_)
      return BrowserList::GetInstance()->GetLastActive();
    run_loop_.Run();
    EXPECT_TRUE(browser_);
    return browser_;
  }

 private:
  // BrowserListObserver implementation.
  void OnBrowserAdded(Browser* browser) override {
    if (BrowserList::GetInstance()->size() != total_count_)
      return;
    browser_ = browser;
    run_loop_.Quit();
  }

  const size_t total_count_;
  raw_ptr<Browser> browser_ = nullptr;
  base::RunLoop run_loop_;
};

// Fake user policy signin service immediately invoking the callbacks.
class FakeUserPolicySigninService : public policy::UserPolicySigninService {
 public:
  static std::unique_ptr<KeyedService> Build(content::BrowserContext* context) {
    Profile* profile = Profile::FromBrowserContext(context);
    return std::make_unique<FakeUserPolicySigninService>(
        profile, IdentityManagerFactory::GetForProfile(profile), std::string(),
        std::string());
  }

  static std::unique_ptr<KeyedService> BuildForEnterprise(
      content::BrowserContext* context) {
    Profile* profile = Profile::FromBrowserContext(context);
    // Non-empty dm token & client id means enterprise account.
    return std::make_unique<FakeUserPolicySigninService>(
        profile, IdentityManagerFactory::GetForProfile(profile), "foo", "bar");
  }

  FakeUserPolicySigninService(Profile* profile,
                              signin::IdentityManager* identity_manager,
                              const std::string& dm_token,
                              const std::string& client_id)
      : UserPolicySigninService(profile,
                                nullptr,
                                nullptr,
                                nullptr,
                                identity_manager,
                                nullptr),
        dm_token_(dm_token),
        client_id_(client_id) {}

  // policy::UserPolicySigninService:
  void RegisterForPolicyWithAccountId(
      const std::string& username,
      const CoreAccountId& account_id,
      PolicyRegistrationCallback callback) override {
    std::move(callback).Run(dm_token_, client_id_);
  }

  // policy::UserPolicySigninServiceBase:
  void FetchPolicyForSignedInUser(
      const AccountId& account_id,
      const std::string& dm_token,
      const std::string& client_id,
      scoped_refptr<network::SharedURLLoaderFactory> test_shared_loader_factory,
      PolicyFetchCallback callback) override {
    std::move(callback).Run(true);
  }

 private:
  std::string dm_token_;
  std::string client_id_;
};

class TestTabDialogs : public TabDialogs {
 public:
  TestTabDialogs(content::WebContents* contents, base::RunLoop* run_loop)
      : contents_(contents), run_loop_(run_loop) {}
  ~TestTabDialogs() override = default;

  // Creates a platform specific instance, and attaches it to |contents|.
  // If an instance is already attached, it overwrites it.
  static void OverwriteForWebContents(content::WebContents* contents,
                                      base::RunLoop* run_loop) {
    DCHECK(contents);
    contents->SetUserData(UserDataKey(),
                          std::make_unique<TestTabDialogs>(contents, run_loop));
  }

  gfx::NativeView GetDialogParentView() const override {
    return contents_->GetNativeView();
  }
  void ShowCollectedCookies() override {}
  void ShowHungRendererDialog(
      content::RenderWidgetHost* render_widget_host,
      base::RepeatingClosure hang_monitor_restarter) override {}
  void HideHungRendererDialog(
      content::RenderWidgetHost* render_widget_host) override {}
  bool IsShowingHungRendererDialog() override { return false; }

  void ShowManagePasswordsBubble(bool user_action) override {}
  void HideManagePasswordsBubble() override {}

 private:
  raw_ptr<content::WebContents> contents_;
  raw_ptr<base::RunLoop> run_loop_;
};

std::unique_ptr<KeyedService> CreateTestTracker(content::BrowserContext*) {
  return feature_engagement::CreateTestTracker();
}

class PageNonEmptyPaintObserver : public content::WebContentsObserver {
 public:
  explicit PageNonEmptyPaintObserver(const GURL& url,
                                     content::WebContents* web_contents)
      : WebContentsObserver(web_contents),
        barrier_closure_(base::BarrierClosure(2, run_loop_.QuitClosure())),
        url_(url) {}

  void Wait() {
    // Check if the right page has already been painted or loaded.
    if (web_contents()->GetLastCommittedURL() == url_) {
      if (web_contents()->CompletedFirstVisuallyNonEmptyPaint())
        DidFirstVisuallyNonEmptyPaint();
      if (!web_contents()->IsLoading())
        DidStopLoading();
    }

    run_loop_.Run();
  }

 private:
  // WebContentsObserver:
  void DidFirstVisuallyNonEmptyPaint() override {
    // Making sure that the same event does not trigger the barrier twice.
    if (did_paint_)
      return;

    did_paint_ = true;
    barrier_closure_.Run();
  }

  void DidStopLoading() override {
    ASSERT_EQ(web_contents()->GetLastCommittedURL(), url_);

    // Making sure that the same event does not trigger the barrier twice.
    if (did_load_)
      return;

    // It shouldn't technically be necessary to wait for load stop here, we do
    // this to be consistent with the other tests relying on `WaitForLoadStop()`
    did_load_ = true;
    barrier_closure_.Run();
  }

  base::RunLoop run_loop_;
  base::RepeatingClosure barrier_closure_;
  GURL url_;

  bool did_paint_ = false;
  bool did_load_ = false;
};

// Waits for a first non empty paint for `target` and expects that it will load
// the given `url`.
void WaitForFirstNonEmptyPaint(const GURL& url, content::WebContents* target) {
  ASSERT_NE(target, nullptr);

  PageNonEmptyPaintObserver observer(url, target);
  observer.Wait();
}

}  // namespace

class ProfilePickerCreationFlowBrowserTest : public ProfilePickerTestBase {
 public:
  ProfilePickerCreationFlowBrowserTest()
      : feature_list_(feature_engagement::kIPHProfileSwitchFeature) {}

  void SetUpInProcessBrowserTestFixture() override {
    ProfilePickerTestBase::SetUpInProcessBrowserTestFixture();
    create_services_subscription_ =
        BrowserContextDependencyManager::GetInstance()
            ->RegisterCreateServicesCallbackForTesting(
                base::BindRepeating(&ProfilePickerCreationFlowBrowserTest::
                                        OnWillCreateBrowserContextServices,
                                    base::Unretained(this)));
  }

  void SetUpOnMainThread() override {
    ProfilePickerTestBase::SetUpOnMainThread();

    // Avoid showing the What's New page. These tests assume this isn't the
    // first update and the NTP opens after sign in.
    g_browser_process->local_state()->SetInteger(prefs::kLastWhatsNewVersion,
                                                 CHROME_VERSION_MAJOR);
  }

  virtual void OnWillCreateBrowserContextServices(
      content::BrowserContext* context) {
    policy::UserPolicySigninServiceFactory::GetInstance()->SetTestingFactory(
        context, base::BindRepeating(&FakeUserPolicySigninService::Build));
    feature_engagement::TrackerFactory::GetInstance()->SetTestingFactory(
        context, base::BindRepeating(&CreateTestTracker));

    // Clear the previous cookie responses (if any) before using it for a new
    // profile (as test_url_loader_factory() is shared across profiles).
    test_url_loader_factory()->ClearResponses();
    ChromeSigninClientFactory::GetInstance()->SetTestingFactory(
        context, base::BindRepeating(&BuildChromeSigninClientWithURLLoader,
                                     test_url_loader_factory()));
  }

  Profile* SignInForNewProfile(
      const GURL& target_url,
      const std::string& email,
      const std::string& given_name,
      const std::string& hosted_domain = kNoHostedDomainFound) {
#if BUILDFLAG(IS_CHROMEOS_LACROS)
    return LacrosSignIn(target_url, email, given_name, hosted_domain);
#else
    Profile* profile_being_created = StartDiceSignIn();
    FinishDiceSignIn(profile_being_created, email, given_name, hosted_domain);
    WaitForLoadStop(target_url);
    return profile_being_created;
#endif
  }

#if BUILDFLAG(IS_CHROMEOS_LACROS)
  Profile* LacrosSignIn(
      const GURL& target_url,
      const std::string& email,
      const std::string& given_name,
      const std::string& hosted_domain = kNoHostedDomainFound) {
    ProfilePicker::Show(ProfilePicker::EntryPoint::kProfileMenuAddNewProfile);
    // Wait until webUI is fully initialized.
    const GURL kNewProfileUrl("chrome://profile-picker/new-profile");
    WaitForLoadStop(kNewProfileUrl);

    // Fake clicking the "Next"/"Sign in" button.
    base::ListValue args;
    args.Append(/*color=*/static_cast<int>(kProfileColor));
    args.Append(/*gaiaId=*/base::Value(base::Value::Type::STRING));
    web_contents()->GetWebUI()->ProcessWebUIMessage(
        kNewProfileUrl, "loadSignInProfileCreationFlow", args);

    // Wait for the Ash UI to show up.
    FakeAccountManagerUI* fake_ui = GetFakeAccountManagerUI();
    FakeAccountManagerUIAddAccountDialogWaiter(fake_ui).Wait();

    // Fake the OS account addition.
    account_manager::AccountKey kAccountKey{
        "some_gaia_id", account_manager::AccountType::kGaia};
    auto* account_manager = MaybeGetAshAccountManagerForTests();
    DCHECK(account_manager);
    account_manager->UpsertAccount(kAccountKey, email, "access_token");

    // Fake that this account was successfully added via the UI.
    crosapi::AccountManagerMojoService* mojo_service =
        MaybeGetAshAccountManagerMojoServiceForTests();
    DCHECK(mojo_service);
    mojo_service->OnAccountAdditionFinishedForTesting(
        account_manager::AccountAdditionResult::FromAccount(
            {kAccountKey, email}));
    fake_ui->CloseDialog();

    WaitForLoadStop(target_url);
    Profile* profile_being_created =
        static_cast<Profile*>(web_contents()->GetBrowserContext());

    // Add full account info.
    signin::IdentityManager* identity_manager =
        IdentityManagerFactory::GetForProfile(profile_being_created);
    CoreAccountInfo core_account_info =
        identity_manager->GetPrimaryAccountInfo(signin::ConsentLevel::kSignin);
    AccountInfo account_info =
        FillAccountInfo(core_account_info, given_name, hosted_domain);
    signin::UpdateAccountInfoForAccount(identity_manager, account_info);

    return profile_being_created;
  }
#else
  // Opens the Gaia signin page in the profile creation flow. Returns the new
  // profile that was created.
  Profile* StartDiceSignIn() {
    ProfilePicker::Show(ProfilePicker::EntryPoint::kProfileMenuAddNewProfile);
    // Wait until webUI is fully initialized.
    WaitForLoadStop(GURL("chrome://profile-picker/new-profile"));

    // Simulate a click on the signin button.
    base::MockCallback<base::OnceCallback<void(bool)>> switch_finished_callback;
    EXPECT_CALL(switch_finished_callback, Run(true));
    ProfilePicker::SwitchToDiceSignIn(kProfileColor,
                                      switch_finished_callback.Get());

    // The DICE navigation happens in a new web contents (for the profile being
    // created), wait for it.
    WaitForLoadStop(GaiaUrls::GetInstance()->signin_chrome_sync_dice());
    return static_cast<Profile*>(web_contents()->GetBrowserContext());
  }

  AccountInfo FinishDiceSignIn(
      Profile* profile_being_created,
      const std::string& email,
      const std::string& given_name,
      const std::string& hosted_domain = kNoHostedDomainFound) {
    // Add an account - simulate a successful Gaia sign-in.
    signin::IdentityManager* identity_manager =
        IdentityManagerFactory::GetForProfile(profile_being_created);
    CoreAccountInfo core_account_info =
        signin::MakeAccountAvailable(identity_manager, email);
    signin::SetCookieAccounts(identity_manager, test_url_loader_factory(),
                              {{email, core_account_info.gaia}});
    EXPECT_TRUE(identity_manager->HasAccountWithRefreshToken(
        core_account_info.account_id));

    AccountInfo account_info =
        FillAccountInfo(core_account_info, given_name, hosted_domain);
    signin::UpdateAccountInfoForAccount(identity_manager, account_info);
    return account_info;
  }
#endif  // BUILDFLAG(IS_CHROMEOS_LACROS)

  // Returns true if the profile switch IPH has been shown.
  bool ProfileSwitchPromoHasBeenShown(Browser* browser) {
    feature_engagement::Tracker* tracker =
        feature_engagement::TrackerFactory::GetForBrowserContext(
            browser->profile());

    base::RunLoop loop;
    tracker->AddOnInitializedCallback(
        base::BindLambdaForTesting([&loop](bool success) {
          DCHECK(success);
          loop.Quit();
        }));
    loop.Run();

    EXPECT_TRUE(tracker->IsInitialized());
    return tracker->GetTriggerState(
               feature_engagement::kIPHProfileSwitchFeature) ==
           feature_engagement::Tracker::TriggerState::HAS_BEEN_DISPLAYED;
  }

  // Simulates a click on a profile card. The profile picker must be already
  // opened.
  void OpenProfileFromPicker(const base::FilePath& profile_path,
                             bool open_settings) {
    base::ListValue args;
    args.Append(
        base::Value::ToUniquePtrValue(base::FilePathToValue(profile_path)));
    profile_picker_handler()->HandleLaunchSelectedProfile(open_settings, &args);
  }

  // Simulates a click on "Browse as Guest".
  void OpenGuestFromPicker() {
    base::ListValue args;
    profile_picker_handler()->HandleLaunchGuestProfile(&args);
  }

  // Creates a new profile without opening a browser.
  base::FilePath CreateNewProfileWithoutBrowser() {
    // Create a second profile.
    ProfileManager* profile_manager = g_browser_process->profile_manager();
    base::FilePath path = profile_manager->GenerateNextProfileDirectoryPath();
    base::RunLoop run_loop;
    profile_manager->CreateProfileAsync(
        path, base::BindLambdaForTesting(
                  [&run_loop](Profile* profile, Profile::CreateStatus status) {
                    if (status == Profile::CREATE_STATUS_INITIALIZED) {
                      // Avoid showing the welcome page.
                      profile->GetPrefs()->SetBoolean(
                          prefs::kHasSeenWelcomePage, true);
                      run_loop.Quit();
                    }
                  }));
    run_loop.Run();
    return path;
  }

  // Returns profile picker webUI handler. Profile picker must be opened before
  // calling this function.
  ProfilePickerHandler* profile_picker_handler() {
    DCHECK(ProfilePicker::IsOpen());
    return web_contents()
        ->GetWebUI()
        ->GetController()
        ->GetAs<ProfilePickerUI>()
        ->GetProfilePickerHandlerForTesting();
  }

  network::TestURLLoaderFactory* test_url_loader_factory() {
    return &test_url_loader_factory_;
  }

 private:
  network::TestURLLoaderFactory test_url_loader_factory_;
  base::CallbackListSubscription create_services_subscription_;
  base::test::ScopedFeatureList feature_list_;
};

IN_PROC_BROWSER_TEST_F(ProfilePickerCreationFlowBrowserTest, ShowPicker) {
  ProfilePicker::Show(ProfilePicker::EntryPoint::kOnStartup);
  EXPECT_TRUE(ProfilePicker::IsOpen());
  WaitForPickerWidgetCreated();
  // Check that non-default accessible title is provided both before the page
  // loads and after it loads.
  views::WidgetDelegate* delegate = widget()->widget_delegate();
  EXPECT_NE(delegate->GetWindowTitle(), delegate->GetAccessibleWindowTitle());
  WaitForLoadStop(GURL("chrome://profile-picker"));
  EXPECT_NE(delegate->GetWindowTitle(), delegate->GetAccessibleWindowTitle());
}

IN_PROC_BROWSER_TEST_F(ProfilePickerCreationFlowBrowserTest, ShowChoice) {
  ProfilePicker::Show(ProfilePicker::EntryPoint::kProfileMenuAddNewProfile);
  EXPECT_TRUE(ProfilePicker::IsOpen());
  WaitForPickerWidgetCreated();
  // Check that non-default accessible title is provided both before the page
  // loads and after it loads.
  views::WidgetDelegate* delegate = widget()->widget_delegate();
  EXPECT_NE(delegate->GetWindowTitle(), delegate->GetAccessibleWindowTitle());
  WaitForLoadStop(GURL("chrome://profile-picker/new-profile"));
  EXPECT_NE(delegate->GetWindowTitle(), delegate->GetAccessibleWindowTitle());
}

IN_PROC_BROWSER_TEST_F(ProfilePickerCreationFlowBrowserTest,
                       CreateSignedInProfile) {
  ASSERT_EQ(1u, BrowserList::GetInstance()->size());
  // Simulate a successful sign-in and wait for the sign-in to propagate to the
  // flow, resulting in sync confirmation screen getting displayed.
  Profile* profile_being_created = SignInForNewProfile(
      GetSyncConfirmationURL(), "joe.consumer@gmail.com", "Joe");

  // Simulate closing the UI with "No, thanks".
  LoginUIServiceFactory::GetForProfile(profile_being_created)
      ->SyncConfirmationUIClosed(LoginUIService::ABORT_SYNC);
  Browser* new_browser = BrowserAddedWaiter(2u).Wait();
  WaitForLoadStop(GURL("chrome://newtab/"),
                  new_browser->tab_strip_model()->GetActiveWebContents());

  // Check expectations when the profile creation flow is done.
  WaitForPickerClosed();

  ProfileAttributesEntry* entry =
      g_browser_process->profile_manager()
          ->GetProfileAttributesStorage()
          .GetProfileAttributesWithPath(profile_being_created->GetPath());
  ASSERT_NE(entry, nullptr);
  EXPECT_FALSE(entry->IsEphemeral());
  EXPECT_EQ(entry->GetLocalProfileName(), u"Joe");

  syncer::SyncService* sync_service =
      SyncServiceFactory::GetForProfile(profile_being_created);
  EXPECT_FALSE(entry->IsAuthenticated());
  EXPECT_FALSE(sync_service->HasSyncConsent());
  EXPECT_EQ(ThemeServiceFactory::GetForProfile(profile_being_created)
                ->GetAutogeneratedThemeColor(),
            kProfileColor);
}

#if !BUILDFLAG(IS_CHROMEOS_LACROS)
// Regression test for crbug.com/1266415.
IN_PROC_BROWSER_TEST_F(ProfilePickerCreationFlowBrowserTest,
                       CreateSignedInProfileWithSyncEncryptionKeys) {
  ASSERT_EQ(1u, BrowserList::GetInstance()->size());
  StartDiceSignIn();

  // It would be nicer to verify that HasEncryptionKeysApiForTesting()
  // returns true but this isn't possible because the sigin page returns an
  // error, without setting up a fake HTTP server.
  EXPECT_NE(SyncEncryptionKeysTabHelper::FromWebContents(web_contents()),
            nullptr);
}

// Regression test for crbug.com/1196290. Makes no sense for lacros because you
// cannot sign-in twice in the same way on lacros.
IN_PROC_BROWSER_TEST_F(ProfilePickerCreationFlowBrowserTest,
                       CreateSignedInProfileAfterCancellingFirstAttempt) {
  ASSERT_EQ(1u, BrowserList::GetInstance()->size());
  // Simulate a successful sign-in and wait for the sign-in to propagate to the
  // flow, resulting in sync confirmation screen getting displayed.
  Profile* profile_to_cancel = SignInForNewProfile(
      GetSyncConfirmationURL(), "joe.consumer@gmail.com", "Joe");

  // Close the flow with the [X] button.
  base::FilePath canceled_path = profile_to_cancel->GetPath();
  widget()->CloseWithReason(views::Widget::ClosedReason::kCloseButtonClicked);
  WaitForPickerClosed();

  ProfileAttributesStorage& storage =
      g_browser_process->profile_manager()->GetProfileAttributesStorage();
  // The canceled profile got marked for deletion.
  ASSERT_EQ(storage.GetProfileAttributesWithPath(canceled_path), nullptr);

  // Restart the flow again. As the flow for `profile_to_cancel` got aborted,
  // it's disregarded. Instead of the profile switch screen, the normal sync
  // confirmation should appear.
  Profile* profile_being_created = SignInForNewProfile(
      GetSyncConfirmationURL(), "joe.consumer@gmail.com", "Joe");
  EXPECT_NE(canceled_path, profile_being_created->GetPath());

  // Simulate closing the UI with "No, thanks".
  LoginUIServiceFactory::GetForProfile(profile_being_created)
      ->SyncConfirmationUIClosed(LoginUIService::ABORT_SYNC);
  Browser* new_browser = BrowserAddedWaiter(2u).Wait();
  WaitForLoadStop(GURL("chrome://newtab/"),
                  new_browser->tab_strip_model()->GetActiveWebContents());

  // Check expectations when the profile creation flow is done.
  WaitForPickerClosed();

  ProfileAttributesEntry* entry =
      storage.GetProfileAttributesWithPath(profile_being_created->GetPath());
  ASSERT_NE(entry, nullptr);
  EXPECT_FALSE(entry->IsEphemeral());
  EXPECT_EQ(entry->GetLocalProfileName(), u"Joe");

  syncer::SyncService* sync_service =
      SyncServiceFactory::GetForProfile(profile_being_created);
  EXPECT_FALSE(entry->IsAuthenticated());
  EXPECT_FALSE(sync_service->HasSyncConsent());
  EXPECT_EQ(ThemeServiceFactory::GetForProfile(profile_being_created)
                ->GetAutogeneratedThemeColor(),
            kProfileColor);
}

IN_PROC_BROWSER_TEST_F(ProfilePickerCreationFlowBrowserTest,
                       CancelWhileSigningIn) {
  ASSERT_EQ(1u, BrowserList::GetInstance()->size());
  Profile* profile_to_cancel = StartDiceSignIn();
  base::FilePath profile_to_cancel_path = profile_to_cancel->GetPath();

  // Close the flow with the [X] button.
  widget()->CloseWithReason(views::Widget::ClosedReason::kCloseButtonClicked);
  WaitForPickerClosed();

  // The profile entry is deleted.
  ProfileAttributesEntry* entry =
      g_browser_process->profile_manager()
          ->GetProfileAttributesStorage()
          .GetProfileAttributesWithPath(profile_to_cancel_path);
  EXPECT_EQ(entry, nullptr);
}

// Regression test for crbug.com/1278726.
IN_PROC_BROWSER_TEST_F(ProfilePickerCreationFlowBrowserTest,
                       CancelWhileSigningInBeforeProfileCreated) {
  ASSERT_EQ(1u, BrowserList::GetInstance()->size());
  ProfilePicker::Show(ProfilePicker::EntryPoint::kProfileMenuAddNewProfile);
  // Wait until webUI is fully initialized.
  WaitForLoadStop(GURL("chrome://profile-picker/new-profile"));

  // Simulate a click on the signin button.
  base::MockCallback<base::OnceCallback<void(bool)>> switch_finished_callback;
  EXPECT_CALL(switch_finished_callback, Run).Times(0);
  ProfilePicker::SwitchToDiceSignIn(kProfileColor,
                                    switch_finished_callback.Get());

  // Close the flow immediately with the [X] button before
  // `switch_finished_callback` gets called (and before the respective profile
  // gets created).
  widget()->CloseWithReason(views::Widget::ClosedReason::kCloseButtonClicked);
  // The flow should not crash.
  WaitForPickerClosed();
}

IN_PROC_BROWSER_TEST_F(ProfilePickerCreationFlowBrowserTest,
                       CancelWhileSigningInWithNoOtherWindow) {
  ASSERT_EQ(1u, BrowserList::GetInstance()->size());
  Profile* profile_to_cancel = StartDiceSignIn();
  base::FilePath profile_to_cancel_path = profile_to_cancel->GetPath();

  // First close all browser windows to make sure Chrome quits when closing the
  // flow.
  CloseBrowserSynchronously(browser());
  ASSERT_EQ(0u, BrowserList::GetInstance()->size());

  // Close the flow with the [X] button.
  widget()->CloseWithReason(views::Widget::ClosedReason::kCloseButtonClicked);
  WaitForPickerClosed();

  // The profile entry is deleted.
  ProfileAttributesEntry* entry =
      g_browser_process->profile_manager()
          ->GetProfileAttributesStorage()
          .GetProfileAttributesWithPath(profile_to_cancel_path);
  EXPECT_EQ(entry, nullptr);

  // Still no browser window is open.
  EXPECT_EQ(0u, BrowserList::GetInstance()->size());
}

// Tests dice-specific logic for keeping track of the new profile color.
IN_PROC_BROWSER_TEST_F(ProfilePickerCreationFlowBrowserTest,
                       CreateSignedInProfileDiceReenter) {
  ASSERT_EQ(1u, BrowserList::GetInstance()->size());
  Profile* profile_being_created = StartDiceSignIn();

  // Simulate the sign-in screen get re-entered with a different color
  // (configured on the local profile screen).
  const SkColor kDifferentProfileColor = SK_ColorBLUE;
  base::MockCallback<base::OnceCallback<void(bool)>> switch_finished_callback;
  EXPECT_CALL(switch_finished_callback, Run(true));
  ProfilePicker::SwitchToDiceSignIn(kDifferentProfileColor,
                                    switch_finished_callback.Get());

  // Simulate a successful Gaia sign-in.
  FinishDiceSignIn(profile_being_created, "joe.consumer@gmail.com", "Joe");

  // Wait for the sign-in to propagate to the flow, resulting in sync
  // confirmation screen getting displayed.
  WaitForLoadStop(GetSyncConfirmationURL(kDifferentProfileColor));

  // Simulate closing the UI with "No, thanks".
  LoginUIServiceFactory::GetForProfile(profile_being_created)
      ->SyncConfirmationUIClosed(LoginUIService::ABORT_SYNC);
  Browser* new_browser = BrowserAddedWaiter(2u).Wait();
  WaitForLoadStop(GURL("chrome://newtab/"),
                  new_browser->tab_strip_model()->GetActiveWebContents());

  // Check expectations when the profile creation flow is done.
  WaitForPickerClosed();

  ProfileAttributesEntry* entry =
      g_browser_process->profile_manager()
          ->GetProfileAttributesStorage()
          .GetProfileAttributesWithPath(profile_being_created->GetPath());
  ASSERT_NE(entry, nullptr);
  EXPECT_FALSE(entry->IsEphemeral());
  EXPECT_EQ(entry->GetLocalProfileName(), u"Joe");

  syncer::SyncService* sync_service =
      SyncServiceFactory::GetForProfile(profile_being_created);
  EXPECT_FALSE(entry->IsAuthenticated());
  EXPECT_FALSE(sync_service->HasSyncConsent());
  EXPECT_EQ(ThemeServiceFactory::GetForProfile(profile_being_created)
                ->GetAutogeneratedThemeColor(),
            kDifferentProfileColor);
}
#endif  // !BUILDFLAG(IS_CHROMEOS_LACROS)

IN_PROC_BROWSER_TEST_F(ProfilePickerCreationFlowBrowserTest,
                       CreateSignedInProfileSettings) {
  ASSERT_EQ(1u, BrowserList::GetInstance()->size());
  // Simulate a successful sign-in and wait for the sign-in to propagate to the
  // flow, resulting in sync confirmation screen getting displayed.
  Profile* profile_being_created = SignInForNewProfile(
      GetSyncConfirmationURL(), "joe.consumer@gmail.com", "Joe");

  // Simulate closing the UI with "Yes, I'm in".
  LoginUIServiceFactory::GetForProfile(profile_being_created)
      ->SyncConfirmationUIClosed(LoginUIService::CONFIGURE_SYNC_FIRST);
  Browser* new_browser = BrowserAddedWaiter(2u).Wait();
  WaitForLoadStop(GURL("chrome://settings/syncSetup"),
                  new_browser->tab_strip_model()->GetActiveWebContents());

  // Check expectations when the profile creation flow is done.
  WaitForPickerClosed();

  ProfileAttributesEntry* entry =
      g_browser_process->profile_manager()
          ->GetProfileAttributesStorage()
          .GetProfileAttributesWithPath(profile_being_created->GetPath());
  ASSERT_NE(entry, nullptr);
  EXPECT_FALSE(entry->IsEphemeral());
  EXPECT_EQ(entry->GetLocalProfileName(), u"Joe");

  // Sync is getting configured.
  EXPECT_TRUE(entry->IsAuthenticated());
  syncer::SyncService* sync_service =
      SyncServiceFactory::GetForProfile(profile_being_created);
  EXPECT_TRUE(sync_service->HasSyncConsent());
  EXPECT_FALSE(sync_service->GetUserSettings()->IsFirstSetupComplete());

  // The color is not applied if the user enters settings.
  EXPECT_FALSE(ThemeServiceFactory::GetForProfile(profile_being_created)
                   ->UsingAutogeneratedTheme());
}

// The following tests rely on dice specific logic. Some of them could be
// extended to cover lacros as well.
#if !BUILDFLAG(IS_CHROMEOS_LACROS)
IN_PROC_BROWSER_TEST_F(ProfilePickerCreationFlowBrowserTest,
                       CreateSignedInProfileOpenLink) {
  ASSERT_EQ(1u, BrowserList::GetInstance()->size());
  StartDiceSignIn();

  // Simulate clicking on a link that opens in a new window.
  const GURL kURL("https://foo.google.com");
  EXPECT_TRUE(ExecuteScript(web_contents(),
                            "var link = document.createElement('a');"
                            "link.href = '" +
                                kURL.spec() +
                                "';"
                                "link.target = '_blank';"
                                "document.body.appendChild(link);"
                                "link.click();"));
  // A new pppup browser is displayed (with the specified URL).
  Browser* new_browser = BrowserAddedWaiter(2u).Wait();
  EXPECT_EQ(new_browser->type(), Browser::TYPE_POPUP);
  WaitForLoadStop(kURL, new_browser->tab_strip_model()->GetActiveWebContents());
}

// Regression test for crbug.com/1219980.
// TODO(crbug.com/1219535): Re-implement the test bases on the final fix.
IN_PROC_BROWSER_TEST_F(ProfilePickerCreationFlowBrowserTest,
                       CreateSignedInProfileSecurityInterstitials) {
  ASSERT_EQ(1u, BrowserList::GetInstance()->size());
  StartDiceSignIn();

  // Simulate clicking on the settings link in a security interstitial (that
  // appears in the sign-in flow e.g. due to broken internet connection).
  security_interstitials::ChromeSettingsPageHelper::
      CreateChromeSettingsPageHelper()
          ->OpenEnhancedProtectionSettings(web_contents());
  // Nothing happens, the browser should not crash.
  base::RunLoop().RunUntilIdle();
}

// TODO(crbug.com/1248040): Extend this test to support lacros.
IN_PROC_BROWSER_TEST_F(ProfilePickerCreationFlowBrowserTest,
                       CreateSignedInProfileExtendedInfoTimeout) {
  ASSERT_EQ(1u, BrowserList::GetInstance()->size());
  Profile* profile_being_created = StartDiceSignIn();

  signin::IdentityManager* identity_manager =
      IdentityManagerFactory::GetForProfile(profile_being_created);

  // Make it work without waiting for a long delay.
  ProfilePicker::SetExtendedAccountInfoTimeoutForTesting(
      base::Milliseconds(10));

  // Add an account - simulate a successful Gaia sign-in.
  CoreAccountInfo core_account_info =
      signin::MakeAccountAvailable(identity_manager, "joe.consumer@gmail.com");
  signin::SetCookieAccounts(
      identity_manager, test_url_loader_factory(),
      {{"joe.consumer@gmail.com", core_account_info.gaia}});
  ASSERT_TRUE(identity_manager->HasAccountWithRefreshToken(
      core_account_info.account_id));

  // Wait for the sign-in to propagate to the flow, resulting in sync
  // confirmation screen getting displayed.
  WaitForLoadStop(GetSyncConfirmationURL());

  // Simulate closing the UI with "Yes, I'm in".
  LoginUIServiceFactory::GetForProfile(profile_being_created)
      ->SyncConfirmationUIClosed(LoginUIService::ABORT_SYNC);
  Browser* new_browser = BrowserAddedWaiter(2u).Wait();
  WaitForLoadStop(GURL("chrome://newtab/"),
                  new_browser->tab_strip_model()->GetActiveWebContents());

  // Check expectations when the profile creation flow is done.
  WaitForPickerClosed();

  ProfileAttributesEntry* entry =
      g_browser_process->profile_manager()
          ->GetProfileAttributesStorage()
          .GetProfileAttributesWithPath(profile_being_created->GetPath());
  ASSERT_NE(entry, nullptr);
  EXPECT_FALSE(entry->IsEphemeral());
  // Since the given name is not provided, the email address is used instead as
  // a profile name.
  EXPECT_EQ(entry->GetLocalProfileName(), u"joe.consumer@gmail.com");

  syncer::SyncService* sync_service =
      SyncServiceFactory::GetForProfile(profile_being_created);
  EXPECT_FALSE(entry->IsAuthenticated());
  EXPECT_FALSE(sync_service->HasSyncConsent());
  EXPECT_EQ(ThemeServiceFactory::GetForProfile(profile_being_created)
                ->GetAutogeneratedThemeColor(),
            kProfileColor);
}

IN_PROC_BROWSER_TEST_F(ProfilePickerCreationFlowBrowserTest,
                       CreateSignedInProfileWithSAML) {
  const GURL kNonGaiaURL("https://signin.saml-provider.com/");

  ASSERT_EQ(1u, BrowserList::GetInstance()->size());
  Profile* profile_being_created = StartDiceSignIn();

  // Redirect the web contents to a non gaia url (simulating a SAML page).
  content::WebContents* wc = web_contents();
  wc->GetController().LoadURL(kNonGaiaURL, content::Referrer(),
                              ui::PAGE_TRANSITION_AUTO_TOPLEVEL, std::string());
  Browser* new_browser = BrowserAddedWaiter(2u).Wait();
  WaitForLoadStop(kNonGaiaURL, wc);
  WaitForPickerClosed();

  // Check that the web contents got actually moved to the browser.
  EXPECT_EQ(wc, new_browser->tab_strip_model()->GetActiveWebContents());
  EXPECT_NE(nullptr, DiceTabHelper::FromWebContents(wc));

  ProfileAttributesEntry* entry =
      g_browser_process->profile_manager()
          ->GetProfileAttributesStorage()
          .GetProfileAttributesWithPath(profile_being_created->GetPath());
  ASSERT_NE(entry, nullptr);
  EXPECT_FALSE(entry->IsEphemeral());
  EXPECT_FALSE(entry->IsAuthenticated());
  EXPECT_EQ(entry->GetLocalProfileName(), kWork);
  // The color is not applied if the user enters the SAML flow.
  EXPECT_FALSE(ThemeServiceFactory::GetForProfile(profile_being_created)
                   ->UsingAutogeneratedTheme());
}

// TODO(crbug.com/1248040): Extend this test to support lacros.
// Regression test for crash https://crbug.com/1195784.
// Crash requires specific conditions to be reproduced. Browser should have 2
// profiles with the same GAIA account name and the first profile should use
// default local name. This is set up specifically in order to trigger
// ProfileAttributesStorage::NotifyIfProfileNamesHaveChanged() when a new third
// profile is added.
IN_PROC_BROWSER_TEST_F(ProfilePickerCreationFlowBrowserTest,
                       PRE_ProfileNameChangesOnProfileAdded) {
  Profile* default_profile = browser()->profile();
  AccountInfo default_account_info =
      FinishDiceSignIn(default_profile, "joe@gmail.com", "Joe");
  IdentityManagerFactory::GetForProfile(default_profile)
      ->GetPrimaryAccountMutator()
      ->SetPrimaryAccount(default_account_info.account_id,
                          signin::ConsentLevel::kSync);

  // Create a second profile.
  base::RunLoop run_loop;
  Profile* second_profile = nullptr;
  ProfileManager::CreateMultiProfileAsync(
      u"Joe", /*icon_index=*/0, /*is_hidden=*/false,
      base::BindLambdaForTesting(
          [&](Profile* profile, Profile::CreateStatus status) {
            if (status == Profile::CREATE_STATUS_INITIALIZED) {
              second_profile = profile;
              run_loop.Quit();
            }
          }));
  run_loop.Run();
  AccountInfo second_profile_info =
      FinishDiceSignIn(second_profile, "joe.secondary@gmail.com", "Joe");
  IdentityManagerFactory::GetForProfile(second_profile)
      ->GetPrimaryAccountMutator()
      ->SetPrimaryAccount(second_profile_info.account_id,
                          signin::ConsentLevel::kSync);

  // The first profile should use default name.
  ProfileAttributesEntry* default_profile_entry =
      g_browser_process->profile_manager()
          ->GetProfileAttributesStorage()
          .GetProfileAttributesWithPath(default_profile->GetPath());
  ASSERT_NE(default_profile_entry, nullptr);
  EXPECT_TRUE(default_profile_entry->IsUsingDefaultName());

  ProfileAttributesEntry* second_profile_entry =
      g_browser_process->profile_manager()
          ->GetProfileAttributesStorage()
          .GetProfileAttributesWithPath(second_profile->GetPath());
  ASSERT_NE(second_profile_entry, nullptr);

  // Both profiles should have matching GAIA name.
  EXPECT_EQ(default_profile_entry->GetGAIANameToDisplay(),
            second_profile_entry->GetGAIANameToDisplay());
}

IN_PROC_BROWSER_TEST_F(ProfilePickerCreationFlowBrowserTest,
                       ProfileNameChangesOnProfileAdded) {
  EXPECT_EQ(g_browser_process->profile_manager()->GetNumberOfProfiles(), 2u);

  // This should not crash.
  StartDiceSignIn();
}
#endif  // !BUILDFLAG(IS_CHROMEOS_LACROS)

IN_PROC_BROWSER_TEST_F(ProfilePickerCreationFlowBrowserTest,
                       OpenPickerAndClose) {
  ProfilePicker::Show(ProfilePicker::EntryPoint::kProfileMenuManageProfiles);
  WaitForLoadStop(GURL("chrome://profile-picker"));
  EXPECT_TRUE(ProfilePicker::IsOpen());
  ProfilePicker::Hide();
  WaitForPickerClosed();
}

// Regression test for https://crbug.com/1205147.
IN_PROC_BROWSER_TEST_F(ProfilePickerCreationFlowBrowserTest,
                       OpenPickerWhileClosing) {
  // Open the first picker.
  ProfilePicker::Show(ProfilePicker::EntryPoint::kProfileMenuManageProfiles);
  WaitForLoadStop(GURL("chrome://profile-picker"));
  EXPECT_TRUE(ProfilePicker::IsOpen());

  // Request to open the second picker window while the first one is still
  // closing.
  ProfilePicker::Hide();
  ProfilePicker::Show(ProfilePicker::EntryPoint::kProfileLocked);

  // The first picker should be closed and the second picker should be
  // displayed.
  WaitForPickerClosedAndReopenedImmediately();
}

IN_PROC_BROWSER_TEST_F(ProfilePickerCreationFlowBrowserTest, OpenProfile) {
  base::HistogramTester histogram_tester;

  AvatarToolbarButton::SetIPHMinDelayAfterCreationForTesting(base::Seconds(0));
  auto lock = BrowserFeaturePromoController::BlockActiveWindowCheckForTesting();
  ASSERT_EQ(1u, BrowserList::GetInstance()->size());
  // Create a second profile.
  base::FilePath other_path = CreateNewProfileWithoutBrowser();
  // Open the picker.
  ProfilePicker::Show(ProfilePicker::EntryPoint::kProfileMenuManageProfiles);
  WaitForLoadStop(GURL("chrome://profile-picker"));
  // Open the other profile.
  OpenProfileFromPicker(other_path, /*open_settings=*/false);
  // Browser for the profile is displayed.
  Browser* new_browser = BrowserAddedWaiter(2u).Wait();
  WaitForFirstNonEmptyPaint(
      GURL("chrome://newtab/"),
      new_browser->tab_strip_model()->GetActiveWebContents());
  EXPECT_EQ(new_browser->profile()->GetPath(), other_path);
  WaitForPickerClosed();
  // IPH is shown.
  EXPECT_TRUE(ProfileSwitchPromoHasBeenShown(new_browser));

  // FirstProfileTime.* histograms aren't recorded because the picker
  // is opened from the menu.
  EXPECT_THAT(histogram_tester.GetTotalCountsForPrefix(
                  "ProfilePicker.FirstProfileTime."),
              testing::IsEmpty());
}

IN_PROC_BROWSER_TEST_F(ProfilePickerCreationFlowBrowserTest,
                       OpenProfileFromStartup) {
  base::HistogramTester histogram_tester;
  ASSERT_FALSE(ProfilePicker::IsOpen());

  // Create a second profile.
  base::FilePath other_path = CreateNewProfileWithoutBrowser();

  // Open the picker.
  ProfilePicker::Show(ProfilePicker::EntryPoint::kOnStartup);
  EXPECT_TRUE(ProfilePicker::IsOpen());
  WaitForLoadStop(GURL("chrome://profile-picker"));

  // Open the new profile.
  OpenProfileFromPicker(other_path, /*open_settings=*/false);

  // Measurement of startup performance started.

  // Browser for the profile is displayed.
  Browser* new_browser = BrowserAddedWaiter(2u).Wait();
  WaitForFirstNonEmptyPaint(
      GURL("chrome://newtab/"),
      new_browser->tab_strip_model()->GetActiveWebContents());
  EXPECT_EQ(new_browser->profile()->GetPath(), other_path);
  WaitForPickerClosed();

  histogram_tester.ExpectTotalCount(
      "ProfilePicker.FirstProfileTime.FirstWebContentsNonEmptyPaint", 1);
  histogram_tester.ExpectUniqueSample(
      "ProfilePicker.FirstProfileTime.FirstWebContentsFinishReason",
      metrics::StartupProfilingFinishReason::kDone, 1);
}

IN_PROC_BROWSER_TEST_F(ProfilePickerCreationFlowBrowserTest,
                       OpenProfile_Settings) {
  AvatarToolbarButton::SetIPHMinDelayAfterCreationForTesting(base::Seconds(0));
  ASSERT_EQ(1u, BrowserList::GetInstance()->size());
  // Create a second profile.
  base::FilePath other_path = CreateNewProfileWithoutBrowser();
  // Open the picker.
  ProfilePicker::Show(ProfilePicker::EntryPoint::kProfileMenuManageProfiles);
  WaitForLoadStop(GURL("chrome://profile-picker"));
  // Open the other profile.
  OpenProfileFromPicker(other_path, /*open_settings=*/true);
  // Browser for the profile is displayed.
  Browser* new_browser = BrowserAddedWaiter(2u).Wait();
  WaitForLoadStop(GURL("chrome://settings/manageProfile"),
                  new_browser->tab_strip_model()->GetActiveWebContents());
  EXPECT_EQ(new_browser->profile()->GetPath(), other_path);
  WaitForPickerClosed();
  // IPH is not shown.
  EXPECT_FALSE(ProfileSwitchPromoHasBeenShown(new_browser));
}

IN_PROC_BROWSER_TEST_F(ProfilePickerCreationFlowBrowserTest,
                       OpenURL_PickerClosed) {
  ASSERT_EQ(1u, BrowserList::GetInstance()->size());
  const GURL kTargetURL("chrome://settings/help");
  // Create a profile.
  base::FilePath profile_path = CreateNewProfileWithoutBrowser();
  // Open the picker.
  ProfilePicker::Show(ProfilePicker::EntryPoint::kProfileMenuManageProfiles,
                      kTargetURL);
  WaitForLoadStop(GURL("chrome://profile-picker"));
  // Open the profile.
  OpenProfileFromPicker(profile_path, /*open_settings=*/false);
  // Browser for the profile is displayed.
  Browser* new_browser = BrowserAddedWaiter(2u).Wait();
  WaitForLoadStop(kTargetURL,
                  new_browser->tab_strip_model()->GetActiveWebContents());
  EXPECT_EQ(new_browser->profile()->GetPath(), profile_path);
  WaitForPickerClosed();
}

IN_PROC_BROWSER_TEST_F(ProfilePickerCreationFlowBrowserTest,
                       OpenURL_PickerAlreadyOpen) {
  ASSERT_EQ(1u, BrowserList::GetInstance()->size());
  const GURL kTargetURL("chrome://settings/help");
  // Create a profile.
  base::FilePath profile_path = CreateNewProfileWithoutBrowser();
  // Open the picker without target URL.
  ProfilePicker::Show(ProfilePicker::EntryPoint::kProfileMenuManageProfiles);
  WaitForLoadStop(GURL("chrome://profile-picker"));
  // Request a URL when the picker is already open.
  ProfilePicker::Show(ProfilePicker::EntryPoint::kProfileMenuManageProfiles,
                      kTargetURL);
  // Open the profile.
  OpenProfileFromPicker(profile_path, /*open_settings=*/false);
  // Browser for the profile is displayed.
  Browser* new_browser = BrowserAddedWaiter(2u).Wait();
  WaitForLoadStop(kTargetURL,
                  new_browser->tab_strip_model()->GetActiveWebContents());
  EXPECT_EQ(new_browser->profile()->GetPath(), profile_path);
  WaitForPickerClosed();
}

// Regression test for https://crbug.com/1199035
IN_PROC_BROWSER_TEST_F(ProfilePickerCreationFlowBrowserTest,
                       OpenProfile_Guest) {
  AvatarToolbarButton::SetIPHMinDelayAfterCreationForTesting(base::Seconds(0));
  ASSERT_EQ(1u, BrowserList::GetInstance()->size());
  // Create a second profile.
  base::FilePath other_path = CreateNewProfileWithoutBrowser();
  // Open the picker.
  ProfilePicker::Show(ProfilePicker::EntryPoint::kProfileMenuManageProfiles);
  WaitForLoadStop(GURL("chrome://profile-picker"));
  // Open a Guest profile.
  OpenGuestFromPicker();
  // Browser for the guest profile is displayed.
  Browser* new_browser = BrowserAddedWaiter(2u).Wait();
  WaitForLoadStop(GURL("chrome://newtab"),
                  new_browser->tab_strip_model()->GetActiveWebContents());
  EXPECT_TRUE(new_browser->profile()->IsGuestSession());
  WaitForPickerClosed();
  // IPH is not shown.
  EXPECT_FALSE(ProfileSwitchPromoHasBeenShown(new_browser));
}

// Local profiles are not supported on lacros.
#if !BUILDFLAG(IS_CHROMEOS_LACROS)
// Closes the default browser window before creating a new profile in the
// profile picker.
// Regression test for https://crbug.com/1144092.
IN_PROC_BROWSER_TEST_F(ProfilePickerCreationFlowBrowserTest,
                       CloseBrowserBeforeCreatingNewProfile) {
  ASSERT_EQ(1u, BrowserList::GetInstance()->size());

  // Open the picker.
  ProfilePicker::Show(ProfilePicker::EntryPoint::kProfileMenuManageProfiles);
  WaitForLoadStop(GURL("chrome://profile-picker"));

  // Close the browser window.
  BrowserList::GetInstance()->CloseAllBrowsersWithProfile(browser()->profile());
  ui_test_utils::WaitForBrowserToClose(browser());
  base::RunLoop().RunUntilIdle();
  ASSERT_EQ(0u, BrowserList::GetInstance()->size());

  // Imitate creating a new profile through the profile picker.
  ProfilePickerHandler* handler = profile_picker_handler();
  base::ListValue args;
  args.Append(u"My Profile");                    // Profile name.
  args.Append(std::make_unique<base::Value>());  // Profile color.
  args.Append(0);                                // Avatar index.
  args.Append(false);                            // Create shortcut.
  handler->HandleCreateProfile(&args);

  BrowserAddedWaiter(1u).Wait();
  EXPECT_EQ(1u, BrowserList::GetInstance()->size());
  WaitForPickerClosed();
}
#endif  // !BUILDFLAG(IS_CHROMEOS_LACROS)

class ProfilePickerEnterpriseCreationFlowBrowserTest
    : public ProfilePickerCreationFlowBrowserTest {
 public:
  void OnWillCreateBrowserContextServices(
      content::BrowserContext* context) override {
    ProfilePickerCreationFlowBrowserTest::OnWillCreateBrowserContextServices(
        context);
    policy::UserPolicySigninServiceFactory::GetInstance()->SetTestingFactory(
        context,
        base::BindRepeating(&FakeUserPolicySigninService::BuildForEnterprise));
  }

  void ExpectEnterpriseScreenTypeAndProceed(
      EnterpriseProfileWelcomeUI::ScreenType expected_type,
      bool proceed) {
    EnterpriseProfileWelcomeHandler* handler =
        web_contents()
            ->GetWebUI()
            ->GetController()
            ->GetAs<EnterpriseProfileWelcomeUI>()
            ->GetHandlerForTesting();
    EXPECT_EQ(handler->GetTypeForTesting(), expected_type);

    // Simulate clicking on the next button.
    handler->CallProceedCallbackForTesting(proceed);
  }
};

IN_PROC_BROWSER_TEST_F(ProfilePickerEnterpriseCreationFlowBrowserTest,
                       CreateSignedInProfile) {
  ASSERT_EQ(1u, BrowserList::GetInstance()->size());
  // Simulate a successful sign-in and wait for the sign-in to propagate to the
  // flow, resulting in enterprise welcome screen getting displayed.
  // Consumer-looking gmail address avoids code that forces the sync service to
  // actually start which would add overhead in mocking further stuff.
  // Enterprise domain needed for this profile being detected as Work.
  Profile* profile_being_created =
      SignInForNewProfile(GURL("chrome://enterprise-profile-welcome/"),
                          "joe.enterprise@gmail.com", "Joe", "enterprise.com");

  ExpectEnterpriseScreenTypeAndProceed(
      /*expected_type=*/EnterpriseProfileWelcomeUI::ScreenType::
          kEntepriseAccountSyncEnabled,
      /*proceed=*/true);

  WaitForLoadStop(GetSyncConfirmationURL());
  // Simulate finishing the flow with "No, thanks".
  LoginUIServiceFactory::GetForProfile(profile_being_created)
      ->SyncConfirmationUIClosed(LoginUIService::ABORT_SYNC);

  Browser* new_browser = BrowserAddedWaiter(2u).Wait();
  WaitForLoadStop(GURL("chrome://newtab/"),
                  new_browser->tab_strip_model()->GetActiveWebContents());
  WaitForPickerClosed();

  // Check expectations when the profile creation flow is done.
  ProfileAttributesEntry* entry =
      g_browser_process->profile_manager()
          ->GetProfileAttributesStorage()
          .GetProfileAttributesWithPath(profile_being_created->GetPath());
  ASSERT_NE(entry, nullptr);
  EXPECT_NE(entry->GetGAIAId(), std::string());
  EXPECT_FALSE(entry->IsEphemeral());
  EXPECT_EQ(entry->GetLocalProfileName(), u"enterprise.com");

  syncer::SyncService* sync_service =
      SyncServiceFactory::GetForProfile(profile_being_created);
  EXPECT_FALSE(entry->IsAuthenticated());
  EXPECT_FALSE(sync_service->HasSyncConsent());
  EXPECT_EQ(ThemeServiceFactory::GetForProfile(profile_being_created)
                ->GetAutogeneratedThemeColor(),
            kProfileColor);
}

#if !BUILDFLAG(IS_CHROMEOS_LACROS)
// TODO(crbug.com/1248047): Extend this test to support mirror.
IN_PROC_BROWSER_TEST_F(ProfilePickerEnterpriseCreationFlowBrowserTest,
                       CreateSignedInProfileWithSyncDisabled) {
  ASSERT_EQ(1u, BrowserList::GetInstance()->size());
  Profile* profile_being_created = StartDiceSignIn();

  // Set the device as managed in prefs.
  syncer::SyncPrefs prefs(profile_being_created->GetPrefs());
  prefs.SetManagedForTest(true);
  syncer::SyncService* sync_service =
      SyncServiceFactory::GetForProfile(profile_being_created);

  // Consumer-looking gmail address avoids code that forces the sync service to
  // actually start which would add overhead in mocking further stuff.
  // Enterprise domain needed for this profile being detected as Work.
  FinishDiceSignIn(profile_being_created, "joe.enterprise@gmail.com", "Joe",
                   "enterprise.com");

  // Wait for the sign-in to propagate to the flow, resulting in enterprise
  // welcome screen getting displayed.
  WaitForLoadStop(GURL("chrome://enterprise-profile-welcome/"));

  ExpectEnterpriseScreenTypeAndProceed(
      /*expected_type=*/EnterpriseProfileWelcomeUI::ScreenType::
          kConsumerAccountSyncDisabled,
      /*proceed=*/true);

  Browser* new_browser = BrowserAddedWaiter(2u).Wait();
  WaitForLoadStop(GURL("chrome://newtab/"),
                  new_browser->tab_strip_model()->GetActiveWebContents());
  WaitForPickerClosed();

  // Check expectations when the profile creation flow is done.
  ProfileAttributesEntry* entry =
      g_browser_process->profile_manager()
          ->GetProfileAttributesStorage()
          .GetProfileAttributesWithPath(profile_being_created->GetPath());
  ASSERT_NE(entry, nullptr);
  EXPECT_FALSE(entry->IsEphemeral());
  EXPECT_EQ(entry->GetLocalProfileName(), u"enterprise.com");

  // Sync is disabled.
  EXPECT_NE(entry->GetGAIAId(), std::string());
  EXPECT_FALSE(sync_service->GetUserSettings()->IsSyncRequested());
  EXPECT_EQ(ThemeServiceFactory::GetForProfile(profile_being_created)
                ->GetAutogeneratedThemeColor(),
            kProfileColor);
}
#endif  // !BUILDFLAG(IS_CHROMEOS_LACROS)

IN_PROC_BROWSER_TEST_F(ProfilePickerEnterpriseCreationFlowBrowserTest,
                       CreateSignedInProfileSettings) {
  ASSERT_EQ(1u, BrowserList::GetInstance()->size());
  // Simulate a successful sign-in and wait for the sign-in to propagate to the
  // flow, resulting in enterprise welcome screen getting displayed.
  // Consumer-looking gmail address avoids code that forces the sync service to
  // actually start which would add overhead in mocking further stuff.
  // Enterprise domain needed for this profile being detected as Work.
  Profile* profile_being_created =
      SignInForNewProfile(GURL("chrome://enterprise-profile-welcome/"),
                          "joe.enterprise@gmail.com", "Joe", "enterprise.com");

  // Wait for the sign-in to propagate to the flow, resulting in enterprise
  // welcome screen getting displayed.
  WaitForLoadStop(GURL("chrome://enterprise-profile-welcome/"));

  ExpectEnterpriseScreenTypeAndProceed(
      /*expected_type=*/EnterpriseProfileWelcomeUI::ScreenType::
          kEntepriseAccountSyncEnabled,
      /*proceed=*/true);

  WaitForLoadStop(GetSyncConfirmationURL());
  // Simulate finishing the flow with "Configure sync".
  LoginUIServiceFactory::GetForProfile(profile_being_created)
      ->SyncConfirmationUIClosed(LoginUIService::CONFIGURE_SYNC_FIRST);

  Browser* new_browser = BrowserAddedWaiter(2u).Wait();
  WaitForLoadStop(GURL("chrome://settings/syncSetup"),
                  new_browser->tab_strip_model()->GetActiveWebContents());
  WaitForPickerClosed();

  // Check expectations when the profile creation flow is done.
  ProfileAttributesEntry* entry =
      g_browser_process->profile_manager()
          ->GetProfileAttributesStorage()
          .GetProfileAttributesWithPath(profile_being_created->GetPath());
  ASSERT_NE(entry, nullptr);
  EXPECT_NE(entry->GetGAIAId(), std::string());
  EXPECT_FALSE(entry->IsEphemeral());
  EXPECT_EQ(entry->GetLocalProfileName(), u"enterprise.com");

  // Sync is getting configured.
  EXPECT_TRUE(entry->IsAuthenticated());
  syncer::SyncService* sync_service =
      SyncServiceFactory::GetForProfile(profile_being_created);
  EXPECT_TRUE(sync_service->HasSyncConsent());
  EXPECT_FALSE(sync_service->GetUserSettings()->IsFirstSetupComplete());

  // The color is not applied if the user enters settings.
  EXPECT_FALSE(ThemeServiceFactory::GetForProfile(profile_being_created)
                   ->UsingAutogeneratedTheme());
}

IN_PROC_BROWSER_TEST_F(ProfilePickerEnterpriseCreationFlowBrowserTest, Cancel) {
  ASSERT_EQ(1u, BrowserList::GetInstance()->size());
  // Simulate a successful sign-in and wait for the sign-in to propagate to the
  // flow, resulting in enterprise welcome screen getting displayed.
  // Consumer-looking gmail address avoids code that forces the sync service to
  // actually start which would add overhead in mocking further stuff.
  // Enterprise domain needed for this profile being detected as Work.
  Profile* profile_being_created =
      SignInForNewProfile(GURL("chrome://enterprise-profile-welcome/"),
                          "joe.enterprise@gmail.com", "Joe", "enterprise.com");
  base::FilePath profile_being_created_path = profile_being_created->GetPath();

  // Wait for the sign-in to propagate to the flow, resulting in enterprise
  // welcome screen getting displayed.
  WaitForLoadStop(GURL("chrome://enterprise-profile-welcome/"));

  ProfileDeletionObserver observer;
  ExpectEnterpriseScreenTypeAndProceed(
      /*expected_type=*/EnterpriseProfileWelcomeUI::ScreenType::
          kEntepriseAccountSyncEnabled,
      /*proceed=*/false);

  // As the profile creation flow was opened directly, the window is closed now.
  WaitForPickerClosed();
  observer.Wait();

  // The profile entry is deleted
  ProfileAttributesEntry* entry =
      g_browser_process->profile_manager()
          ->GetProfileAttributesStorage()
          .GetProfileAttributesWithPath(profile_being_created_path);
  EXPECT_EQ(entry, nullptr);
}

#if !BUILDFLAG(IS_CHROMEOS_LACROS)
// The switch screen tests are not related to enterprise but the functionality
// is bundled in the same feature flag. This flow cannot happen on lacros
// because the OS dialog does not allow to sign-in with an account that already
// exists in the system.
IN_PROC_BROWSER_TEST_F(ProfilePickerEnterpriseCreationFlowBrowserTest,
                       CreateSignedInProfileSigninAlreadyExists_ConfirmSwitch) {
  ASSERT_EQ(1u, BrowserList::GetInstance()->size());

  // Create a pre-existing profile syncing with the same account as the profile
  // being created.
  base::FilePath other_path = CreateNewProfileWithoutBrowser();
  ProfileAttributesStorage& storage =
      g_browser_process->profile_manager()->GetProfileAttributesStorage();
  ProfileAttributesEntry* other_entry =
      storage.GetProfileAttributesWithPath(other_path);
  ASSERT_NE(other_entry, nullptr);
  // Fake sync is enabled in this profile with Joe's account.
  other_entry->SetAuthInfo(std::string(), u"joe.consumer@gmail.com",
                           /*is_consented_primary_account=*/true);

  // Simulate a successful sign-in and wait for the sign-in to propagate to the
  // flow, resulting in profile switch screen getting displayed (in between,
  // chrome://sync-confirmation/loading gets displayed but that page may not
  // finish loading and anyway is not so relevant).
  Profile* profile_being_created =
      SignInForNewProfile(GURL("chrome://profile-picker/profile-switch"),
                          "joe.consumer@gmail.com", "Joe");
  base::FilePath profile_being_created_path = profile_being_created->GetPath();

  EXPECT_EQ(ProfilePicker::GetSwitchProfilePath(), other_path);

  // Simulate clicking on the confirm switch button.
  ProfilePickerHandler* handler = profile_picker_handler();
  base::ListValue args;
  args.Append(base::Value::ToUniquePtrValue(base::FilePathToValue(other_path)));
  handler->HandleConfirmProfileSwitch(&args);

  // Browser for a pre-existing profile is displayed.
  Browser* new_browser = BrowserAddedWaiter(2u).Wait();
  WaitForLoadStop(GURL("chrome://newtab/"),
                  new_browser->tab_strip_model()->GetActiveWebContents());
  EXPECT_EQ(new_browser->profile()->GetPath(), other_path);

  // Check expectations when the profile creation flow is done.
  WaitForPickerClosed();

  // Profile should be already deleted.
  ProfileAttributesEntry* entry =
      storage.GetProfileAttributesWithPath(profile_being_created_path);
  EXPECT_EQ(entry, nullptr);
}

IN_PROC_BROWSER_TEST_F(ProfilePickerEnterpriseCreationFlowBrowserTest,
                       CreateSignedInProfileSigninAlreadyExists_CancelSwitch) {
  ASSERT_EQ(1u, BrowserList::GetInstance()->size());

  // Create a pre-existing profile syncing with the same account as the profile
  // being created.
  base::FilePath other_path = CreateNewProfileWithoutBrowser();
  ProfileAttributesStorage& storage =
      g_browser_process->profile_manager()->GetProfileAttributesStorage();
  ProfileAttributesEntry* other_entry =
      storage.GetProfileAttributesWithPath(other_path);
  ASSERT_NE(other_entry, nullptr);
  // Fake sync is enabled in this profile with Joe's account.
  other_entry->SetAuthInfo(std::string(), u"joe.consumer@gmail.com",
                           /*is_consented_primary_account=*/true);

  // Simulate a successful sign-in and wait for the sign-in to propagate to the
  // flow, resulting in profile switch screen getting displayed (in between,
  // chrome://sync-confirmation/loading gets displayed but that page may not
  // finish loading and anyway is not so relevant).
  Profile* profile_being_created =
      SignInForNewProfile(GURL("chrome://profile-picker/profile-switch"),
                          "joe.consumer@gmail.com", "Joe");
  base::FilePath profile_being_created_path = profile_being_created->GetPath();

  // The profile switch screen should be displayed
  EXPECT_EQ(ProfilePicker::GetSwitchProfilePath(), other_path);

  // Simulate clicking on the cancel button.
  ProfilePickerHandler* handler = profile_picker_handler();
  base::ListValue args;
  handler->HandleCancelProfileSwitch(&args);

  // Check expectations when the profile creation flow is done.
  WaitForPickerClosed();

  // Only one browser should be displayed.
  EXPECT_EQ(BrowserList::GetInstance()->size(), 1u);

  // The sign-in profile should be marked for deletion.
  ProfileManager::IsProfileDirectoryMarkedForDeletion(
      profile_being_created_path);
}
#endif  // !BUILDFLAG(IS_CHROMEOS_LACROS)

// ForceEphemeralProfiles is not supported on CrOS (and thus not on lacros).
#if !BUILDFLAG(IS_CHROMEOS_LACROS)
class ProfilePickerCreationFlowEphemeralProfileBrowserTest
    : public ProfilePickerCreationFlowBrowserTest,
      public testing::WithParamInterface<ForceEphemeralProfilesPolicy> {
 public:
  ProfilePickerCreationFlowEphemeralProfileBrowserTest() = default;

  ForceEphemeralProfilesPolicy GetForceEphemeralProfilesPolicy() const {
    return GetParam();
  }

  bool AreEphemeralProfilesForced() const {
    return GetForceEphemeralProfilesPolicy() ==
           ForceEphemeralProfilesPolicy::kEnabled;
  }

  // Check that the policy was correctly applied to the preference.
  void CheckPolicyApplied(Profile* profile) {
    EXPECT_EQ(profile->GetPrefs()->GetBoolean(prefs::kForceEphemeralProfiles),
              AreEphemeralProfilesForced());
  }

  static ProfileManager* profile_manager() {
    return g_browser_process->profile_manager();
  }

  // Checks if a profile matching `name` exists in the profile manager.
  bool ProfileWithNameExists(const std::u16string& name) {
    for (const auto* entry : profile_manager()
                                 ->GetProfileAttributesStorage()
                                 .GetAllProfilesAttributes()) {
      if (entry->GetLocalProfileName() == name)
        return true;
    }
    return false;
  }

  // Checks if the original profile (the initial profile existing at the start
  // of the test) exists in the profile manager.
  bool OriginalProfileExists() {
    return ProfileWithNameExists(kOriginalProfileName);
  }

  void SetUpInProcessBrowserTestFixture() override {
    ForceEphemeralProfilesPolicy policy = GetForceEphemeralProfilesPolicy();

    if (policy != ForceEphemeralProfilesPolicy::kUnset) {
      policy::PolicyMap policy_map;
      policy_map.Set(
          policy::key::kForceEphemeralProfiles, policy::POLICY_LEVEL_MANDATORY,
          policy::POLICY_SCOPE_USER, policy::POLICY_SOURCE_CLOUD,
          base::Value(policy == ForceEphemeralProfilesPolicy::kEnabled),
          nullptr);
      policy_provider_.UpdateChromePolicy(policy_map);

      policy_provider_.SetDefaultReturns(
          /*is_initialization_complete_return=*/true,
          /*is_first_policy_load_complete_return=*/true);
      policy::BrowserPolicyConnector::SetPolicyProviderForTesting(
          &policy_provider_);
    }
    ProfilePickerCreationFlowBrowserTest::SetUpInProcessBrowserTestFixture();
  }

  void SetUpOnMainThread() override {
    ProfilePickerCreationFlowBrowserTest::SetUpOnMainThread();
    if (GetTestPreCount() == 1) {
      // Only called in "PRE_" tests, to set a name to the starting profile.
      ProfileAttributesEntry* entry =
          profile_manager()
              ->GetProfileAttributesStorage()
              .GetProfileAttributesWithPath(browser()->profile()->GetPath());
      ASSERT_NE(entry, nullptr);
      entry->SetLocalProfileName(kOriginalProfileName,
                                 entry->IsUsingDefaultName());
    }
    CheckPolicyApplied(browser()->profile());
  }

 private:
  testing::NiceMock<policy::MockConfigurationPolicyProvider> policy_provider_;
};

// Flaky on Windows: https://crbug.com/1247530.
#if BUILDFLAG(IS_WIN)
#define MAYBE_PRE_Signin DISABLED_PRE_Signin
#define MAYBE_Signin DISABLED_Signin
#else
#define MAYBE_PRE_Signin PRE_Signin
#define MAYBE_Signin Signin
#endif
// Checks that the new profile is no longer ephemeral at the end of the flow and
// still exists after restart.
IN_PROC_BROWSER_TEST_P(ProfilePickerCreationFlowEphemeralProfileBrowserTest,
                       MAYBE_PRE_Signin) {
  ASSERT_EQ(1u, BrowserList::GetInstance()->size());
  ASSERT_EQ(1u, profile_manager()->GetNumberOfProfiles());
  ASSERT_TRUE(OriginalProfileExists());

  // Simulate a successful sign-in and wait for the sign-in to propagate to the
  // flow, resulting in sync confirmation screen getting displayed.
  Profile* profile_being_created = SignInForNewProfile(
      GetSyncConfirmationURL(), "joe.consumer@gmail.com", "Joe");

  // Check that the profile is ephemeral, regardless of the policy.
  ProfileAttributesEntry* entry =
      profile_manager()
          ->GetProfileAttributesStorage()
          .GetProfileAttributesWithPath(profile_being_created->GetPath());
  ASSERT_NE(entry, nullptr);
  EXPECT_TRUE(entry->IsEphemeral());
  EXPECT_TRUE(entry->IsOmitted());

  // Simulate closing the UI with "No, thanks".
  LoginUIServiceFactory::GetForProfile(profile_being_created)
      ->SyncConfirmationUIClosed(LoginUIService::ABORT_SYNC);
  Browser* new_browser = BrowserAddedWaiter(2u).Wait();
  WaitForLoadStop(GURL("chrome://newtab/"),
                  new_browser->tab_strip_model()->GetActiveWebContents());

  WaitForPickerClosed();
  EXPECT_EQ(2u, profile_manager()->GetNumberOfProfiles());
  EXPECT_EQ(entry->GetLocalProfileName(), u"Joe");
  // The profile is no longer ephemeral, unless the policy is enabled.
  EXPECT_EQ(entry->IsEphemeral(), AreEphemeralProfilesForced());
  EXPECT_FALSE(entry->IsOmitted());
  // The preference is consistent with the policy.
  CheckPolicyApplied(profile_being_created);
}

IN_PROC_BROWSER_TEST_P(ProfilePickerCreationFlowEphemeralProfileBrowserTest,
                       MAYBE_Signin) {
  if (AreEphemeralProfilesForced()) {
    // If the policy is set, all profiles should have been deleted.
    EXPECT_EQ(1u, profile_manager()->GetNumberOfProfiles());
    // The current profile is not the one that was created in the previous run.
    EXPECT_FALSE(ProfileWithNameExists(u"Joe"));
    EXPECT_FALSE(OriginalProfileExists());
    return;
  }

  // If the policy is disabled or unset, the two profiles are still here.
  EXPECT_EQ(2u, profile_manager()->GetNumberOfProfiles());
  EXPECT_TRUE(ProfileWithNameExists(u"Joe"));
  EXPECT_TRUE(OriginalProfileExists());
}

// Flaky on Windows: https://crbug.com/1247530.
#if BUILDFLAG(IS_WIN)
#define MAYBE_PRE_ExitDuringSignin DISABLED_PRE_ExitDuringSignin
#define MAYBE_ExitDuringSignin DISABLED_ExitDuringSignin
#else
#define MAYBE_PRE_ExitDuringSignin PRE_ExitDuringSignin
#define MAYBE_ExitDuringSignin ExitDuringSignin
#endif
// Checks that the new profile is deleted on next startup if Chrome exits during
// the signin flow.
IN_PROC_BROWSER_TEST_P(ProfilePickerCreationFlowEphemeralProfileBrowserTest,
                       MAYBE_PRE_ExitDuringSignin) {
  ASSERT_EQ(1u, BrowserList::GetInstance()->size());
  ASSERT_EQ(1u, profile_manager()->GetNumberOfProfiles());
  ASSERT_TRUE(OriginalProfileExists());
  Profile* profile_being_created = StartDiceSignIn();

  // Check that the profile is ephemeral, regardless of the policy.
  ProfileAttributesEntry* entry =
      profile_manager()
          ->GetProfileAttributesStorage()
          .GetProfileAttributesWithPath(profile_being_created->GetPath());
  ASSERT_NE(entry, nullptr);
  EXPECT_TRUE(entry->IsEphemeral());
  EXPECT_TRUE(entry->IsOmitted());
  // Exit Chrome while still in the signin flow.
}

IN_PROC_BROWSER_TEST_P(ProfilePickerCreationFlowEphemeralProfileBrowserTest,
                       MAYBE_ExitDuringSignin) {
  // The profile was deleted, regardless of the policy.
  EXPECT_EQ(1u, profile_manager()->GetNumberOfProfiles());
  // The other profile still exists.
  EXPECT_NE(AreEphemeralProfilesForced(), OriginalProfileExists());
}

INSTANTIATE_TEST_SUITE_P(
    All,
    ProfilePickerCreationFlowEphemeralProfileBrowserTest,
    testing::Values(ForceEphemeralProfilesPolicy::kUnset,
                    ForceEphemeralProfilesPolicy::kDisabled,
                    ForceEphemeralProfilesPolicy::kEnabled));
#endif  // !BUILDFLAG(IS_CHROMEOS_LACROS)
