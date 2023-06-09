// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/chrome/browser/ui/authentication/enterprise/enterprise_prompt/enterprise_prompt_coordinator.h"

#import "ios/chrome/browser/chrome_url_constants.h"
#include "ios/chrome/browser/main/browser.h"
#import "ios/chrome/browser/ui/authentication/enterprise/enterprise_prompt/enterprise_prompt_view_controller.h"
#import "ios/chrome/browser/ui/commands/application_commands.h"
#import "ios/chrome/browser/ui/commands/command_dispatcher.h"
#import "ios/chrome/browser/ui/commands/open_new_tab_command.h"
#import "ios/chrome/browser/ui/commands/policy_change_commands.h"
#import "ios/chrome/common/ui/confirmation_alert/confirmation_alert_action_handler.h"
#include "url/gurl.h"

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

#if !TARGET_OS_MACCATALYST
namespace {
constexpr CGFloat kHalfSheetCornerRadius = 20;
}  // namespace
#endif

@interface EnterprisePromptCoordinator () <
    ConfirmationAlertActionHandler,
    UIAdaptivePresentationControllerDelegate>

// ViewController that contains enterprise prompt information.
@property(nonatomic, strong) EnterprisePromptViewController* viewController;

// PromptType that contains the type of the prompt to display.
@property(nonatomic, assign) EnterprisePromptType promptType;

@end

@implementation EnterprisePromptCoordinator

- (instancetype)initWithBaseViewController:(UIViewController*)baseViewController
                                   browser:(Browser*)browser
                                promptType:(EnterprisePromptType)promptType {
  if (self = [super initWithBaseViewController:baseViewController
                                       browser:browser]) {
    _promptType = promptType;
  }
  return self;
}

- (void)start {
  [super start];

  self.viewController = [[EnterprisePromptViewController alloc]
      initWithpromptType:self.promptType];
  self.viewController.presentationController.delegate = self;
  self.viewController.actionHandler = self;

#if !TARGET_OS_MACCATALYST
  if (@available(iOS 15, *)) {
    self.viewController.modalPresentationStyle = UIModalPresentationPageSheet;
    UISheetPresentationController* presentationController =
        self.viewController.sheetPresentationController;
    presentationController.prefersEdgeAttachedInCompactHeight = YES;
    presentationController.detents = @[
      UISheetPresentationControllerDetent.mediumDetent,
      UISheetPresentationControllerDetent.largeDetent
    ];
    presentationController.preferredCornerRadius = kHalfSheetCornerRadius;
  } else {
#else
  {
#endif
    self.viewController.modalPresentationStyle = UIModalPresentationFormSheet;
  }

  [self.baseViewController presentViewController:self.viewController
                                        animated:YES
                                      completion:nil];
}

- (void)stop {
  [self dismissSignOutViewController];
  [super stop];
}

#pragma mark - ConfirmationAlertActionHandler

- (void)confirmationAlertPrimaryAction {
  [self.delegate hideEnterprisePrompForLearnMore:NO];
}

- (void)confirmationAlertSecondaryAction {
  [self.delegate hideEnterprisePrompForLearnMore:YES];
  [self openManagementPage];
}

#pragma mark - UIAdaptivePresentationControllerDelegate

- (void)presentationControllerDidDismiss:
    (UIPresentationController*)presentationController {
  [self.delegate hideEnterprisePrompForLearnMore:NO];
}

#pragma mark - Private

// Removes view controller from display.
- (void)dismissSignOutViewController {
  if (self.viewController) {
    [self.baseViewController.presentedViewController
        dismissViewControllerAnimated:YES
                           completion:nil];
    self.viewController = nil;
  }
}

// Opens the management page in a new tab.
- (void)openManagementPage {
  OpenNewTabCommand* command =
      [OpenNewTabCommand commandWithURLFromChrome:GURL(kChromeUIManagementURL)];
  id<ApplicationCommands> applicationHandler = HandlerForProtocol(
      self.browser->GetCommandDispatcher(), ApplicationCommands);
  [applicationHandler openURLInNewTab:command];
}

@end
