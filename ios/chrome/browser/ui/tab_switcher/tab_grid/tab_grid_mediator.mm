// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/chrome/browser/ui/tab_switcher/tab_grid/tab_grid_mediator.h"

#import <MobileCoreServices/UTCoreTypes.h>
#import <UIKit/UIKit.h>
#include <memory>

#include "base/bind.h"
#include "base/metrics/histogram_functions.h"
#include "base/metrics/histogram_macros.h"
#include "base/metrics/user_metrics.h"
#include "base/metrics/user_metrics_action.h"
#include "base/scoped_multi_source_observation.h"
#include "base/strings/stringprintf.h"
#include "base/strings/sys_string_conversions.h"
#include "components/bookmarks/browser/bookmark_model.h"
#import "components/bookmarks/common/bookmark_pref_names.h"
#include "components/favicon/ios/web_favicon_driver.h"
#import "components/prefs/pref_service.h"
#include "components/sessions/core/tab_restore_service.h"
#include "ios/chrome/browser/bookmarks/bookmark_model_factory.h"
#include "ios/chrome/browser/browser_state/chrome_browser_state.h"
#include "ios/chrome/browser/chrome_url_constants.h"
#import "ios/chrome/browser/chrome_url_util.h"
#import "ios/chrome/browser/commerce/price_alert_util.h"
#import "ios/chrome/browser/commerce/shopping_persisted_data_tab_helper.h"
#import "ios/chrome/browser/drag_and_drop/drag_item_util.h"
#include "ios/chrome/browser/main/browser.h"
#import "ios/chrome/browser/main/browser_list.h"
#import "ios/chrome/browser/main/browser_list_factory.h"
#import "ios/chrome/browser/main/browser_util.h"
#import "ios/chrome/browser/sessions/session_restoration_browser_agent.h"
#import "ios/chrome/browser/snapshots/snapshot_browser_agent.h"
#import "ios/chrome/browser/snapshots/snapshot_cache.h"
#import "ios/chrome/browser/snapshots/snapshot_cache_observer.h"
#import "ios/chrome/browser/snapshots/snapshot_tab_helper.h"
#include "ios/chrome/browser/system_flags.h"
#import "ios/chrome/browser/tabs/tab_title_util.h"
#import "ios/chrome/browser/tabs_search/tabs_search_service.h"
#import "ios/chrome/browser/tabs_search/tabs_search_service_factory.h"
#import "ios/chrome/browser/ui/commands/bookmark_add_command.h"
#import "ios/chrome/browser/ui/commands/bookmarks_commands.h"
#import "ios/chrome/browser/ui/commands/browser_commands.h"
#import "ios/chrome/browser/ui/commands/command_dispatcher.h"
#import "ios/chrome/browser/ui/commands/reading_list_add_command.h"
#import "ios/chrome/browser/ui/main/scene_state.h"
#import "ios/chrome/browser/ui/main/scene_state_browser_agent.h"
#import "ios/chrome/browser/ui/menu/action_factory.h"
#import "ios/chrome/browser/ui/tab_switcher/tab_grid/features.h"
#import "ios/chrome/browser/ui/tab_switcher/tab_grid/grid/grid_consumer.h"
#import "ios/chrome/browser/ui/tab_switcher/tab_grid/grid/grid_item.h"
#import "ios/chrome/browser/ui/tab_switcher/tab_switcher_item.h"
#import "ios/chrome/browser/ui/util/url_with_title.h"
#include "ios/chrome/browser/web_state_list/web_state_list.h"
#import "ios/chrome/browser/web_state_list/web_state_list_observer_bridge.h"
#import "ios/chrome/browser/web_state_list/web_state_list_serialization.h"
#include "ios/chrome/browser/web_state_list/web_state_opener.h"
#import "ios/web/public/navigation/navigation_manager.h"
#import "ios/web/public/web_state.h"
#import "ios/web/public/web_state_observer_bridge.h"
#import "net/base/mac/url_conversions.h"
#include "ui/gfx/image/image.h"

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

