// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.feed;

import android.animation.ObjectAnimator;
import android.animation.PropertyValuesHolder;
import android.app.Activity;
import android.util.TypedValue;
import android.view.View;
import android.view.ViewGroup;
import android.view.ViewParent;
import android.widget.FrameLayout;

import androidx.annotation.Nullable;
import androidx.annotation.VisibleForTesting;
import androidx.recyclerview.widget.LinearLayoutManager;
import androidx.recyclerview.widget.RecyclerView;

import org.chromium.base.Callback;
import org.chromium.base.Function;
import org.chromium.base.Log;
import org.chromium.base.ObserverList;
import org.chromium.base.ThreadUtils;
import org.chromium.base.annotations.CalledByNative;
import org.chromium.base.annotations.JNINamespace;
import org.chromium.base.annotations.NativeMethods;
import org.chromium.base.supplier.ObservableSupplier;
import org.chromium.base.supplier.ObservableSupplierImpl;
import org.chromium.base.supplier.Supplier;
import org.chromium.base.task.PostTask;
import org.chromium.chrome.browser.feed.v2.FeedUserActionType;
import org.chromium.chrome.browser.feedback.HelpAndFeedbackLauncher;
import org.chromium.chrome.browser.flags.ChromeFeatureList;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.chrome.browser.share.ChromeShareExtras;
import org.chromium.chrome.browser.share.ShareDelegate;
import org.chromium.chrome.browser.ui.messages.snackbar.Snackbar;
import org.chromium.chrome.browser.ui.messages.snackbar.SnackbarManager;
import org.chromium.chrome.browser.xsurface.FeedActionsHandler;
import org.chromium.chrome.browser.xsurface.FeedLaunchReliabilityLogger;
import org.chromium.chrome.browser.xsurface.FeedLaunchReliabilityLogger.StreamType;
import org.chromium.chrome.browser.xsurface.HybridListRenderer;
import org.chromium.chrome.browser.xsurface.LoggingParameters;
import org.chromium.chrome.browser.xsurface.SurfaceActionsHandler;
import org.chromium.chrome.browser.xsurface.SurfaceScope;
import org.chromium.components.browser_ui.bottomsheet.BottomSheetContent;
import org.chromium.components.browser_ui.bottomsheet.BottomSheetController;
import org.chromium.components.browser_ui.bottomsheet.BottomSheetController.StateChangeReason;
import org.chromium.components.browser_ui.bottomsheet.EmptyBottomSheetObserver;
import org.chromium.components.browser_ui.share.ShareParams;
import org.chromium.components.browser_ui.widget.animation.Interpolators;
import org.chromium.components.feed.proto.FeedUiProto;
import org.chromium.components.feed.proto.wire.ReliabilityLoggingEnums.DiscoverLaunchResult;
import org.chromium.content_public.browser.LoadUrlParams;
import org.chromium.content_public.browser.UiThreadTaskTraits;
import org.chromium.ui.base.PageTransition;
import org.chromium.ui.base.WindowAndroid;
import org.chromium.ui.display.DisplayAndroid;
import org.chromium.ui.mojom.WindowOpenDisposition;
import org.chromium.url.GURL;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.Map;

/**
 * A implementation of a Feed {@link Stream} that is just able to render a vertical stream of
 * cards for Feed v2.
 */
@JNINamespace("feed::android")
public class FeedStream implements Stream {
    private static final String TAG = "FeedStream";

    Function<String, GURL> mMakeGURL = url -> new GURL(url);

    /**
     * Implementation of SurfaceActionsHandler methods.
     */
    @VisibleForTesting
    class FeedSurfaceActionsHandler implements SurfaceActionsHandler {
        FeedActionDelegate mActionDelegate;
        FeedSurfaceActionsHandler(FeedActionDelegate actionDelegate) {
            mActionDelegate = actionDelegate;
        }

        @Override
        public void navigateTab(String url, View actionSourceView) {
            assert ThreadUtils.runningOnUiThread();
            FeedStreamJni.get().reportOpenAction(mNativeFeedStream, FeedStream.this,
                    mMakeGURL.apply(url), getSliceIdFromView(actionSourceView));

            openSuggestionUrl(url, WindowOpenDisposition.CURRENT_TAB);

            // Attempts to load more content if needed.
            maybeLoadMore();
        }

        @Override
        public void navigateNewTab(String url, View actionSourceView) {
            assert ThreadUtils.runningOnUiThread();
            FeedStreamJni.get().reportOpenInNewTabAction(mNativeFeedStream, FeedStream.this,
                    mMakeGURL.apply(url), getSliceIdFromView(actionSourceView));

            openSuggestionUrl(url, WindowOpenDisposition.NEW_BACKGROUND_TAB);

            // Attempts to load more content if needed.
            maybeLoadMore();
        }

        @Override
        public void navigateIncognitoTab(String url) {
            assert ThreadUtils.runningOnUiThread();
            FeedStreamJni.get().reportOtherUserAction(mNativeFeedStream, FeedStream.this,
                    FeedUserActionType.TAPPED_OPEN_IN_NEW_INCOGNITO_TAB);

            openSuggestionUrl(url, WindowOpenDisposition.OFF_THE_RECORD);

            // Attempts to load more content if needed.
            maybeLoadMore();
        }

        @Override
        public void downloadLink(String url) {
            assert ThreadUtils.runningOnUiThread();
            FeedStreamJni.get().reportOtherUserAction(
                    mNativeFeedStream, FeedStream.this, FeedUserActionType.TAPPED_DOWNLOAD);
            mActionDelegate.downloadPage(url);
        }

        @Override
        public void addToReadingList(String title, String url) {
            assert ThreadUtils.runningOnUiThread();
            FeedStreamJni.get().reportOtherUserAction(mNativeFeedStream, FeedStream.this,
                    FeedUserActionType.TAPPED_ADD_TO_READING_LIST);
            mActionDelegate.addToReadingList(title, url);
        }

