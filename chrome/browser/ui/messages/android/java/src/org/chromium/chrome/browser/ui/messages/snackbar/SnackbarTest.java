// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.ui.messages.snackbar;

import android.app.Activity;
import android.os.Build;

import androidx.test.filters.MediumTest;
import androidx.test.filters.SmallTest;

import org.junit.Assert;
import org.junit.Before;
import org.junit.BeforeClass;
import org.junit.ClassRule;
import org.junit.Test;
import org.junit.runner.RunWith;

import org.chromium.base.supplier.Supplier;
import org.chromium.base.task.PostTask;
import org.chromium.base.test.BaseActivityTestRule;
import org.chromium.base.test.util.Batch;
import org.chromium.base.test.util.CriteriaHelper;
import org.chromium.base.test.util.DisableIf;
import org.chromium.chrome.browser.ui.messages.snackbar.SnackbarManager.SnackbarController;
import org.chromium.chrome.browser.util.ChromeAccessibilityUtil;
import org.chromium.chrome.test.ChromeJUnit4ClassRunner;
import org.chromium.content_public.browser.UiThreadTaskTraits;
import org.chromium.content_public.browser.test.util.TestThreadUtils;
import org.chromium.ui.test.util.BlankUiTestActivity;

import java.util.concurrent.TimeUnit;

/**
 * Tests for {@link SnackbarManager}.
 */
@RunWith(ChromeJUnit4ClassRunner.class)
@Batch(Batch.PER_CLASS)
public class SnackbarTest {
    private SnackbarManager mManager;
    private SnackbarController mDefaultController = new SnackbarController() {
        @Override
        public void onDismissNoAction(Object actionData) {}

        @Override
        public void onAction(Object actionData) {}
    };

    private SnackbarController mDismissController = new SnackbarController() {
        @Override
        public void onDismissNoAction(Object actionData) {
            mDismissed = true;
        }

        @Override
        public void onAction(Object actionData) {}
    };

    @ClassRule
    public static BaseActivityTestRule<BlankUiTestActivity> activityTestRule =
            new BaseActivityTestRule<>(BlankUiTestActivity.class);

    private static Activity sActivity;
    private boolean mDismissed;

    @BeforeClass
    public static void setupSuite() {
        activityTestRule.launchActivity(null);
        TestThreadUtils.runOnUiThreadBlocking(() -> {
            sActivity = activityTestRule.getActivity();
            SnackbarManager.setDurationForTesting(1000);
        });
    }

    @Before
    public void setupTest() {
        PostTask.runOrPostTask(UiThreadTaskTraits.DEFAULT, () -> {
            mManager = new SnackbarManager(
                    sActivity, sActivity.findViewById(android.R.id.content), null);
        });
    }

    @Test
    @MediumTest
    public void testStackQueuePersistentOrder() {
        final Snackbar stackbar = Snackbar.make(
                "stack", mDefaultController, Snackbar.TYPE_ACTION, Snackbar.UMA_TEST_SNACKBAR);
        final Snackbar queuebar = Snackbar.make("queue", mDefaultController,
                Snackbar.TYPE_NOTIFICATION, Snackbar.UMA_TEST_SNACKBAR);
        final Snackbar persistent = Snackbar.make("persistent", mDefaultController,
                Snackbar.TYPE_PERSISTENT, Snackbar.UMA_TEST_SNACKBAR);
        PostTask.runOrPostTask(UiThreadTaskTraits.DEFAULT, () -> mManager.showSnackbar(stackbar));
        pollSnackbarCondition("First snackbar not shown",
                () -> mManager.isShowing() && mManager.getCurrentSnackbarForTesting() == stackbar);
        PostTask.runOrPostTask(UiThreadTaskTraits.DEFAULT, () -> {
            mManager.showSnackbar(queuebar);
            Assert.assertTrue("Snackbar not showing", mManager.isShowing());
            Assert.assertEquals("Snackbars on stack should not be cancelled by snackbars on queue",
                    stackbar, mManager.getCurrentSnackbarForTesting());
        });
        PostTask.runOrPostTask(UiThreadTaskTraits.DEFAULT, () -> {
            mManager.showSnackbar(persistent);
            Assert.assertTrue("Snackbar not showing", mManager.isShowing());
            Assert.assertEquals(
                    "Snackbars on stack should not be cancelled by persistent snackbars", stackbar,
                    mManager.getCurrentSnackbarForTesting());
        });
        pollSnackbarCondition("Snackbar on queue not shown",
                () -> mManager.isShowing() && mManager.getCurrentSnackbarForTesting() == queuebar);
        pollSnackbarCondition("Snackbar on queue not timed out",
                ()
                        -> mManager.isShowing()
                        && mManager.getCurrentSnackbarForTesting() == persistent);
        PostTask.runOrPostTask(UiThreadTaskTraits.DEFAULT, () -> mManager.onClick(null));
        pollSnackbarCondition(
                "Persistent snackbar did not get cleared", () -> !mManager.isShowing());
    }