namespace {
// Constructs a TabSwitcherItem from a |web_state|.
TabSwitcherItem* CreateItem(web::WebState* web_state) {
  TabSwitcherItem* item = [[TabSwitcherItem alloc]
      initWithIdentifier:web_state->GetStableIdentifier()];
  // chrome://newtab (NTP) tabs have no title.
  if (IsURLNtp(web_state->GetVisibleURL())) {
    item.hidesTitle = YES;
  }
  item.title = tab_util::GetTabTitle(web_state);
  item.showsActivity = web_state->IsLoading();
  return item;
}

// Constructs an array of TabSwitcherItems from a |web_state_list|.
NSArray* CreateItems(WebStateList* web_state_list) {
  NSMutableArray* items = [[NSMutableArray alloc] init];
  for (int i = 0; i < web_state_list->count(); i++) {
    web::WebState* web_state = web_state_list->GetWebStateAt(i);
    [items addObject:CreateItem(web_state)];
  }
  return [items copy];
}

// Returns the ID of the active tab in |web_state_list|.
NSString* GetActiveTabId(WebStateList* web_state_list) {
  if (!web_state_list)
    return nil;

  web::WebState* web_state = web_state_list->GetActiveWebState();
  if (!web_state)
    return nil;
  return web_state->GetStableIdentifier();
}

void LogPriceDropMetrics(web::WebState* web_state) {
  ShoppingPersistedDataTabHelper* shopping_helper =
      ShoppingPersistedDataTabHelper::FromWebState(web_state);
  if (!shopping_helper)
    return;
  const ShoppingPersistedDataTabHelper::PriceDrop* price_drop =
      shopping_helper->GetPriceDrop();
  BOOL has_price_drop =
      price_drop && price_drop->current_price && price_drop->previous_price;
  base::RecordAction(base::UserMetricsAction(
      base::StringPrintf("Commerce.TabGridSwitched.%s",
                         has_price_drop ? "HasPriceDrop" : "NoPriceDrop")
          .c_str()));
}

// Returns the index of the tab with |identifier| in |web_state_list|. Returns
// WebStateList::kInvalidIndex if not found.
int GetIndexOfTabWithId(WebStateList* web_state_list, NSString* identifier) {
  for (int i = 0; i < web_state_list->count(); i++) {
    web::WebState* web_state = web_state_list->GetWebStateAt(i);
    if ([identifier isEqualToString:web_state->GetStableIdentifier()])
      return i;
  }
  return WebStateList::kInvalidIndex;
}

// Returns the WebState with |identifier| in |browser_state|. Returns |nullptr|
// if not found.
web::WebState* GetWebStateWithId(ChromeBrowserState* browser_state,
                                 NSString* identifier) {
  BrowserList* browser_list =
      BrowserListFactory::GetForBrowserState(browser_state);
  std::set<Browser*> browsers = browser_state->IsOffTheRecord()
                                    ? browser_list->AllIncognitoBrowsers()
                                    : browser_list->AllRegularBrowsers();
  for (Browser* browser : browsers) {
    WebStateList* web_state_list = browser->GetWebStateList();
    int index = GetIndexOfTabWithId(web_state_list, identifier);
    if (index != WebStateList::kInvalidIndex) {
      return web_state_list->GetWebStateAt(index);
    }
  }
  return nullptr;
}

// Returns the Browser with |identifier| in its WebStateList. Returns |nullptr|
// if not found.
Browser* GetBrowserForTabWithId(BrowserList* browser_list,
                                NSString* identifier,
                                bool is_otr_tab) {
  std::set<Browser*> browsers = is_otr_tab
                                    ? browser_list->AllIncognitoBrowsers()
                                    : browser_list->AllRegularBrowsers();
  for (Browser* browser : browsers) {
    WebStateList* webStateList = browser->GetWebStateList();
    int index = GetIndexOfTabWithId(webStateList, identifier);
    if (index != WebStateList::kInvalidIndex)
      return browser;
  }
  return nullptr;
}

}  // namespace

@interface TabGridMediator () <CRWWebStateObserver,
                               SnapshotCacheObserver,
                               WebStateListObserving>
// The list from the browser.
@property(nonatomic, assign) WebStateList* webStateList;
// The browser state from the browser.
@property(nonatomic, readonly) ChromeBrowserState* browserState;
// The UI consumer to which updates are made.
@property(nonatomic, weak) id<GridConsumer> consumer;
// Handler for reading list command.
@property(nonatomic, weak) id<BrowserCommands> readingListHandler;
// The saved session window just before close all tabs is called.
@property(nonatomic, strong) SessionWindowIOS* closedSessionWindow;
// The number of tabs in |closedSessionWindow| that are synced by
// TabRestoreService.
@property(nonatomic, assign) int syncedClosedTabsCount;
// Short-term cache for grid thumbnails.
@property(nonatomic, strong)
    NSMutableDictionary<NSString*, UIImage*>* appearanceCache;
@end

@implementation TabGridMediator {
  // Observers for WebStateList.
  std::unique_ptr<WebStateListObserverBridge> _webStateListObserverBridge;
  std::unique_ptr<
      base::ScopedMultiSourceObservation<WebStateList, WebStateListObserver>>
      _scopedWebStateListObservation;
  // Observer for WebStates.
  std::unique_ptr<web::WebStateObserverBridge> _webStateObserverBridge;
  std::unique_ptr<
      base::ScopedMultiSourceObservation<web::WebState, web::WebStateObserver>>
      _scopedWebStateObservation;
}