        @Override
        public void showBottomSheet(View view, View actionSourceView) {
            assert ThreadUtils.runningOnUiThread();
            dismissBottomSheet();

            FeedStreamJni.get().reportOtherUserAction(
                    mNativeFeedStream, FeedStream.this, FeedUserActionType.OPENED_CONTEXT_MENU);

            // Remember the currently focused view so that we can get back to it once the bottom
            // sheet is closed. This is to fix the problem that the last focused view is not
            // restored after opening and closing the bottom sheet.
            mLastFocusedView = mActivity.getCurrentFocus();

            // Make a sheetContent with the view.
            mBottomSheetContent = new CardMenuBottomSheetContent(view);
            mBottomSheetOriginatingSliceId = getSliceIdFromView(actionSourceView);
            mBottomSheetController.addObserver(new EmptyBottomSheetObserver() {
                @Override
                public void onSheetClosed(@StateChangeReason int reason) {
                    if (mLastFocusedView == null) return;
                    mLastFocusedView.requestFocus();
                    mLastFocusedView = null;
                }
            });
            mBottomSheetController.requestShowContent(mBottomSheetContent, true);
        }

        @Override
        public void dismissBottomSheet() {
            FeedStream.this.dismissBottomSheet();
        }

        private void openSuggestionUrl(String url, int disposition) {
            boolean inNewTab = (disposition == WindowOpenDisposition.NEW_BACKGROUND_TAB
                    || disposition == WindowOpenDisposition.OFF_THE_RECORD);

            if (disposition != WindowOpenDisposition.NEW_BACKGROUND_TAB
                    && mLaunchReliabilityLogger != null
                    && mLaunchReliabilityLogger.isLaunchInProgress()) {
                mLaunchReliabilityLogger.logLaunchFinished(
                        System.nanoTime(), DiscoverLaunchResult.CARD_TAPPED.getNumber());
            }
            // This postTask is necessary so that other click-handlers have a chance
            // to run before we begin navigating. On start surface, navigation immediately
            // triggers unbind, which can break event handling.
            PostTask.postTask(UiThreadTaskTraits.DEFAULT, () -> {
                mActionDelegate.openSuggestionUrl(disposition,
                        new LoadUrlParams(url, PageTransition.AUTO_BOOKMARK),
                        ()
                                -> FeedStreamJni.get().reportPageLoaded(
                                        mNativeFeedStream, FeedStream.this, inNewTab),
                        visitResult
                        -> FeedServiceBridge.reportOpenVisitComplete(visitResult.visitTimeMs));
            });
        }
    }

    /**
     * Implementation of FeedActionsHandler methods.
     */
    class FeedActionsHandlerImpl implements FeedActionsHandler {
        private static final int SNACKBAR_DURATION_MS_SHORT = 4000;
        private static final int SNACKBAR_DURATION_MS_LONG = 10000;

        @VisibleForTesting
        static final String FEEDBACK_REPORT_TYPE =
                "com.google.chrome.feed.USER_INITIATED_FEEDBACK_REPORT";
        @VisibleForTesting
        static final String XSURFACE_CARD_URL = "Card URL";

        @Override
        public void processThereAndBackAgainData(byte[] data, LoggingParameters loggingParameters) {
            assert ThreadUtils.runningOnUiThread();
            // TODO(crbug.com/1268575): Forward loggingParameters to FeedApi, and check that they
            // match the current state.
            FeedStreamJni.get().processThereAndBackAgain(mNativeFeedStream, FeedStream.this, data,
                    FeedLoggingParameters.convertToProto(loggingParameters).toByteArray());
        }

        @Override
        public void sendFeedback(Map<String, String> productSpecificDataMap) {
            assert ThreadUtils.runningOnUiThread();
            FeedStreamJni.get().reportOtherUserAction(
                    mNativeFeedStream, FeedStream.this, FeedUserActionType.TAPPED_SEND_FEEDBACK);

            // Make sure the bottom sheet is dismissed before we take a snapshot.
            dismissBottomSheet();

            Profile profile = Profile.getLastUsedRegularProfile();
            if (profile == null) {
                return;
            }

            String url = productSpecificDataMap.get(XSURFACE_CARD_URL);

            Map<String, String> feedContext = convertNameFormat(productSpecificDataMap);

            // FEEDBACK_REPORT_TYPE: Reports for Chrome mobile must have a contextTag of the form
            // com.chrome.feed.USER_INITIATED_FEEDBACK_REPORT, or they will be discarded for not
            // matching an allow list rule.
            mHelpAndFeedbackLauncher.showFeedback(
                    mActivity, profile, url, FEEDBACK_REPORT_TYPE, feedContext);
        }

        @Override
        public int requestDismissal(byte[] data) {
            assert ThreadUtils.runningOnUiThread();
            return FeedStreamJni.get().executeEphemeralChange(
                    mNativeFeedStream, FeedStream.this, data);
        }

        @Override
        public void commitDismissal(int changeId) {
            assert ThreadUtils.runningOnUiThread();
            FeedStreamJni.get().commitEphemeralChange(mNativeFeedStream, FeedStream.this, changeId);

            // Attempts to load more content if needed.
            maybeLoadMore();
        }

        @Override
        public void discardDismissal(int changeId) {
            assert ThreadUtils.runningOnUiThread();
            FeedStreamJni.get().discardEphemeralChange(
                    mNativeFeedStream, FeedStream.this, changeId);
        }