    @Test
    @SmallTest
    public void testPersistentQueueStackOrder() {
        final Snackbar stackbar = Snackbar.make(
                "stack", mDefaultController, Snackbar.TYPE_ACTION, Snackbar.UMA_TEST_SNACKBAR);
        final Snackbar queuebar = Snackbar.make("queue", mDefaultController,
                Snackbar.TYPE_NOTIFICATION, Snackbar.UMA_TEST_SNACKBAR);
        final Snackbar persistent = Snackbar.make("persistent", mDefaultController,
                Snackbar.TYPE_PERSISTENT, Snackbar.UMA_TEST_SNACKBAR);
        PostTask.runOrPostTask(UiThreadTaskTraits.DEFAULT, () -> mManager.showSnackbar(persistent));
        pollSnackbarCondition("First snackbar not shown",
                ()
                        -> mManager.isShowing()
                        && mManager.getCurrentSnackbarForTesting() == persistent);
        PostTask.runOrPostTask(UiThreadTaskTraits.DEFAULT, () -> mManager.showSnackbar(queuebar));
        pollSnackbarCondition("Persistent snackbar was not cleared by queue snackbar",
                () -> mManager.isShowing() && mManager.getCurrentSnackbarForTesting() == queuebar);
        PostTask.runOrPostTask(UiThreadTaskTraits.DEFAULT, () -> mManager.showSnackbar(stackbar));
        pollSnackbarCondition("Snackbar on queue was not cleared by snackbar stack.",
                () -> mManager.isShowing() && mManager.getCurrentSnackbarForTesting() == stackbar);
        pollSnackbarCondition("Snackbar did not time out",
                ()
                        -> mManager.isShowing()
                        && mManager.getCurrentSnackbarForTesting() == persistent);
        PostTask.runOrPostTask(UiThreadTaskTraits.DEFAULT, () -> mManager.onClick(null));
        pollSnackbarCondition(
                "Persistent snackbar did not get cleared", () -> !mManager.isShowing());
    }

    @Test
    @SmallTest
    public void testDismissSnackbar() {
        final Snackbar snackbar = Snackbar.make(
                "stack", mDismissController, Snackbar.TYPE_ACTION, Snackbar.UMA_TEST_SNACKBAR);
        mDismissed = false;
        PostTask.runOrPostTask(UiThreadTaskTraits.DEFAULT, () -> mManager.showSnackbar(snackbar));
        pollSnackbarCondition("Snackbar on queue was not cleared by snackbar stack.",
                () -> mManager.isShowing() && mManager.getCurrentSnackbarForTesting() == snackbar);
        PostTask.runOrPostTask(
                UiThreadTaskTraits.DEFAULT, () -> mManager.dismissSnackbars(mDismissController));
        pollSnackbarCondition(
                "Snackbar did not time out", () -> !mManager.isShowing() && mDismissed);
    }

    @Test
    @SmallTest
    public void testPersistentSnackbar() throws InterruptedException {
        int timeout = 100;
        SnackbarManager.setDurationForTesting(timeout);
        final Snackbar snackbar = Snackbar.make("persistent", mDismissController,
                Snackbar.TYPE_PERSISTENT, Snackbar.UMA_TEST_SNACKBAR);
        mDismissed = false;
        PostTask.runOrPostTask(UiThreadTaskTraits.DEFAULT, () -> mManager.showSnackbar(snackbar));
        pollSnackbarCondition("Persistent Snackbar not shown.",
                () -> mManager.isShowing() && mManager.getCurrentSnackbarForTesting() == snackbar);
        TimeUnit.MILLISECONDS.sleep(timeout);
        pollSnackbarCondition(
                "Persistent snackbar timed out.", () -> mManager.isShowing() && !mDismissed);
        PostTask.runOrPostTask(UiThreadTaskTraits.DEFAULT, () -> mManager.onClick(null));
        pollSnackbarCondition(
                "Persistent snackbar not removed on action.", () -> !mManager.isShowing());
    }

    @Test
    @SmallTest
    @DisableIf.Build(sdk_is_greater_than = Build.VERSION_CODES.P)
    public void testSnackbarDuration() {
        final Snackbar snackbar = Snackbar.make(
                "persistent", mDismissController, Snackbar.TYPE_ACTION, Snackbar.UMA_TEST_SNACKBAR);
        PostTask.runOrPostTask(UiThreadTaskTraits.DEFAULT, () -> {
            snackbar.setDuration(0);
            Assert.assertEquals(
                    "Snackbar should use default duration when client sets duration to 0.",
                    SnackbarManager.getDefaultDurationForTesting(), mManager.getDuration(snackbar));
        });

        PostTask.runOrPostTask(UiThreadTaskTraits.DEFAULT, () -> {
            snackbar.setDuration(SnackbarManager.getDefaultA11yDurationForTesting() / 3);
            ChromeAccessibilityUtil.get().setAccessibilityEnabledForTesting(true);
            Assert.assertEquals(
                    "Default a11y duration should be used if the duration of snackbar in non-a11y mode is less than half of it",
                    SnackbarManager.getDefaultA11yDurationForTesting(),
                    mManager.getDuration(snackbar));
        });
    }

    void pollSnackbarCondition(String message, Supplier<Boolean> condition) {
        CriteriaHelper.pollUiThread(condition::get, message);
    }
}