- (instancetype)initWithConsumer:(id<GridConsumer>)consumer {
  if (self = [super init]) {
    _consumer = consumer;
    _webStateListObserverBridge =
        std::make_unique<WebStateListObserverBridge>(self);
    _scopedWebStateListObservation = std::make_unique<
        base::ScopedMultiSourceObservation<WebStateList, WebStateListObserver>>(
        _webStateListObserverBridge.get());
    _webStateObserverBridge =
        std::make_unique<web::WebStateObserverBridge>(self);
    _scopedWebStateObservation =
        std::make_unique<base::ScopedMultiSourceObservation<
            web::WebState, web::WebStateObserver>>(
            _webStateObserverBridge.get());
    _appearanceCache = [[NSMutableDictionary alloc] init];
  }
  return self;
}

#pragma mark - Public properties

- (void)setBrowser:(Browser*)browser {
  [self.snapshotCache removeObserver:self];
  _scopedWebStateListObservation->RemoveAllObservations();
  _scopedWebStateObservation->RemoveAllObservations();
  _readingListHandler = nullptr;

  _browser = browser;

  _webStateList = browser ? browser->GetWebStateList() : nullptr;
  _browserState = browser ? browser->GetBrowserState() : nullptr;
  if (_browser) {
    // TODO(crbug.com/1045047): Use HandlerForProtocol after commands
    // protocol clean up.
    _readingListHandler =
        static_cast<id<BrowserCommands>>(_browser->GetCommandDispatcher());
  }
  [self.snapshotCache addObserver:self];

  if (_webStateList) {
    _scopedWebStateListObservation->AddObservation(_webStateList);
    for (int i = 0; i < self.webStateList->count(); i++) {
      web::WebState* webState = self.webStateList->GetWebStateAt(i);
      _scopedWebStateObservation->AddObservation(webState);
    }
    if (self.webStateList->count() > 0)
      [self populateConsumerItems];
  }
}

#pragma mark - WebStateListObserving

- (void)webStateList:(WebStateList*)webStateList
    didInsertWebState:(web::WebState*)webState
              atIndex:(int)index
           activating:(BOOL)activating {
  DCHECK_EQ(_webStateList, webStateList);
  if (webStateList->IsBatchInProgress())
    return;
  [self.consumer insertItem:CreateItem(webState)
                    atIndex:index
             selectedItemID:GetActiveTabId(webStateList)];
  _scopedWebStateObservation->AddObservation(webState);
}

- (void)webStateList:(WebStateList*)webStateList
     didMoveWebState:(web::WebState*)webState
           fromIndex:(int)fromIndex
             toIndex:(int)toIndex {
  DCHECK_EQ(_webStateList, webStateList);
  if (webStateList->IsBatchInProgress())
    return;
  [self.consumer moveItemWithID:webState->GetStableIdentifier()
                        toIndex:toIndex];
}

- (void)webStateList:(WebStateList*)webStateList
    didReplaceWebState:(web::WebState*)oldWebState
          withWebState:(web::WebState*)newWebState
               atIndex:(int)index {
  DCHECK_EQ(_webStateList, webStateList);
  if (webStateList->IsBatchInProgress())
    return;
  [self.consumer replaceItemID:oldWebState->GetStableIdentifier()
                      withItem:CreateItem(newWebState)];
  _scopedWebStateObservation->RemoveObservation(oldWebState);
  _scopedWebStateObservation->AddObservation(newWebState);
}

- (void)webStateList:(WebStateList*)webStateList
    didDetachWebState:(web::WebState*)webState
              atIndex:(int)index {
  DCHECK_EQ(_webStateList, webStateList);
  if (webStateList->IsBatchInProgress())
    return;
  if (!webStateList)
    return;
  [self.consumer removeItemWithID:webState->GetStableIdentifier()
                   selectedItemID:GetActiveTabId(webStateList)];
  _scopedWebStateObservation->RemoveObservation(webState);
}

- (void)webStateList:(WebStateList*)webStateList
    didChangeActiveWebState:(web::WebState*)newWebState
                oldWebState:(web::WebState*)oldWebState
                    atIndex:(int)atIndex
                     reason:(ActiveWebStateChangeReason)reason {
  DCHECK_EQ(_webStateList, webStateList);
  if (webStateList->IsBatchInProgress())
    return;
  // If the selected index changes as a result of the last webstate being
  // detached, atIndex will be kInvalidIndex.
  if (atIndex == WebStateList::kInvalidIndex) {
    [self.consumer selectItemWithID:nil];
    return;
  }

  [self.consumer selectItemWithID:newWebState->GetStableIdentifier()];
}

- (void)webStateListWillBeginBatchOperation:(WebStateList*)webStateList {
  DCHECK_EQ(_webStateList, webStateList);
  _scopedWebStateObservation->RemoveAllObservations();
}

