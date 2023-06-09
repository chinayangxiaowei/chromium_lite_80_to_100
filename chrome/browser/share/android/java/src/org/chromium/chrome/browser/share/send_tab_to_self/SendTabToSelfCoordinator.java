// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.share.send_tab_to_self;

import android.content.Context;

import androidx.annotation.VisibleForTesting;

import org.chromium.components.browser_ui.bottomsheet.BottomSheetContent;
import org.chromium.components.browser_ui.bottomsheet.BottomSheetController;

/**
 * Coordinator for displaying the send tab to self feature.
 */
public class SendTabToSelfCoordinator {
    private static BottomSheetContent sBottomSheetContentForTesting;
    private final Context mContext;
    private final String mUrl;
    private final String mTitle;
    private final BottomSheetController mController;
    private final boolean mIsSyncEnabled;
    private final long mNavigationTime;

    public SendTabToSelfCoordinator(Context context, String url, String title,
            BottomSheetController controller, boolean isSyncEnabled, long navigationTime) {
        mContext = context;
        mUrl = url;
        mTitle = title;
        mController = controller;
        mIsSyncEnabled = isSyncEnabled;
        mNavigationTime = navigationTime;
    }

    public void show() {
        mController.requestShowContent(createBottomSheetContent(mContext, mUrl, mTitle,
                                               mNavigationTime, mController, mIsSyncEnabled),
                true);
    }

    public static BottomSheetContent createBottomSheetContent(Context context, String url,
            String title, long navigationTime, BottomSheetController controller,
            boolean isSyncEnabled) {
        if (sBottomSheetContentForTesting != null) {
            return sBottomSheetContentForTesting;
        }
        return new DevicePickerBottomSheetContent(
                context, url, title, navigationTime, controller, isSyncEnabled);
    }

    @VisibleForTesting
    public static void setBottomSheetContentForTesting(BottomSheetContent bottomSheetContent) {
        sBottomSheetContentForTesting = bottomSheetContent;
    }
}
