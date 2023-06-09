// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.layouts;

import org.chromium.base.ThreadUtils;
import org.chromium.base.test.util.CallbackHelper;
import org.chromium.chrome.browser.layouts.LayoutStateProvider.LayoutStateObserver;
import org.chromium.content_public.browser.test.util.TestThreadUtils;

import java.util.concurrent.TimeoutException;

/** Utilities to aid in testing that involves switching between layouts. */
public class LayoutTestUtils {
    /**
     * Wait for a specified layout to be shown. If the layout is already showing, this returns
     * immediately.
     * @param layoutManager The {@link LayoutManager} showing the layout.
     * @param type The type of layout to wait for.
     */
    public static void waitForLayout(LayoutManager layoutManager, @LayoutType int type) {
        CallbackHelper finishedShowingCallbackHelper = new CallbackHelper();
        LayoutStateObserver observer = new LayoutStateObserver() {
            @Override
            public void onFinishedShowing(int layoutType) {
                finishedShowingCallbackHelper.notifyCalled();
            }
        };
        TestThreadUtils.runOnUiThreadBlocking(() -> {
            if (layoutManager.isLayoutVisible(type)) {
                finishedShowingCallbackHelper.notifyCalled();
            } else {
                layoutManager.addObserver(observer);
            }
        });

        try {
            finishedShowingCallbackHelper.waitForFirst();
        } catch (TimeoutException e) {
            assert false : "Timed out waiting for layout (@LayoutType " + type + ") to show!";
        }
        TestThreadUtils.runOnUiThreadBlocking(() -> layoutManager.removeObserver(observer));
    }

    /**
     * Start showing a layout and wait for it to finish showing.
     * @param layoutManager A layout manager to show different layouts.
     * @param type The type of layout to show.
     * @param animate Whether to animate the transition.
     */
    public static void startShowingAndWaitForLayout(
            LayoutManager layoutManager, @LayoutType int type, boolean animate) {
        ThreadUtils.runOnUiThreadBlocking(() -> layoutManager.showLayout(type, animate));
        waitForLayout(layoutManager, type);
    }
}