- (void)webStateListBatchOperationEnded:(WebStateList*)webStateList {
  DCHECK_EQ(_webStateList, webStateList);
  for (int i = 0; i < self.webStateList->count(); i++) {
    web::WebState* webState = self.webStateList->GetWebStateAt(i);
    _scopedWebStateObservation->AddObservation(webState);
  }
  [self.consumer populateItems:CreateItems(self.webStateList)
                selectedItemID:GetActiveTabId(self.webStateList)];
}

#pragma mark - CRWWebStateObserver

- (void)webStateDidStartLoading:(web::WebState*)webState {
  [self updateConsumerItemForWebState:webState];
}

- (void)webStateDidStopLoading:(web::WebState*)webState {
  [self updateConsumerItemForWebState:webState];
}

- (void)webStateDidChangeTitle:(web::WebState*)webState {
  [self updateConsumerItemForWebState:webState];
}

- (void)updateConsumerItemForWebState:(web::WebState*)webState {
  [self.consumer replaceItemID:webState->GetStableIdentifier()
                      withItem:CreateItem(webState)];
}

#pragma mark - SnapshotCacheObserver

- (void)snapshotCache:(SnapshotCache*)snapshotCache
    didUpdateSnapshotForIdentifier:(NSString*)identifier {
  [self.appearanceCache removeObjectForKey:identifier];
  web::WebState* webState = GetWebStateWithId(self.browserState, identifier);
  if (webState) {
    // It is possible to observe an updated snapshot for a WebState before
    // observing that the WebState has been added to the WebStateList. It is the
    // consumer's responsibility to ignore any updates before inserts.
    [self.consumer replaceItemID:identifier withItem:CreateItem(webState)];
  }
}

#pragma mark - GridCommands

- (void)addNewItem {
  [self insertNewItemAtIndex:self.webStateList->count()];
}

- (void)insertNewItemAtIndex:(NSUInteger)index {
  [self insertNewItemAtIndex:index withURL:GURL(kChromeUINewTabURL)];
}

- (void)moveItemWithID:(NSString*)itemID toIndex:(NSUInteger)destinationIndex {
  int sourceIndex = GetIndexOfTabWithId(self.webStateList, itemID);
  if (sourceIndex != WebStateList::kInvalidIndex)
    self.webStateList->MoveWebStateAt(sourceIndex, destinationIndex);
}

- (void)selectItemWithID:(NSString*)itemID {
  int index = GetIndexOfTabWithId(self.webStateList, itemID);
  WebStateList* itemWebStateList = self.webStateList;
  if (index == WebStateList::kInvalidIndex) {
    if (IsTabsSearchEnabled()) {
      // If this is a search result, it may contain items from other windows -
      // check other windows first before giving up.
      BrowserList* browserList =
          BrowserListFactory::GetForBrowserState(self.browserState);
      Browser* browser = GetBrowserForTabWithId(
          browserList, itemID, self.browserState->IsOffTheRecord());

      if (!browser)
        return;

      itemWebStateList = browser->GetWebStateList();
      index = GetIndexOfTabWithId(itemWebStateList, itemID);
      SceneState* targetSceneState =
          SceneStateBrowserAgent::FromBrowser(browser)->GetSceneState();
      SceneState* currentSceneState =
          SceneStateBrowserAgent::FromBrowser(self.browser)->GetSceneState();

      UISceneActivationRequestOptions* options =
          [[UISceneActivationRequestOptions alloc] init];
      options.requestingScene = currentSceneState.scene;

      [[UIApplication sharedApplication]
          requestSceneSessionActivation:targetSceneState.scene.session
                           userActivity:nil
                                options:options
                           errorHandler:^(NSError* error) {
                             LOG(ERROR) << base::SysNSStringToUTF8(
                                 error.localizedDescription);
                             NOTREACHED();
                           }];
    } else {
      // Don't activate non-existent indexes.
      return;
    }
  }

  if (IsPriceAlertsEnabled())
    LogPriceDropMetrics(itemWebStateList->GetWebStateAt(index));

  // Don't attempt a no-op activation. Normally this is not an issue, but it's
  // possible that this method (-selectItemWithID:) is being called as part of
  // a WebStateListObserver callback, in which case even a no-op activation
  // will cause a CHECK().
  if (index == itemWebStateList->active_index())
    return;

  // Avoid a reentrant activation. This is a fix for crbug.com/1134663, although
  // ignoring the slection at this point may do weird things.
  if (itemWebStateList->IsMutating())
    return;

  // It should be safe to activate here.
  itemWebStateList->ActivateWebStateAt(index);
}

- (BOOL)isItemWithIDSelected:(NSString*)itemID {
  int index = GetIndexOfTabWithId(self.webStateList, itemID);
  if (index == WebStateList::kInvalidIndex)
    return NO;
  return index == self.webStateList->active_index();
}