        @Override
        public void showSnackbar(String text, String actionLabel, SnackbarDuration duration,
                SnackbarController delegateController) {
            assert ThreadUtils.runningOnUiThread();
            int durationMs = SNACKBAR_DURATION_MS_SHORT;
            if (duration == FeedActionsHandler.SnackbarDuration.LONG) {
                durationMs = SNACKBAR_DURATION_MS_LONG;
            }
            SnackbarManager.SnackbarController controller =
                    new SnackbarManager.SnackbarController() {
                        @Override
                        public void onAction(Object actionData) {
                            delegateController.onAction();
                        }
                        @Override
                        public void onDismissNoAction(Object actionData) {
                            delegateController.onDismissNoAction();
                        }
                    };

            mSnackbarControllers.add(controller);
            mSnackManager.showSnackbar(Snackbar.make(text, controller, Snackbar.TYPE_ACTION,
                                                       Snackbar.UMA_FEED_NTP_STREAM)
                                               .setAction(actionLabel, /*actionData=*/null)
                                               .setDuration(durationMs));
        }

        @Override
        public void share(String url, String title) {
            assert ThreadUtils.runningOnUiThread();
            mShareHelper.share(url, title);
            FeedStreamJni.get().reportOtherUserAction(
                    mNativeFeedStream, FeedStream.this, FeedUserActionType.SHARE);
        }

        @Override
        public void openAutoplaySettings() {
            assert ThreadUtils.runningOnUiThread();
            FeedStreamJni.get().reportOtherUserAction(mNativeFeedStream, FeedStream.this,
                    FeedUserActionType.OPENED_AUTOPLAY_SETTINGS);
            mFeedAutoplaySettingsDelegate.launchAutoplaySettings();
        }

        @Override
        public void watchForViewFirstVisible(View view, float viewedThreshold, Runnable runnable) {
            assert ThreadUtils.runningOnUiThread();
            mSliceViewTracker.watchForFirstVisible(
                    getSliceIdFromView(view), viewedThreshold, runnable);
        }

        @Override
        public void reportNoticeCreated(String key) {
            assert ThreadUtils.runningOnUiThread();
            FeedStreamJni.get().reportNoticeCreated(mNativeFeedStream, FeedStream.this, key);
        }

        @Override
        public void reportNoticeViewed(String key) {
            assert ThreadUtils.runningOnUiThread();
            FeedStreamJni.get().reportNoticeViewed(mNativeFeedStream, FeedStream.this, key);
        }

        @Override
        public void reportNoticeOpenAction(String key) {
            assert ThreadUtils.runningOnUiThread();
            FeedStreamJni.get().reportNoticeOpenAction(mNativeFeedStream, FeedStream.this, key);
        }

        @Override
        public void reportNoticeDismissed(String key) {
            assert ThreadUtils.runningOnUiThread();
            FeedStreamJni.get().reportNoticeDismissed(mNativeFeedStream, FeedStream.this, key);
        }

        // Since the XSurface client strings are slightly different than the Feed strings, convert
        // the name from the XSurface format to the format that can be handled by the feedback
        // system.  Any new strings that are added on the XSurface side will need a code change
        // here, and adding the PSD to the allow list.
        private Map<String, String> convertNameFormat(Map<String, String> xSurfaceMap) {
            Map<String, String> feedbackNameConversionMap = new HashMap<>();
            feedbackNameConversionMap.put("Card URL", "CardUrl");
            feedbackNameConversionMap.put("Card Title", "CardTitle");
            feedbackNameConversionMap.put("Card Snippet", "CardSnippet");
            feedbackNameConversionMap.put("Card category", "CardCategory");
            feedbackNameConversionMap.put("Doc Creation Date", "DocCreationDate");

            // For each <name, value> entry in the input map, convert the name to the new name, and
            // write the new <name, value> pair into the output map.
            Map<String, String> feedbackMap = new HashMap<>();
            for (Map.Entry<String, String> entry : xSurfaceMap.entrySet()) {
                String newName = feedbackNameConversionMap.get(entry.getKey());
                if (newName != null) {
                    feedbackMap.put(newName, entry.getValue());
                } else {
                    Log.v(TAG, "Found an entry with no conversion available.");
                    // We will put the entry into the map if untranslatable. It will be discarded
                    // unless it matches an allow list on the server, though. This way we can choose
                    // to allow it on the server if desired.
                    feedbackMap.put(entry.getKey(), entry.getValue());
                }
            }

            return feedbackMap;
        }
    }

    private class RotationObserver implements DisplayAndroid.DisplayAndroidObserver {
        /**
         * If the device rotates, we dismiss the bottom sheet to avoid a bad interaction
         * between the XSurface client and the chrome bottom sheet.
         *
         * @param rotation One of Surface.ROTATION_* values.
         */
        @Override
        public void onRotationChanged(int rotation) {
            dismissBottomSheet();
        }
    }

    // How far the user has to scroll down in DP before attempting to load more content.
    private final int mLoadMoreTriggerScrollDistanceDp;

    private final Activity mActivity;
    private final long mNativeFeedStream;
    private final ObserverList<ContentChangedListener> mContentChangedListeners =
            new ObserverList<>();
    private final int mStreamKind;
    // Various helpers/controllers.
    private ShareHelperWrapper mShareHelper;
    private SnackbarManager mSnackManager;
    private HelpAndFeedbackLauncher mHelpAndFeedbackLauncher;
    private WindowAndroid mWindowAndroid;
    private final FeedAutoplaySettingsDelegate mFeedAutoplaySettingsDelegate;
    private UnreadContentObserver mUnreadContentObserver;

    // For loading more content.
    private int mAccumulatedDySinceLastLoadMore;
    private int mLoadMoreTriggerLookahead;
    private boolean mIsLoadingMoreContent;

