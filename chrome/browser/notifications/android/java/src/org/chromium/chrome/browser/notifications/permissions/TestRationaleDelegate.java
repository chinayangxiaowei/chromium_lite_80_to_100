// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.notifications.permissions;

import org.chromium.base.Callback;
import org.chromium.chrome.browser.notifications.permissions.NotificationPermissionController.RationaleDelegate;
import org.chromium.ui.modaldialog.DialogDismissalCause;

/**
 * Test implementation of {@link RationaleDelegate}.
 */
class TestRationaleDelegate implements RationaleDelegate {
    private Integer mDialogAction;

    @Override
    public void showRationaleUi(Callback<Boolean> callback) {
        assert mDialogAction != null;
        if (mDialogAction == DialogDismissalCause.POSITIVE_BUTTON_CLICKED) {
            callback.onResult(true);
        } else {
            callback.onResult(false);
        }
    }

    /** Called by tests to set the user action to be taken when the dialog shows up.*/
    void setDialogAction(Integer accept) {
        mDialogAction = accept;
    }
}