- (void)closeItemWithID:(NSString*)itemID {
  WebStateList* itemWebStateList = self.webStateList;
  int index = GetIndexOfTabWithId(itemWebStateList, itemID);
  if (index == WebStateList::kInvalidIndex) {
    if (!IsTabsSearchEnabled())
      return;
    // If this is a search result, it may contain items from other windows -
    // check other windows first before giving up.
    BrowserList* browserList =
        BrowserListFactory::GetForBrowserState(self.browserState);
    Browser* browser = GetBrowserForTabWithId(
        browserList, itemID, self.browserState->IsOffTheRecord());
    if (!browser)
      return;
    itemWebStateList = browser->GetWebStateList();
    index = GetIndexOfTabWithId(itemWebStateList, itemID);
    // This item is not from the current browser therefore no UI updates will be
    // sent to the current grid. So notify the current grid consumer about the
    // change.
    [self.consumer removeItemWithID:itemID selectedItemID:nil];
    base::RecordAction(base::UserMetricsAction(
        "MobileTabGridSearchCloseTabFromAnotherWindow"));
  }
  itemWebStateList->CloseWebStateAt(index, WebStateList::CLOSE_USER_ACTION);
}

- (void)closeItemsWithIDs:(NSArray<NSString*>*)itemIDs {
  __block bool allTabsClosed = true;

  base::UmaHistogramCounts100("IOS.TabGrid.Selection.CloseTabs", itemIDs.count);

  self.webStateList->PerformBatchOperation(
      base::BindOnce(^(WebStateList* list) {
        for (NSString* itemID in itemIDs) {
          int index = GetIndexOfTabWithId(list, itemID);
          if (index != WebStateList::kInvalidIndex)
            list->CloseWebStateAt(index, WebStateList::CLOSE_USER_ACTION);
        }

        allTabsClosed = list->empty();
      }));

  if (allTabsClosed) {
    if (!self.browserState->IsOffTheRecord()) {
      base::RecordAction(base::UserMetricsAction(
          "MobileTabGridSelectionCloseAllRegularTabsConfirmed"));
    } else {
      base::RecordAction(base::UserMetricsAction(
          "MobileTabGridSelectionCloseAllIncognitoTabsConfirmed"));
    }
  }
}

- (void)closeAllItems {
  if (!self.browserState->IsOffTheRecord()) {
    base::RecordAction(
        base::UserMetricsAction("MobileTabGridCloseAllRegularTabs"));
  } else {
    base::RecordAction(
        base::UserMetricsAction("MobileTabGridCloseAllIncognitoTabs"));
  }
  // This is a no-op if |webStateList| is already empty.
  self.webStateList->CloseAllWebStates(WebStateList::CLOSE_USER_ACTION);
  SnapshotBrowserAgent::FromBrowser(self.browser)->RemoveAllSnapshots();
}

- (void)saveAndCloseAllItems {
  base::RecordAction(
      base::UserMetricsAction("MobileTabGridCloseAllRegularTabs"));
  if (self.webStateList->empty())
    return;
  self.closedSessionWindow = SerializeWebStateList(self.webStateList, nil);
  int old_size =
      self.tabRestoreService ? self.tabRestoreService->entries().size() : 0;
  self.webStateList->CloseAllWebStates(WebStateList::CLOSE_USER_ACTION);
  self.syncedClosedTabsCount =
      self.tabRestoreService
          ? self.tabRestoreService->entries().size() - old_size
          : 0;
}

- (void)undoCloseAllItems {
  base::RecordAction(
      base::UserMetricsAction("MobileTabGridUndoCloseAllRegularTabs"));
  if (!self.closedSessionWindow)
    return;
  SessionRestorationBrowserAgent::FromBrowser(self.browser)
      ->RestoreSessionWindow(self.closedSessionWindow);
  self.closedSessionWindow = nil;
  [self removeEntriesFromTabRestoreService];
  self.syncedClosedTabsCount = 0;
}

- (void)discardSavedClosedItems {
  if (!self.closedSessionWindow)
    return;
  self.syncedClosedTabsCount = 0;
  self.closedSessionWindow = nil;
  SnapshotBrowserAgent::FromBrowser(self.browser)->RemoveAllSnapshots();
}

- (void)
    showCloseItemsConfirmationActionSheetWithItems:(NSArray<NSString*>*)items
                                            anchor:
                                                (UIBarButtonItem*)buttonAnchor {
  [self.delegate dismissPopovers];

  [self.delegate
      showCloseItemsConfirmationActionSheetWithTabGridMediator:self
                                                         items:items
                                                        anchor:buttonAnchor];
}