    // Things attached on bind.
    private RestoreScrollObserver mRestoreScrollObserver = new RestoreScrollObserver();
    private RecyclerView.OnScrollListener mMainScrollListener;
    private FeedSliceViewTracker mSliceViewTracker;
    private ScrollReporter mScrollReporter;
    private final Map<String, Object> mHandlersMap;
    private RotationObserver mRotationObserver;
    private FeedReliabilityLoggingBridge mReliabilityLoggingBridge;
    private FeedLaunchReliabilityLogger mLaunchReliabilityLogger;

    // Things valid only when bound.
    private @Nullable RecyclerView mRecyclerView;
    private @Nullable NtpListContentManager mContentManager;
    private @Nullable SurfaceScope mSurfaceScope;
    private @Nullable HybridListRenderer mRenderer;
    private FeedScrollState mScrollStateToRestore;
    private int mHeaderCount;
    private boolean mIsPlaceholderShown;
    private long mLastFetchTimeMs;
    private ArrayList<SnackbarManager.SnackbarController> mSnackbarControllers = new ArrayList<>();

    // Placeholder view that simply takes up space.
    private NtpListContentManager.NativeViewContent mSpacerViewContent;

    // Bottomsheet.
    private final BottomSheetController mBottomSheetController;
    private BottomSheetContent mBottomSheetContent;
    private String mBottomSheetOriginatingSliceId;
    private View mLastFocusedView;

    /**
     * Creates a new Feed Stream.
     * @param activity {@link Activity} that this is bound to.
     * @param snackbarManager {@link SnackbarManager} for showing snackbars.
     * @param bottomSheetController {@link BottomSheetController} for menus.
     * @param isPlaceholderShown Whether the placeholder is shown initially.
     * @param windowAndroid The {@link WindowAndroid} this is shown on.
     * @param shareDelegateSupplier The supplier for {@link ShareDelegate} for sharing actions.
     * @param streamKind Kind of stream data this feed stream serves.
     * @param feedAutoplaySettingsDelegate The delegate to invoke autoplay settings.
     * @param actionDelegate Implements some Feed actions.
     * @param helpAndFeedbackLauncher A HelpAndFeedbackLauncher.
     */
    public FeedStream(Activity activity, SnackbarManager snackbarManager,
            BottomSheetController bottomSheetController, boolean isPlaceholderShown,
            WindowAndroid windowAndroid, Supplier<ShareDelegate> shareDelegateSupplier,
            int streamKind, FeedAutoplaySettingsDelegate feedAutoplaySettingsDelegate,
            FeedActionDelegate actionDelegate, HelpAndFeedbackLauncher helpAndFeedbackLauncher) {
        this.mActivity = activity;
        mStreamKind = streamKind;
        mReliabilityLoggingBridge = new FeedReliabilityLoggingBridge();
        mNativeFeedStream = FeedStreamJni.get().init(
                this, streamKind, mReliabilityLoggingBridge.getNativePtr());

        mBottomSheetController = bottomSheetController;
        mShareHelper = new ShareHelperWrapper(windowAndroid, shareDelegateSupplier);
        mSnackManager = snackbarManager;
        mHelpAndFeedbackLauncher = helpAndFeedbackLauncher;
        mIsPlaceholderShown = isPlaceholderShown;
        mWindowAndroid = windowAndroid;
        mFeedAutoplaySettingsDelegate = feedAutoplaySettingsDelegate;
        mRotationObserver = new RotationObserver();

        mHandlersMap = new HashMap<>();
        mHandlersMap.put(SurfaceActionsHandler.KEY, new FeedSurfaceActionsHandler(actionDelegate));
        mHandlersMap.put(FeedActionsHandler.KEY, new FeedActionsHandlerImpl());

        this.mLoadMoreTriggerScrollDistanceDp =
                FeedServiceBridge.getLoadMoreTriggerScrollDistanceDp();

        addOnContentChangedListener(contents -> {
            // Feed's background is set to be transparent in {@link #bind} to show the Feed
            // placeholder. When first batch of articles are about to show, set recyclerView back
            // to non-transparent.
            if (isPlaceholderShown()) {
                hidePlaceholder();
            }
        });
        mScrollReporter = new ScrollReporter();

        mLoadMoreTriggerLookahead = FeedServiceBridge.getLoadMoreTriggerLookahead();

        mMainScrollListener = new RecyclerView.OnScrollListener() {
            @Override
            public void onScrolled(RecyclerView v, int dx, int dy) {
                super.onScrolled(v, dx, dy);
                checkScrollingForLoadMore(dy);
                FeedStreamJni.get().reportStreamScrollStart(mNativeFeedStream, FeedStream.this);
                mScrollReporter.trackScroll(dx, dy);
            }
        };

        // Only watch for unread content on the web feed, not for-you feed.
        // Sort options only available for web feed right now.
        if (streamKind == StreamKind.FOLLOWING) {
            mUnreadContentObserver = new UnreadContentObserver(/*isWebFeed=*/true);
        }
    }

    @Override
    public boolean supportsOptions() {
        return ChromeFeatureList.isEnabled(ChromeFeatureList.WEB_FEED_SORT)
                && mStreamKind == StreamKind.FOLLOWING;
    }

    @Override
    public void destroy() {
        if (mUnreadContentObserver != null) {
            mUnreadContentObserver.destroy();
        }
        mReliabilityLoggingBridge.destroy();
    }

    @Override
    @StreamKind
    public int getStreamKind() {
        return mStreamKind;
    }

    @Override
    public String getContentState() {
        return String.valueOf(mLastFetchTimeMs);
    }

