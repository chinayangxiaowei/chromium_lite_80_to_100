// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/chrome/browser/ui/table_view/cells/table_view_tabs_search_suggested_history_item.h"

#include "base/format_macros.h"
#include "base/strings/sys_string_conversions.h"
#include "ios/chrome/grit/ios_strings.h"
#include "ui/base/l10n/l10n_util_mac.h"

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

@implementation TableViewTabsSearchSuggestedHistoryItem

- (instancetype)initWithType:(NSInteger)type {
  self = [super initWithType:type];
  if (self) {
    self.cellClass = [TableViewTabsSearchSuggestedHistoryCell class];
    self.image = [[UIImage imageNamed:@"popup_menu_history"]
        imageWithRenderingMode:UIImageRenderingModeAlwaysTemplate];
    self.title = l10n_util::GetNSString(
        IDS_IOS_TABS_SEARCH_SUGGESTED_ACTION_SEARCH_HISTORY_UNKNOWN_RESULT_COUNT);
  }
  return self;
}

@end

@implementation TableViewTabsSearchSuggestedHistoryCell

- (void)updateHistoryResultsCount:(size_t)resultsCount {
  NSString* matchesStr = [NSString stringWithFormat:@"%" PRIuS, resultsCount];
  self.textLabel.text = l10n_util::GetNSStringF(
      IDS_IOS_TABS_SEARCH_SUGGESTED_ACTION_SEARCH_HISTORY,
      base::SysNSStringToUTF16(matchesStr));
}

@end