- (void)shareItems:(NSArray<NSString*>*)items
            anchor:(UIBarButtonItem*)buttonAnchor {
  [self.delegate dismissPopovers];

  NSMutableArray<URLWithTitle*>* URLs = [[NSMutableArray alloc] init];
  for (NSString* itemIdentifier in items) {
    GridItem* item = [self gridItemForCellIdentifier:itemIdentifier];
    URLWithTitle* URL = [[URLWithTitle alloc] initWithURL:item.URL
                                                    title:item.title];
    [URLs addObject:URL];
  }
  base::RecordAction(
      base::UserMetricsAction("MobileTabGridSelectionShareTabs"));
  base::UmaHistogramCounts100("IOS.TabGrid.Selection.ShareTabs", items.count);
  [self.delegate tabGridMediator:self shareURLs:URLs anchor:buttonAnchor];
}

- (NSArray<UIMenuElement*>*)addToButtonMenuElementsForItems:
    (NSArray<NSString*>*)items {
  if (!self.browser) {
    return nil;
  }

  ActionFactory* actionFactory =
      [[ActionFactory alloc] initWithScenario:MenuScenario::kTabGridAddTo];

  __weak TabGridMediator* weakSelf = self;

  UIAction* bookmarkAction = [actionFactory actionToBookmarkWithBlock:^{
    [weakSelf addItemsToBookmarks:items];
  }];
  // Bookmarking can be disabled from prefs (from an enterprise policy),
  // if that's the case grey out the option in the menu.
  BOOL isEditBookmarksEnabled =
      self.browser->GetBrowserState()->GetPrefs()->GetBoolean(
          bookmarks::prefs::kEditBookmarksEnabled);
  if (!isEditBookmarksEnabled)
    bookmarkAction.attributes = UIMenuElementAttributesDisabled;

  return @[
    [actionFactory actionToAddToReadingListWithBlock:^{
      [weakSelf addItemsToReadingList:items];
    }],
    bookmarkAction
  ];
}

- (void)searchItemsWithText:(NSString*)searchText {
  TabsSearchService* searchService =
      TabsSearchServiceFactory::GetForBrowserState(self.browserState);
  const std::u16string& searchTerm = base::SysNSStringToUTF16(searchText);
  searchService->Search(
      searchTerm, base::BindOnce(^(std::vector<web::WebState*> results) {
        NSMutableArray* items = [[NSMutableArray alloc] init];
        for (web::WebState* webState : results) {
          [items addObject:CreateItem(webState)];
        }
        [self.consumer populateItems:items selectedItemID:nil];
      }));
}

- (void)resetToAllItems {
  [self populateConsumerItems];
}

- (void)fetchSearchHistoryResultsCountForText:(NSString*)searchText
                                   completion:(void (^)(size_t))completion {
  TabsSearchService* search_service =
      TabsSearchServiceFactory::GetForBrowserState(self.browserState);
  const std::u16string& searchTerm = base::SysNSStringToUTF16(searchText);
  search_service->SearchHistory(searchTerm,
                                base::BindOnce(^(size_t resultCount) {
                                  completion(resultCount);
                                }));
}

#pragma mark GridCommands helpers

- (void)insertNewItemAtIndex:(NSUInteger)index withURL:(const GURL&)newTabURL {
  // The incognito mediator's Browser is briefly set to nil after the last
  // incognito tab is closed.  This occurs because the incognito BrowserState
  // needs to be destroyed to correctly clear incognito browsing data.  Don't
  // attempt to create a new WebState with a nil BrowserState.
  if (!self.browser)
    return;

  DCHECK(self.browserState);
  web::WebState::CreateParams params(self.browserState);
  std::unique_ptr<web::WebState> webState = web::WebState::Create(params);

  web::NavigationManager::WebLoadParams loadParams(newTabURL);
  loadParams.transition_type = ui::PAGE_TRANSITION_TYPED;
  webState->GetNavigationManager()->LoadURLWithParams(loadParams);

  self.webStateList->InsertWebState(
      base::checked_cast<int>(index), std::move(webState),
      (WebStateList::INSERT_FORCE_INDEX | WebStateList::INSERT_ACTIVATE),
      WebStateOpener());
}

#pragma mark - GridDragDropHandler

- (UIDragItem*)dragItemForItemWithID:(NSString*)itemID {
  web::WebState* webState = GetWebStateWithId(self.browserState, itemID);
  return CreateTabDragItem(webState);
}