    @Override
    public void bind(RecyclerView rootView, NtpListContentManager manager,
            FeedScrollState savedInstanceState, SurfaceScope surfaceScope,
            HybridListRenderer renderer, FeedLaunchReliabilityLogger launchReliabilityLogger,
            int headerCount) {
        mLaunchReliabilityLogger = launchReliabilityLogger;
        launchReliabilityLogger.sendPendingEvents(getStreamType(),
                FeedStreamJni.get().getSurfaceId(mNativeFeedStream, FeedStream.this));
        launchReliabilityLogger.logFeedReloading(System.nanoTime());
        mReliabilityLoggingBridge.setLogger(launchReliabilityLogger);

        mScrollStateToRestore = savedInstanceState;
        manager.setHandlers(mHandlersMap);
        mSliceViewTracker =
                new FeedSliceViewTracker(rootView, manager, new FeedStream.ViewTrackerObserver());
        mSliceViewTracker.bind();

        rootView.addOnScrollListener(mMainScrollListener);
        rootView.getAdapter().registerAdapterDataObserver(mRestoreScrollObserver);
        mRecyclerView = rootView;
        mContentManager = manager;
        mSurfaceScope = surfaceScope;
        mRenderer = renderer;
        mHeaderCount = headerCount;
        if (mWindowAndroid.getDisplay() != null) {
            mWindowAndroid.getDisplay().addObserver(mRotationObserver);
        }

        if (isPlaceholderShown()) {
            // Set recyclerView as transparent until first batch of articles are loaded. Before
            // that, the placeholder is shown.
            mRecyclerView.getBackground().setAlpha(0);
        }

        FeedStreamJni.get().surfaceOpened(mNativeFeedStream, FeedStream.this);
    }

    @Override
    public void restoreSavedInstanceState(FeedScrollState scrollState) {
        if (!restoreScrollState(scrollState)) {
            mScrollStateToRestore = scrollState;
        }
    }