- (UIDropOperation)dropOperationForDropSession:(id<UIDropSession>)session {
  UIDragItem* dragItem = session.localDragSession.items.firstObject;

  // Tab move operations only originate from Chrome so a local object is used.
  // Local objects allow synchronous drops, whereas NSItemProvider only allows
  // asynchronous drops.
  if ([dragItem.localObject isKindOfClass:[TabInfo class]]) {
    TabInfo* tabInfo = static_cast<TabInfo*>(dragItem.localObject);
    if (self.browserState->IsOffTheRecord() && tabInfo.incognito) {
      return UIDropOperationMove;
    }
    if (!self.browserState->IsOffTheRecord() && !tabInfo.incognito) {
      return UIDropOperationMove;
    }
    // Tabs of different profiles (regular/incognito) cannot be dropped.
    return UIDropOperationForbidden;
  }

  // All URLs originating from Chrome create a new tab (as opposed to moving a
  // tab).
  if ([dragItem.localObject isKindOfClass:[NSURL class]]) {
    return UIDropOperationCopy;
  }

  // URLs are accepted when drags originate from outside Chrome.
  NSArray<NSString*>* acceptableTypes = @[ (__bridge NSString*)kUTTypeURL ];
  if ([session hasItemsConformingToTypeIdentifiers:acceptableTypes]) {
    return UIDropOperationCopy;
  }

  // Other UTI types such as image data or file data cannot be dropped.
  return UIDropOperationForbidden;
}

- (void)dropItem:(UIDragItem*)dragItem
               toIndex:(NSUInteger)destinationIndex
    fromSameCollection:(BOOL)fromSameCollection {
  // Tab move operations only originate from Chrome so a local object is used.
  // Local objects allow synchronous drops, whereas NSItemProvider only allows
  // asynchronous drops.
  if ([dragItem.localObject isKindOfClass:[TabInfo class]]) {
    TabInfo* tabInfo = static_cast<TabInfo*>(dragItem.localObject);
    if (!fromSameCollection) {
      // Move tab across Browsers.
      MoveTabToBrowser(tabInfo.tabID, self.browser, destinationIndex);
      return;
    }
    // Reorder tab within same grid.
    int sourceIndex = GetIndexOfTabWithId(self.webStateList, tabInfo.tabID);
    if (sourceIndex != WebStateList::kInvalidIndex)
      self.webStateList->MoveWebStateAt(sourceIndex, destinationIndex);
    return;
  }

  // Handle URLs from within Chrome synchronously using a local object.
  if ([dragItem.localObject isKindOfClass:[URLInfo class]]) {
    URLInfo* droppedURL = static_cast<URLInfo*>(dragItem.localObject);
    [self insertNewItemAtIndex:destinationIndex withURL:droppedURL.URL];
    return;
  }
}

- (void)dropItemFromProvider:(NSItemProvider*)itemProvider
                     toIndex:(NSUInteger)destinationIndex
          placeholderContext:
              (id<UICollectionViewDropPlaceholderContext>)placeholderContext {
  if (![itemProvider canLoadObjectOfClass:[NSURL class]]) {
    [placeholderContext deletePlaceholder];
    return;
  }

  auto loadHandler = ^(__kindof id<NSItemProviderReading> providedItem,
                       NSError* error) {
    dispatch_async(dispatch_get_main_queue(), ^{
      [placeholderContext deletePlaceholder];
      NSURL* droppedURL = static_cast<NSURL*>(providedItem);
      [self insertNewItemAtIndex:destinationIndex
                         withURL:net::GURLWithNSURL(droppedURL)];
    });
  };
  [itemProvider loadObjectOfClass:[NSURL class] completionHandler:loadHandler];
}

#pragma mark - GridImageDataSource

- (void)snapshotForIdentifier:(NSString*)identifier
                   completion:(void (^)(UIImage*))completion {
  if (self.appearanceCache[identifier]) {
    completion(self.appearanceCache[identifier]);
    return;
  }
  web::WebState* webState = GetWebStateWithId(self.browserState, identifier);
  if (webState) {
    SnapshotTabHelper::FromWebState(webState)->RetrieveColorSnapshot(
        ^(UIImage* image) {
          completion(image);
        });
  }
}

- (void)faviconForIdentifier:(NSString*)identifier
                  completion:(void (^)(UIImage*))completion {
  web::WebState* webState = GetWebStateWithId(self.browserState, identifier);
  if (!webState) {
    return;
  }
  // NTP tabs get no favicon.
  if (IsURLNtp(webState->GetVisibleURL())) {
    return;
  }
  UIImage* defaultFavicon =
      webState->GetBrowserState()->IsOffTheRecord()
          ? [UIImage imageNamed:@"default_world_favicon_incognito"]
          : [UIImage imageNamed:@"default_world_favicon_regular"];
  completion(defaultFavicon);

  favicon::FaviconDriver* faviconDriver =
      favicon::WebFaviconDriver::FromWebState(webState);
  if (faviconDriver) {
    gfx::Image favicon = faviconDriver->GetFavicon();
    if (!favicon.IsEmpty())
      completion(favicon.ToUIImage());
  }
}

- (void)preloadSnapshotsForVisibleGridSize:(int)gridSize {
  int startIndex = std::max(self.webStateList->active_index() - gridSize, 0);
  int endIndex = std::min(self.webStateList->active_index() + gridSize,
                          self.webStateList->count() - 1);
  for (int i = startIndex; i <= endIndex; i++) {
    web::WebState* web_state = self.webStateList->GetWebStateAt(i);
    NSString* identifier = web_state->GetStableIdentifier();
    auto cacheImage = ^(UIImage* image) {
      self.appearanceCache[identifier] = image;
    };
    [self snapshotForIdentifier:identifier completion:cacheImage];
  }
}

- (void)clearPreloadedSnapshots {
  [self.appearanceCache removeAllObjects];
}

#pragma mark - GridMenuActionsDataSource

- (GridItem*)gridItemForCellIdentifier:(NSString*)identifier {
  web::WebState* webState = GetWebStateWithId(self.browserState, identifier);

  if (!webState) {
    return nil;
  }

  GridItem* item =
      [[GridItem alloc] initWithTitle:tab_util::GetTabTitle(webState)
                                  url:webState->GetVisibleURL()];
  return item;
}

- (BOOL)isGridItemBookmarked:(GridItem*)item {
  bookmarks::BookmarkModel* bookmarkModel =
      ios::BookmarkModelFactory::GetForBrowserState(self.browserState);
  return item && bookmarkModel &&
         bookmarkModel->GetMostRecentlyAddedUserNodeForURL(item.URL);
}

#pragma mark - GridShareableItemsProvider

- (BOOL)isItemWithIdentifierSharable:(NSString*)identifier {
  web::WebState* webState = GetWebStateWithId(self.browserState, identifier);
  const GURL& URL = webState->GetVisibleURL();
  return URL.is_valid() && URL.SchemeIsHTTPOrHTTPS();
}

#pragma mark - Private

// Calls |-populateItems:selectedItemID:| on the consumer.
- (void)populateConsumerItems {
  [self.consumer populateItems:CreateItems(self.webStateList)
                selectedItemID:GetActiveTabId(self.webStateList)];
}

// Removes |self.syncedClosedTabsCount| most recent entries from the
// TabRestoreService.
- (void)removeEntriesFromTabRestoreService {
  if (!self.tabRestoreService) {
    return;
  }
  std::vector<SessionID> identifiers;
  auto iter = self.tabRestoreService->entries().begin();
  auto end = self.tabRestoreService->entries().end();
  for (int i = 0; i < self.syncedClosedTabsCount && iter != end; i++) {
    identifiers.push_back(iter->get()->id);
    iter++;
  }
  for (const SessionID sessionID : identifiers) {
    self.tabRestoreService->RemoveTabEntryById(sessionID);
  }
}

// Returns a SnapshotCache for the current browser.
- (SnapshotCache*)snapshotCache {
  if (!self.browser)
    return nil;
  return SnapshotBrowserAgent::FromBrowser(self.browser)->snapshot_cache();
}

- (void)addItemsToReadingList:(NSArray<NSString*>*)items {
  if (!_readingListHandler) {
    return;
  }
  [self.delegate dismissPopovers];

  base::UmaHistogramCounts100("IOS.TabGrid.Selection.AddToReadingList",
                              items.count);

  NSArray<URLWithTitle*>* URLs = [self urlsWithTitleFromItemIDs:items];

  ReadingListAddCommand* command =
      [[ReadingListAddCommand alloc] initWithURLs:URLs];
  [_readingListHandler addToReadingList:command];
}

- (void)addItemsToBookmarks:(NSArray<NSString*>*)items {
  id<BookmarksCommands> bookmarkHandler =
      HandlerForProtocol(_browser->GetCommandDispatcher(), BookmarksCommands);

  if (!bookmarkHandler) {
    return;
  }
  [self.delegate dismissPopovers];

  base::UmaHistogramCounts100("IOS.TabGrid.Selection.AddToBookmarks",
                              items.count);

  NSArray<URLWithTitle*>* URLs = [self urlsWithTitleFromItemIDs:items];

  BookmarkAddCommand* command = [[BookmarkAddCommand alloc] initWithURLs:URLs];
  [bookmarkHandler bookmark:command];
}

- (NSArray<URLWithTitle*>*)urlsWithTitleFromItemIDs:(NSArray<NSString*>*)items {
  NSMutableArray<URLWithTitle*>* URLs = [[NSMutableArray alloc] init];
  for (NSString* itemIdentifier in items) {
    GridItem* item = [self gridItemForCellIdentifier:itemIdentifier];
    URLWithTitle* URL = [[URLWithTitle alloc] initWithURL:item.URL
                                                    title:item.title];
    [URLs addObject:URL];
  }
  return URLs;
}

@end