    @Override
    public void unbind(boolean shouldPlaceSpacer) {
        // Dismiss any snackbars. It's important we do this now so that xsurface can respond to
        // these events before the content is removed.
        for (SnackbarManager.SnackbarController controller : mSnackbarControllers) {
            mSnackManager.dismissSnackbars(controller);
        }
        mSnackbarControllers.clear();

        mSliceViewTracker.destroy();
        mSliceViewTracker = null;
        mSurfaceScope = null;
        mAccumulatedDySinceLastLoadMore = 0;
        mScrollReporter.onUnbind();

        // Remove Feed content from the content manager. Add spacer if needed.
        ArrayList<NtpListContentManager.FeedContent> list = new ArrayList<>();
        if (shouldPlaceSpacer) {
            if (mSpacerViewContent == null) {
                FrameLayout spacerView = new FrameLayout(mActivity);
                mSpacerViewContent = new NtpListContentManager.NativeViewContent(
                        getLateralPaddingsPx(), "Spacer", spacerView);
                spacerView.setLayoutParams(new FrameLayout.LayoutParams(
                        ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
            }
            list.add(mSpacerViewContent);
        }
        updateContentsInPlace(list);

        // Dismiss bottomsheet if any is shown.
        dismissBottomSheet();

        // Clear handlers.
        mContentManager.setHandlers(new HashMap<>());
        mContentManager = null;

        mRecyclerView.removeOnScrollListener(mMainScrollListener);
        mRecyclerView.getAdapter().unregisterAdapterDataObserver(mRestoreScrollObserver);
        mRecyclerView = null;

        if (mWindowAndroid.getDisplay() != null) {
            mWindowAndroid.getDisplay().removeObserver(mRotationObserver);
        }

        FeedStreamJni.get().surfaceClosed(mNativeFeedStream, FeedStream.this);
    }

    @Override
    public void notifyNewHeaderCount(int newHeaderCount) {
        mHeaderCount = newHeaderCount;
    }

    @Override
    public void addOnContentChangedListener(ContentChangedListener listener) {
        mContentChangedListeners.addObserver(listener);
    }

    @Override
    public void removeOnContentChangedListener(ContentChangedListener listener) {
        mContentChangedListeners.removeObserver(listener);
    }

    @Override
    public void triggerRefresh(Callback<Boolean> callback) {
        PostTask.postTask(UiThreadTaskTraits.DEFAULT, () -> {
            if (mRenderer != null) {
                mRenderer.onPullToRefreshStarted();
            }
            FeedStreamJni.get().manualRefresh(mNativeFeedStream, FeedStream.this, callback);
        });
    }

    @Override
    public boolean isPlaceholderShown() {
        return mIsPlaceholderShown;
    }

    @Override
    public void hidePlaceholder() {
        if (!mIsPlaceholderShown || mContentManager == null) {
            return;
        }
        ObjectAnimator animator = ObjectAnimator.ofPropertyValuesHolder(
                mRecyclerView.getBackground(), PropertyValuesHolder.ofInt("alpha", 255));
        animator.setTarget(mRecyclerView.getBackground());
        animator.setDuration(mRecyclerView.getItemAnimator().getAddDuration())
                .setInterpolator(Interpolators.LINEAR_INTERPOLATOR);
        animator.start();
        mIsPlaceholderShown = false;
    }

    void dismissBottomSheet() {
        assert ThreadUtils.runningOnUiThread();
        if (mBottomSheetContent != null) {
            mBottomSheetController.hideContent(mBottomSheetContent, true);
        }
        mBottomSheetContent = null;
        mBottomSheetOriginatingSliceId = null;
    }

    @CalledByNative
    void replaceDataStoreEntry(String key, byte[] data) {
        if (mSurfaceScope != null) mSurfaceScope.replaceDataStoreEntry(key, data);
    }

    @CalledByNative
    void removeDataStoreEntry(String key) {
        if (mSurfaceScope != null) mSurfaceScope.removeDataStoreEntry(key);
    }

    @VisibleForTesting
    void checkScrollingForLoadMore(int dy) {
        if (mContentManager == null) return;

        mAccumulatedDySinceLastLoadMore += dy;
        if (mAccumulatedDySinceLastLoadMore < 0) {
            mAccumulatedDySinceLastLoadMore = 0;
        }
        if (mAccumulatedDySinceLastLoadMore < TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP,
                    mLoadMoreTriggerScrollDistanceDp,
                    mRecyclerView.getResources().getDisplayMetrics())) {
            return;
        }

        boolean canTrigger = maybeLoadMore();
        if (canTrigger) {
            mAccumulatedDySinceLastLoadMore = 0;
        }
    }

    @Override
    public ObservableSupplier<Boolean> hasUnreadContent() {
        return mUnreadContentObserver != null ? mUnreadContentObserver.mHasUnreadContent
                                              : Stream.super.hasUnreadContent();
    }

    @Override
    public long getLastFetchTimeMs() {
        return FeedStreamJni.get().getLastFetchTimeMs(mNativeFeedStream, FeedStream.this);
    }

    /**
     * Attempts to load more content if it can be triggered.
     *
     * <p>This method uses the default or Finch configured load more lookahead trigger.
     *
     * @return true if loading more content can be triggered.
     */
    boolean maybeLoadMore() {
        return maybeLoadMore(mLoadMoreTriggerLookahead);
    }

    /**
     * Attempts to load more content if it can be triggered.
     * @param lookaheadTrigger The threshold of off-screen cards below which the feed should attempt
     *         to load more content. I.e., if there are less than or equal to |lookaheadTrigger|
     *         cards left to show the user, then the feed should load more cards.
     * @return true if loading more content can be triggered.
     */
    private boolean maybeLoadMore(int lookaheadTrigger) {
        // Checks if we've been unbinded.
        if (mRecyclerView == null) {
            return false;
        }
        // Checks if loading more can be triggered.
        LinearLayoutManager layoutManager = (LinearLayoutManager) mRecyclerView.getLayoutManager();
        if (layoutManager == null) {
            return false;
        }

        // Check if the layout manager is initialized.
        int totalItemCount = layoutManager.getItemCount();
        if (totalItemCount < 0) {
            return false;
        }

        // When swapping feeds, the totalItemCount and lastVisibleItemPosition can temporarily fall
        // out of sync. Early exit on the pathological case where we think we're showing an item
        // beyond the end of the feed. This can occur if maybeLoadMore() is called during a feed
        // swap, after the feed items have been cleared, but before the view has finished updating
        // (which happens asynchronously).
        int lastVisibleItem = layoutManager.findLastVisibleItemPosition();
        if (totalItemCount < lastVisibleItem) {
            return false;
        }

        // No need to load more if there are more scrollable items than the trigger amount.
        int numItemsRemaining = totalItemCount - lastVisibleItem;
        if (numItemsRemaining > lookaheadTrigger) {
            return false;
        }

        // Starts to load more content if not yet.
        if (!mIsLoadingMoreContent) {
            mIsLoadingMoreContent = true;
            FeedUma.recordFeedLoadMoreTrigger(getStreamKind(), totalItemCount, numItemsRemaining);
            // The native loadMore() call may immediately result in onStreamUpdated(), which can
            // result in a crash if maybeLoadMore() is being called in response to certain events.
            // Use postTask to avoid this.
            PostTask.postTask(UiThreadTaskTraits.DEFAULT,
                    ()
                            -> FeedStreamJni.get().loadMore(mNativeFeedStream, FeedStream.this,
                                    (Boolean success) -> { mIsLoadingMoreContent = false; }));
        }

        return true;
    }

    /**
     * Called when the stream update content is available. The content will get passed to UI
     */
    @CalledByNative
    void onStreamUpdated(byte[] data) {
        // There should be no updates while the surface is closed. If the surface was recently
        // closed, just ignore these.
        if (mContentManager == null) return;
        FeedUiProto.StreamUpdate streamUpdate;
        try {
            streamUpdate = FeedUiProto.StreamUpdate.parseFrom(data);
        } catch (com.google.protobuf.InvalidProtocolBufferException e) {
            Log.wtf(TAG, "Unable to parse StreamUpdate proto data", e);
            mReliabilityLoggingBridge.onStreamUpdateError();
            return;
        }

        mLastFetchTimeMs = streamUpdate.getFetchTimeMs();

        FeedLoggingParameters loggingParameters =
                new FeedLoggingParameters(streamUpdate.getLoggingParameters());

        // Invalidate the saved scroll state if the content in the feed has changed.
        // Don't do anything if mLastFetchTimeMs is unset.
        if (mScrollStateToRestore != null && mLastFetchTimeMs != 0) {
            if (!mScrollStateToRestore.feedContentState.equals(getContentState())) {
                mScrollStateToRestore = null;
            }
        }

        // Update using shared states.
        for (FeedUiProto.SharedState state : streamUpdate.getNewSharedStatesList()) {
            mRenderer.update(state.getXsurfaceSharedState().toByteArray());
        }

        // Builds the new list containing:
        // * existing headers
        // * both new and existing contents
        ArrayList<NtpListContentManager.FeedContent> newContentList = new ArrayList<>();
        for (FeedUiProto.StreamUpdate.SliceUpdate sliceUpdate :
                streamUpdate.getUpdatedSlicesList()) {
            if (sliceUpdate.hasSlice()) {
                NtpListContentManager.FeedContent content =
                        createContentFromSlice(sliceUpdate.getSlice(), loggingParameters);
                if (content != null) {
                    newContentList.add(content);
                }
            } else {
                String existingSliceId = sliceUpdate.getSliceId();
                int position = mContentManager.findContentPositionByKey(existingSliceId);
                if (position != -1) {
                    newContentList.add(mContentManager.getContent(position));
                }
            }
        }
        updateContentsInPlace(newContentList);

        // TODO(iwells): Look into alternatives to View.post() that specifically wait for rendering.
        mRecyclerView.post(mReliabilityLoggingBridge::onStreamUpdateFinished);

        // If all of the cards fit on the screen, load more content. The view
        // may not be scrollable, preventing the user from otherwise triggering
        // load more.
        maybeLoadMore(/*lookaheadTrigger=*/0);
    }

    private NtpListContentManager.FeedContent createContentFromSlice(
            FeedUiProto.Slice slice, LoggingParameters loggingParameters) {
        String sliceId = slice.getSliceId();
        if (slice.hasXsurfaceSlice()) {
            return new NtpListContentManager.ExternalViewContent(sliceId,
                    slice.getXsurfaceSlice().getXsurfaceFrame().toByteArray(), loggingParameters);
        } else if (slice.hasLoadingSpinnerSlice()) {
            // If the placeholder is shown, spinner is not needed.
            if (mIsPlaceholderShown) {
                return null;
            }
            if (ChromeFeatureList.isEnabled(ChromeFeatureList.FEED_LOADING_PLACEHOLDER)
                    && slice.getLoadingSpinnerSlice().getIsAtTop()) {
                return new NtpListContentManager.NativeViewContent(
                        getLateralPaddingsPx(), sliceId, R.layout.feed_placeholder_layout);
            }
            return new NtpListContentManager.NativeViewContent(
                    getLateralPaddingsPx(), sliceId, R.layout.feed_spinner);
        }
        assert slice.hasZeroStateSlice();
        if (mStreamKind == StreamKind.FOLLOWING) {
            return new NtpListContentManager.NativeViewContent(
                    getLateralPaddingsPx(), sliceId, R.layout.following_empty_state);
        }
        if (slice.getZeroStateSlice().getType() == FeedUiProto.ZeroStateSlice.Type.CANT_REFRESH) {
            return new NtpListContentManager.NativeViewContent(
                    getLateralPaddingsPx(), sliceId, R.layout.no_connection);
        }
        // TODO(crbug/1152592): Add new UI for NO_WEB_FEED_SUBSCRIPTIONS.
        assert slice.getZeroStateSlice().getType()
                        == FeedUiProto.ZeroStateSlice.Type.NO_CARDS_AVAILABLE
                || slice.getZeroStateSlice().getType()
                        == FeedUiProto.ZeroStateSlice.Type.NO_WEB_FEED_SUBSCRIPTIONS;
        return new NtpListContentManager.NativeViewContent(
                getLateralPaddingsPx(), sliceId, R.layout.no_content_v2);
    }

    private void updateContentsInPlace(
            ArrayList<NtpListContentManager.FeedContent> newContentList) {
        assert mHeaderCount <= mContentManager.getItemCount();
        if (mContentManager.replaceRange(
                    mHeaderCount, mContentManager.getItemCount() - mHeaderCount, newContentList)) {
            notifyContentChange();
        }
    }

    private @StreamType int getStreamType() {
        switch (mStreamKind) {
            case StreamKind.FOR_YOU:
                return StreamType.FOR_YOU;
            case StreamKind.FOLLOWING:
                return StreamType.WEB_FEED;
            default:
                return StreamType.UNSPECIFIED;
        }
    }

    /**
     * Restores the scroll state serialized to |savedInstanceState|.
     * @return true if the scroll state was restored, or if the state could never be restored.
     * false if we need to wait until more items are added to the recycler view to make it
     * scrollable.
     */
    private boolean restoreScrollState(FeedScrollState state) {
        assert (mRecyclerView != null);
        if (state == null || state.lastPosition < 0 || state.position < 0) return true;

        // If too few items exist, defer scrolling until later.
        if (mContentManager.getItemCount() <= state.lastPosition) return false;
        // Don't try to resume scrolling to a native view. This avoids scroll resume to the refresh
        // spinner.
        if (mContentManager.getContent(state.lastPosition).isNativeView()) return false;

        LinearLayoutManager layoutManager = (LinearLayoutManager) mRecyclerView.getLayoutManager();
        if (layoutManager != null) {
            layoutManager.scrollToPositionWithOffset(state.position, state.offset);
        }
        return true;
    }

    private void notifyContentChange() {
        for (ContentChangedListener listener : mContentChangedListeners) {
            listener.onContentChanged(
                    mContentManager != null ? mContentManager.getContentList() : null);
        }
    }

    @VisibleForTesting
    String getSliceIdFromView(View view) {
        View childOfRoot = findChildViewContainingDescendant(mRecyclerView, view);

        if (childOfRoot != null) {
            // View is a child of the recycler view, find slice using the index.
            int position = mRecyclerView.getChildAdapterPosition(childOfRoot);
            if (position >= 0 && position < mContentManager.getItemCount()) {
                return mContentManager.getContent(position).getKey();
            }
        } else if (mBottomSheetContent != null
                && findChildViewContainingDescendant(mBottomSheetContent.getContentView(), view)
                        != null) {
            // View is a child of the bottom sheet, return slice associated with the bottom
            // sheet.
            return mBottomSheetOriginatingSliceId;
        }
        return "";
    }

    /**
     * Returns the immediate child of parentView which contains descendantView.
     * If descendantView is not in parentView's view hierarchy, this returns null.
     * Note that the returned view may be descendantView, or descendantView.getParent(),
     * or descendantView.getParent().getParent(), etc...
     */
    private View findChildViewContainingDescendant(View parentView, View descendantView) {
        if (parentView == null || descendantView == null) return null;
        // Find the direct child of parentView which owns view.
        if (parentView == descendantView.getParent()) {
            return descendantView;
        } else {
            // One of the view's ancestors might be the child.
            ViewParent p = descendantView.getParent();
            while (true) {
                if (p == null) {
                    return null;
                }
                if (p.getParent() == parentView) {
                    if (p instanceof View) return (View) p;
                    return null;
                }
                p = p.getParent();
            }
        }
    }

    @VisibleForTesting
    void setHelpAndFeedbackLauncherForTest(HelpAndFeedbackLauncher launcher) {
        mHelpAndFeedbackLauncher = launcher;
    }

    @VisibleForTesting
    void setShareWrapperForTest(ShareHelperWrapper shareWrapper) {
        mShareHelper = shareWrapper;
    }

    /** @returns True if this feed has been bound. */
    @VisibleForTesting
    public boolean getBoundStatusForTest() {
        return mContentManager != null;
    }

    @VisibleForTesting
    RecyclerView.OnScrollListener getScrollListenerForTest() {
        return mMainScrollListener;
    }

    @VisibleForTesting
    UnreadContentObserver getUnreadContentObserverForTest() {
        return mUnreadContentObserver;
    }

    // Scroll state can't be restored until enough items are added to the recycler view adapter.
    // Attempts to restore scroll state every time new items are added to the adapter.
    class RestoreScrollObserver extends RecyclerView.AdapterDataObserver {
        @Override
        public void onItemRangeInserted(int positionStart, int itemCount) {
            if (mScrollStateToRestore != null) {
                if (restoreScrollState(mScrollStateToRestore)) {
                    mScrollStateToRestore = null;
                }
            }
        }
    };

    private class ViewTrackerObserver implements FeedSliceViewTracker.Observer {
        @Override
        public void sliceVisible(String sliceId) {
            FeedStreamJni.get().reportSliceViewed(mNativeFeedStream, FeedStream.this, sliceId);
        }
        @Override
        public void feedContentVisible() {
            FeedStreamJni.get().reportFeedViewed(mNativeFeedStream, FeedStream.this);
        }
    }

    /**
     * Provides a wrapper around sharing methods.
     *
     * Makes it easier to test.
     */
    @VisibleForTesting
    static class ShareHelperWrapper {
        private WindowAndroid mWindowAndroid;
        private Supplier<ShareDelegate> mShareDelegateSupplier;
        public ShareHelperWrapper(
                WindowAndroid windowAndroid, Supplier<ShareDelegate> shareDelegateSupplier) {
            mWindowAndroid = windowAndroid;
            mShareDelegateSupplier = shareDelegateSupplier;
        }

        /**
         * Shares a url and title from Chrome to another app.
         * Brings up the share sheet.
         */
        public void share(String url, String title) {
            ShareParams params = new ShareParams.Builder(mWindowAndroid, title, url).build();
            mShareDelegateSupplier.get().share(params, new ChromeShareExtras.Builder().build(),
                    ShareDelegate.ShareOrigin.FEED);
        }
    }

    // Ingests scroll events and reports scroll completion back to native.
    private class ScrollReporter extends ScrollTracker {
        @Override
        protected void onScrollEvent(int scrollAmount) {
            FeedStreamJni.get().reportStreamScrolled(
                    mNativeFeedStream, FeedStream.this, scrollAmount);
        }
    }

    @VisibleForTesting
    class UnreadContentObserver extends FeedServiceBridge.UnreadContentObserver {
        ObservableSupplierImpl<Boolean> mHasUnreadContent = new ObservableSupplierImpl<>();

        UnreadContentObserver(boolean isWebFeed) {
            super(isWebFeed);
            mHasUnreadContent.set(false);
        }

        @Override
        public void hasUnreadContentChanged(boolean hasUnreadContent) {
            mHasUnreadContent.set(hasUnreadContent);
        }
    }

    private int getLateralPaddingsPx() {
        return mActivity.getResources().getDimensionPixelSize(
                R.dimen.ntp_header_lateral_paddings_v2);
    }

    @NativeMethods
    @VisibleForTesting
    public interface Natives {
        long init(FeedStream caller, @StreamKind int streamKind,
                long nativeFeedReliabilityLoggingBridge);
        void reportFeedViewed(long nativeFeedStream, FeedStream caller);
        void reportSliceViewed(long nativeFeedStream, FeedStream caller, String sliceId);
        void reportPageLoaded(long nativeFeedStream, FeedStream caller, boolean inNewTab);
        void reportOpenAction(long nativeFeedStream, FeedStream caller, GURL url, String sliceId);
        void reportOpenInNewTabAction(
                long nativeFeedStream, FeedStream caller, GURL url, String sliceId);
        void reportOtherUserAction(
                long nativeFeedStream, FeedStream caller, @FeedUserActionType int userAction);
        void reportStreamScrolled(long nativeFeedStream, FeedStream caller, int distanceDp);
        void reportStreamScrollStart(long nativeFeedStream, FeedStream caller);
        void loadMore(long nativeFeedStream, FeedStream caller, Callback<Boolean> callback);
        void manualRefresh(long nativeFeedStream, FeedStream caller, Callback<Boolean> callback);
        void processThereAndBackAgain(
                long nativeFeedStream, FeedStream caller, byte[] data, byte[] loggingParameters);
        int executeEphemeralChange(long nativeFeedStream, FeedStream caller, byte[] data);
        void commitEphemeralChange(long nativeFeedStream, FeedStream caller, int changeId);
        void discardEphemeralChange(long nativeFeedStream, FeedStream caller, int changeId);
        void surfaceOpened(long nativeFeedStream, FeedStream caller);
        void surfaceClosed(long nativeFeedStream, FeedStream caller);
        int getSurfaceId(long nativeFeedStream, FeedStream caller);
        long getLastFetchTimeMs(long nativeFeedStream, FeedStream caller);
        void reportNoticeCreated(long nativeFeedStream, FeedStream caller, String key);
        void reportNoticeViewed(long nativeFeedStream, FeedStream caller, String key);
        void reportNoticeOpenAction(long nativeFeedStream, FeedStream caller, String key);
        void reportNoticeDismissed(long nativeFeedStream, FeedStream caller, String key);
    }
}
