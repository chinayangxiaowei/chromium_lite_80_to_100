// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.ntp;

import android.app.Activity;
import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Point;
import android.graphics.Rect;
import android.graphics.drawable.Drawable;
import android.view.LayoutInflater;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;

import androidx.annotation.ColorInt;
import androidx.annotation.VisibleForTesting;
import androidx.core.view.ViewCompat;
import androidx.recyclerview.widget.RecyclerView;

import com.google.android.material.color.MaterialColors;

import org.chromium.base.Log;
import org.chromium.base.ObserverList;
import org.chromium.base.TimeUtils;
import org.chromium.base.TraceEvent;
import org.chromium.base.jank_tracker.JankScenario;
import org.chromium.base.jank_tracker.JankTracker;
import org.chromium.base.metrics.RecordHistogram;
import org.chromium.base.metrics.RecordUserAction;
import org.chromium.base.supplier.Supplier;
import org.chromium.chrome.R;
import org.chromium.chrome.browser.app.feed.FeedActionDelegateImpl;
import org.chromium.chrome.browser.bookmarks.BookmarkBridge;
import org.chromium.chrome.browser.browser_controls.BrowserControlsStateProvider;
import org.chromium.chrome.browser.compositor.layouts.content.InvalidationAwareThumbnailProvider;
import org.chromium.chrome.browser.download.DownloadManagerService;
import org.chromium.chrome.browser.feature_engagement.TrackerFactory;
import org.chromium.chrome.browser.feature_guide.notifications.FeatureNotificationUtils;
import org.chromium.chrome.browser.feature_guide.notifications.FeatureType;
import org.chromium.chrome.browser.feed.FeedActionDelegate;
import org.chromium.chrome.browser.feed.FeedLaunchReliabilityLoggingState;
import org.chromium.chrome.browser.feed.FeedReliabilityLoggingSignals;
import org.chromium.chrome.browser.feed.FeedSurfaceCoordinator;
import org.chromium.chrome.browser.feed.FeedSurfaceDelegate;
import org.chromium.chrome.browser.feed.FeedSurfaceLifecycleManager;
import org.chromium.chrome.browser.feed.FeedSurfaceProvider;
import org.chromium.chrome.browser.feed.FeedSwipeRefreshLayout;
import org.chromium.chrome.browser.feed.NtpFeedSurfaceLifecycleManager;
import org.chromium.chrome.browser.feed.componentinterfaces.SurfaceCoordinator;
import org.chromium.chrome.browser.feedback.HelpAndFeedbackLauncherImpl;
import org.chromium.chrome.browser.lifecycle.ActivityLifecycleDispatcher;
import org.chromium.chrome.browser.lifecycle.LifecycleObserver;
import org.chromium.chrome.browser.lifecycle.PauseResumeWithNativeObserver;
import org.chromium.chrome.browser.native_page.ContextMenuManager;
import org.chromium.chrome.browser.omnibox.OmniboxFocusReason;
import org.chromium.chrome.browser.omnibox.OmniboxStub;
import org.chromium.chrome.browser.omnibox.voice.VoiceRecognitionHandler;
import org.chromium.chrome.browser.privacy.settings.PrivacyPreferencesManagerImpl;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.chrome.browser.profiles.ProfileKey;
import org.chromium.chrome.browser.query_tiles.QueryTileSection.QueryInfo;
import org.chromium.chrome.browser.search_engines.TemplateUrlServiceFactory;
import org.chromium.chrome.browser.share.ShareDelegate;
import org.chromium.chrome.browser.suggestions.SuggestionsMetrics;
import org.chromium.chrome.browser.suggestions.SuggestionsNavigationDelegate;
import org.chromium.chrome.browser.suggestions.SuggestionsUiDelegateImpl;
import org.chromium.chrome.browser.suggestions.tile.Tile;
import org.chromium.chrome.browser.suggestions.tile.TileGroup;
import org.chromium.chrome.browser.suggestions.tile.TileGroupDelegateImpl;
import org.chromium.chrome.browser.tab.EmptyTabObserver;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.chrome.browser.tab.TabHidingType;
import org.chromium.chrome.browser.tab.TabObserver;
import org.chromium.chrome.browser.tab.TabSelectionType;
import org.chromium.chrome.browser.tabmodel.TabModelSelector;
import org.chromium.chrome.browser.toolbar.top.Toolbar;
import org.chromium.chrome.browser.ui.messages.snackbar.SnackbarManager;
import org.chromium.chrome.browser.ui.native_page.NativePage;
import org.chromium.chrome.browser.ui.native_page.NativePageHost;
import org.chromium.chrome.browser.vr.VrModuleProvider;
import org.chromium.chrome.browser.xsurface.FeedLaunchReliabilityLogger.SurfaceType;
import org.chromium.components.browser_ui.bottomsheet.BottomSheetController;
import org.chromium.components.browser_ui.styles.ChromeColors;
import org.chromium.components.embedder_support.util.UrlConstants;
import org.chromium.components.embedder_support.util.UrlUtilities;
import org.chromium.components.feature_engagement.EventConstants;
import org.chromium.components.feature_engagement.Tracker;
import org.chromium.components.search_engines.TemplateUrlService.TemplateUrlServiceObserver;
import org.chromium.content_public.browser.LoadUrlParams;
import org.chromium.content_public.browser.NavigationController;
import org.chromium.content_public.browser.NavigationEntry;
import org.chromium.ui.base.DeviceFormFactor;
import org.chromium.ui.base.WindowAndroid;
import org.chromium.ui.mojom.WindowOpenDisposition;

import java.util.List;

/**
 * Provides functionality when the user interacts with the NTP.
 */
public class NewTabPage implements NativePage, InvalidationAwareThumbnailProvider,
                                   TemplateUrlServiceObserver,
                                   BrowserControlsStateProvider.Observer, FeedSurfaceDelegate,
                                   VoiceRecognitionHandler.Observer {
    private static final String TAG = "NewTabPage";

    // Key for the scroll position data that may be stored in a navigation entry.
    private static final String NAVIGATION_ENTRY_SCROLL_POSITION_KEY = "NewTabPageScrollPosition";
    public static final String CONTEXT_MENU_USER_ACTION_PREFIX = "Suggestions";

    protected final Tab mTab;
    private final Supplier<Tab> mActivityTabProvider;
    private final ActivityLifecycleDispatcher mActivityLifecycleDispatcher;

    private final String mTitle;
    private final JankTracker mJankTracker;
    private Context mContext;
    private final int mBackgroundColor;
    protected final NewTabPageManagerImpl mNewTabPageManager;
    protected final TileGroup.Delegate mTileGroupDelegate;
    private final boolean mIsTablet;
    private final BrowserControlsStateProvider mBrowserControlsStateProvider;
    private final NewTabPageUma mNewTabPageUma;
    private final ContextMenuManager mContextMenuManager;
    private final ObserverList<MostVisitedTileClickObserver> mMostVisitedTileClickObservers;
    private FeedSurfaceProvider mFeedSurfaceProvider;
    private FeedReliabilityLoggingSignals mFeedReliabilityLoggingSignals;

    private NewTabPageLayout mNewTabPageLayout;
    private TabObserver mTabObserver;
    private LifecycleObserver mLifecycleObserver;
    protected boolean mSearchProviderHasLogo;

    protected OmniboxStub mOmniboxStub;
    private VoiceRecognitionHandler mVoiceRecognitionHandler;

    // The timestamp at which the constructor was called.
    protected final long mConstructedTimeNs;

    // The timestamp at which this NTP was last shown to the user.
    private long mLastShownTimeNs;

    private boolean mIsLoaded;

    // Whether destroy() has been called.
    private boolean mIsDestroyed;

    private final int mTabStripAndToolbarHeight;

    private final Supplier<Toolbar> mToolbarSupplier;

    @Override
    public void onControlsOffsetChanged(int topOffset, int topControlsMinHeightOffset,
            int bottomOffset, int bottomControlsMinHeightOffset, boolean needsAnimate) {
        updateMargins();
    }

    @Override
    public void onBottomControlsHeightChanged(
            int bottomControlsHeight, int bottomControlsMinHeight) {
        updateMargins();
    }

    /**
     * Allows clients to listen for updates to the scroll changes of the search box on the
     * NTP.
     */
    public interface OnSearchBoxScrollListener {
        /**
         * Callback to be notified when the scroll position of the search box on the NTP has
         * changed.  A scroll percentage of 0, means the search box has no scroll applied and
         * is in it's natural resting position.  A value of 1 means the search box is scrolled
         * entirely to the top of the screen viewport.
         *
         * @param scrollPercentage The percentage the search box has been scrolled off the page.
         */
        void onNtpScrollChanged(float scrollPercentage);
    }

    /**
     * An observer for most visited tile clicks.
     */
    public interface MostVisitedTileClickObserver {
        /**
         * Called when a most visited tile is clicked.
         * @param tile The most visited tile that was clicked.
         * @param tab The tab hosting the most visited tile section.
         */
        void onMostVisitedTileClicked(Tile tile, Tab tab);
    }

    protected class NewTabPageManagerImpl
            extends SuggestionsUiDelegateImpl implements NewTabPageManager {
        private final Tracker mTracker;

        public NewTabPageManagerImpl(SuggestionsNavigationDelegate navigationDelegate,
                Profile profile, NativePageHost nativePageHost, SnackbarManager snackbarManager) {
            super(navigationDelegate, profile, nativePageHost, snackbarManager);
            mTracker = TrackerFactory.getTrackerForProfile(profile);
        }

        @Override
        public boolean isLocationBarShownInNTP() {
            if (mIsDestroyed) return false;
            return isInSingleUrlBarMode() && !mNewTabPageLayout.urlFocusAnimationsDisabled();
        }

        @Override
        public boolean isVoiceSearchEnabled() {
            return mVoiceRecognitionHandler != null
                    && mVoiceRecognitionHandler.isVoiceSearchEnabled();
        }

        @Override
        public void focusSearchBox(boolean beginVoiceSearch, String pastedText) {
            if (mIsDestroyed) return;
            if (VrModuleProvider.getDelegate().isInVr()) return;
            if (mVoiceRecognitionHandler != null && beginVoiceSearch) {
                mFeedReliabilityLoggingSignals.onVoiceSearch();
                mVoiceRecognitionHandler.startVoiceRecognition(
                        VoiceRecognitionHandler.VoiceInteractionSource.NTP);
                mTracker.notifyEvent(EventConstants.NTP_VOICE_SEARCH_BUTTON_CLICKED);
            } else if (mOmniboxStub != null) {
                mFeedReliabilityLoggingSignals.onOmniboxFocused();
                mOmniboxStub.setUrlBarFocus(true, pastedText,
                        pastedText == null ? OmniboxFocusReason.FAKE_BOX_TAP
                                           : OmniboxFocusReason.FAKE_BOX_LONG_PRESS);
            }
        }

        @Override
        public void performSearchQuery(QueryInfo queryInfo) {
            if (mOmniboxStub == null) return;
            mOmniboxStub.performSearchQuery(queryInfo.queryText, queryInfo.searchParams);
        }

        @Override
        public boolean isCurrentPage() {
            if (mIsDestroyed) return false;
            if (mOmniboxStub == null) return false;
            return getNewTabPageForCurrentTab() == NewTabPage.this;
        }

        private NewTabPage getNewTabPageForCurrentTab() {
            Tab currentTab = mActivityTabProvider.get();
            NativePage nativePage = currentTab != null ? currentTab.getNativePage() : null;
            return (nativePage instanceof NewTabPage) ? (NewTabPage) nativePage : null;
        }

        @Override
        public void onLoadingComplete() {
            if (mIsDestroyed) return;

            long loadTimeMs = (System.nanoTime() - mConstructedTimeNs) / 1000000;
            RecordHistogram.recordTimesHistogram("Tab.NewTabOnload", loadTimeMs);
            mIsLoaded = true;
            NewTabPageUma.recordNTPImpression(NewTabPageUma.NTP_IMPRESSION_REGULAR);
            // If not visible when loading completes, wait until onShown is received.
            if (!mTab.isHidden()) recordNTPShown();
        }
    }

    /**
     * Extends {@link TileGroupDelegateImpl} to add metrics logging that is specific to
     * {@link NewTabPage}.
     */
    private class NewTabPageTileGroupDelegate extends TileGroupDelegateImpl {
        private NewTabPageTileGroupDelegate(Context context, Profile profile,
                SuggestionsNavigationDelegate navigationDelegate, SnackbarManager snackbarManager) {
            super(context, profile, navigationDelegate, snackbarManager);
        }

        @Override
        public void onLoadingComplete(List<Tile> tiles) {
            if (mIsDestroyed) return;

            super.onLoadingComplete(tiles);
            mNewTabPageLayout.onTilesLoaded();
        }

        @Override
        public void openMostVisitedItem(int windowDisposition, Tile tile) {
            if (mIsDestroyed) return;

            super.openMostVisitedItem(windowDisposition, tile);
            for (MostVisitedTileClickObserver observer : mMostVisitedTileClickObservers) {
                observer.onMostVisitedTileClicked(tile, mTab);
            }
            if (windowDisposition != WindowOpenDisposition.NEW_WINDOW) {
                RecordHistogram.recordMediumTimesHistogram("NewTabPage.MostVisitedTime",
                        (System.nanoTime() - mLastShownTimeNs)
                                / TimeUtils.NANOSECONDS_PER_MILLISECOND);
            }
        }
    }

    /**
     * Constructs a NewTabPage.
     * @param activity The activity used for context to create the new tab page's View.
     * @param browserControlsStateProvider {@link BrowserControlsStateProvider} to observe for
     *         offset changes.
     * @param activityTabProvider Provides the current active tab.
     * @param snackbarManager {@link SnackbarManager} object.
     * @param lifecycleDispatcher Activity lifecycle dispatcher.
     * @param tabModelSelector {@link TabModelSelector} object.
     * @param isTablet {@code true} if running on a Tablet device.
     * @param uma {@link NewTabPageUma} object recording user metrics.
     * @param isInNightMode {@code true} if the night mode setting is on.
     * @param nativePageHost The host that is showing this new tab page.
     * @param tab The {@link Tab} that contains this new tab page.
     * @param url The URL that launched this new tab page.
     * @param bottomSheetController The controller for bottom sheets, used by the feed.
     * @param shareDelegateSupplier Supplies the Delegate used to open SharingHub.
     * @param windowAndroid The containing window of this page.
     * @param jankTracker {@link JankTracker} object to measure jankiness while NTP is visible.
     * @param toolbarSupplier Supplies the {@link Toolbar}.
     */
    public NewTabPage(Activity activity, BrowserControlsStateProvider browserControlsStateProvider,
            Supplier<Tab> activityTabProvider, SnackbarManager snackbarManager,
            ActivityLifecycleDispatcher lifecycleDispatcher, TabModelSelector tabModelSelector,
            boolean isTablet, NewTabPageUma uma, boolean isInNightMode,
            NativePageHost nativePageHost, Tab tab, String url,
            BottomSheetController bottomSheetController,
            Supplier<ShareDelegate> shareDelegateSupplier, WindowAndroid windowAndroid,
            JankTracker jankTracker, Supplier<Toolbar> toolbarSupplier) {
        mConstructedTimeNs = System.nanoTime();
        TraceEvent.begin(TAG);

        mActivityTabProvider = activityTabProvider;
        mActivityLifecycleDispatcher = lifecycleDispatcher;
        mTab = tab;
        mNewTabPageUma = uma;
        mJankTracker = jankTracker;
        mToolbarSupplier = toolbarSupplier;
        mMostVisitedTileClickObservers = new ObserverList<>();
        mBrowserControlsStateProvider = browserControlsStateProvider;

        Profile profile = Profile.fromWebContents(mTab.getWebContents());

        SuggestionsNavigationDelegate navigationDelegate = new SuggestionsNavigationDelegate(
                activity, profile, nativePageHost, tabModelSelector, mTab);
        mNewTabPageManager = new NewTabPageManagerImpl(
                navigationDelegate, profile, nativePageHost, snackbarManager);
        mTileGroupDelegate = new NewTabPageTileGroupDelegate(
                activity, profile, navigationDelegate, snackbarManager);

        mContext = activity;
        mTitle = activity.getResources().getString(R.string.new_tab_title);
        mBackgroundColor = MaterialColors.getColor(mContext, R.attr.default_bg_color_dynamic, TAG);
        mIsTablet = isTablet;
        TemplateUrlServiceFactory.get().addObserver(this);

        mTabObserver = new EmptyTabObserver() {
            @Override
            public void onShown(Tab tab, @TabSelectionType int type) {
                // Showing the NTP is only meaningful when the page has been loaded already.
                if (mIsLoaded) recordNTPShown();

                mNewTabPageLayout.getTileGroup().onSwitchToForeground(/* trackLoadTask = */ false);
            }

            @Override
            public void onHidden(Tab tab, @TabHidingType int type) {
                if (mIsLoaded) recordNTPHidden();
            }

            @Override
            public void onInteractabilityChanged(Tab tab, boolean isInteractable) {
                // We start/stop tracking based on InteractabilityChanged in addition to
                // Shown/Hidden because those events don't trigger for switching to tab switcher, we
                // don't rely solely on this event because it doeesn't trigger when the user
                // navigates to a website.
                if (isInteractable) {
                    mJankTracker.startTrackingScenario(JankScenario.NEW_TAB_PAGE);
                } else {
                    mJankTracker.finishTrackingScenario(JankScenario.NEW_TAB_PAGE);
                }
            }

            @Override
            public void onLoadUrl(Tab tab, LoadUrlParams params, int loadType) {
                mNewTabPageLayout.onLoadUrl(UrlUtilities.isNTPUrl(tab.getUrl()));
            }
        };
        mTab.addObserver(mTabObserver);

        mLifecycleObserver = new PauseResumeWithNativeObserver() {
            @Override
            public void onResumeWithNative() {}

            @Override
            public void onPauseWithNative() {
                // Only record when this tab is the current tab.
                if (mActivityTabProvider.get() == mTab) {
                    RecordUserAction.record("MobileNTPPaused");
                }
            }
        };
        mActivityLifecycleDispatcher.register(mLifecycleObserver);

        updateSearchProviderHasLogo();
        initializeMainView(activity, windowAndroid, snackbarManager, uma, isInNightMode,
                bottomSheetController, shareDelegateSupplier, url);

        getView().addOnAttachStateChangeListener(new View.OnAttachStateChangeListener() {
            @Override
            public void onViewAttachedToWindow(View view) {
                updateMargins();
                getView().removeOnAttachStateChangeListener(this);
            }

            @Override
            public void onViewDetachedFromWindow(View view) {}
        });
        mBrowserControlsStateProvider.addObserver(this);

        DownloadManagerService.getDownloadManagerService().checkForExternallyRemovedDownloads(
                ProfileKey.getLastUsedRegularProfileKey());

        mTabStripAndToolbarHeight =
                activity.getResources().getDimensionPixelSize(R.dimen.tab_strip_and_toolbar_height);

        mNewTabPageUma.recordIsUserOnline();
        mNewTabPageUma.recordContentSuggestionsDisplayStatus(profile);

        // TODO(twellington): Move this somewhere it can be shared with NewTabPageView?
        Runnable closeContextMenuCallback = activity::closeContextMenu;
        mContextMenuManager = new ContextMenuManager(mNewTabPageManager.getNavigationDelegate(),
                mFeedSurfaceProvider.getTouchEnabledDelegate(), closeContextMenuCallback,
                NewTabPage.CONTEXT_MENU_USER_ACTION_PREFIX);
        windowAndroid.addContextMenuCloseListener(mContextMenuManager);

        mNewTabPageLayout.initialize(mNewTabPageManager, activity, mTileGroupDelegate,
                mSearchProviderHasLogo,
                TemplateUrlServiceFactory.get().isDefaultSearchEngineGoogle(),
                mFeedSurfaceProvider.getScrollDelegate(), mContextMenuManager,
                mFeedSurfaceProvider.getUiConfig(), activityTabProvider, lifecycleDispatcher, uma,
                mTab.isIncognito(), windowAndroid);
        TraceEvent.end(TAG);
    }

    /**
     * Create and initialize the main view contained in this NewTabPage.
     * @param activity The activity used to initialize the view.
     * @param windowAndroid Provides the current active tab.
     * @param snackbarManager {@link SnackbarManager} object.
     * @param uma {@link NewTabPageUma} object recording user metrics.
     * @param isInNightMode {@code true} if the night mode setting is on.
     * @param bottomSheetController The controller for bottom sheets.  Used by the feed.
     * @param shareDelegateSupplier Supplies a delegate used to open SharingHub.
     */
    protected void initializeMainView(Activity activity, WindowAndroid windowAndroid,
            SnackbarManager snackbarManager, NewTabPageUma uma, boolean isInNightMode,
            BottomSheetController bottomSheetController,
            Supplier<ShareDelegate> shareDelegateSupplier, String url) {
        Profile profile = Profile.fromWebContents(mTab.getWebContents());

        LayoutInflater inflater = LayoutInflater.from(activity);
        mNewTabPageLayout = (NewTabPageLayout) inflater.inflate(R.layout.new_tab_page_layout, null);

        FeedActionDelegate actionDelegate = new FeedActionDelegateImpl(activity, snackbarManager,
                mNewTabPageManager.getNavigationDelegate(), new BookmarkBridge(profile)) {
            @Override
            public void openHelpPage() {
                NewTabPageUma.recordAction(NewTabPageUma.ACTION_CLICKED_LEARN_MORE);
                super.openHelpPage();
            }
        };

        FeedSurfaceCoordinator feedSurfaceCoordinator = new FeedSurfaceCoordinator(activity,
                snackbarManager, windowAndroid,
                new SnapScrollHelperImpl(mNewTabPageManager, mNewTabPageLayout), mNewTabPageLayout,
                mBrowserControlsStateProvider.getTopControlsHeight(), isInNightMode, this, profile,
                /* isPlaceholderShownInitially= */ false, bottomSheetController,
                shareDelegateSupplier, /* externalScrollableContainerDelegate= */ null,
                NewTabPageUtils.decodeOriginFromNtpUrl(url),
                PrivacyPreferencesManagerImpl.getInstance(), mToolbarSupplier,
                new FeedLaunchReliabilityLoggingState(SurfaceType.NEW_TAB_PAGE, mConstructedTimeNs),
                FeedSwipeRefreshLayout.create(activity, R.id.toolbar_container),
                /* overScrollDisabled= */ false, /* viewportView= */ null, actionDelegate,
                HelpAndFeedbackLauncherImpl.getInstance());
        mFeedSurfaceProvider = feedSurfaceCoordinator;
        mFeedReliabilityLoggingSignals = feedSurfaceCoordinator;

        // Record the timestamp at which the new tab page's construction started.
        uma.trackTimeToFirstDraw(mFeedSurfaceProvider.getView(), mConstructedTimeNs);

        TrackerFactory.getTrackerForProfile(profile).notifyEvent(EventConstants.NTP_SHOWN);
    }

    /**
     * Saves a single string under a given key to the navigation entry. It is up to the caller to
     * extract and interpret later.
     * @param tab A tab that is used to access the NavigationController and the NavigationEntry
     *            extras.
     * @param key The key to store the data under, will need to be used to access later.
     * @param value The payload to persist.
     *
     * TODO(https://crbug.com/941581): Refactor this to be reusable across NativePage components.
     */
    public static void saveStringToNavigationEntry(Tab tab, String key, String value) {
        if (tab.getWebContents() == null) return;
        NavigationController controller = tab.getWebContents().getNavigationController();
        int index = controller.getLastCommittedEntryIndex();
        NavigationEntry entry = controller.getEntryAtIndex(index);
        if (entry == null) return;

        // At least under test conditions this method may be called initially for the load of the
        // NTP itself, at which point the last committed entry is not for the NTP yet. This method
        // will then be called a second time when the user navigates away, at which point the last
        // committed entry is for the NTP. The extra data must only be set in the latter case.
        if (!UrlUtilities.isNTPUrl(entry.getUrl())) return;

        controller.setEntryExtraData(index, key, value);
    }

    /**
     * @param isTablet Whether the activity is running in tablet mode.
     * @param searchProviderHasLogo Whether the default search engine has logo.
     * @return Whether the NTP is in single url bar mode, i.e. the url bar is shown in-line on the
     *         NTP.
     */
    public static boolean isInSingleUrlBarMode(boolean isTablet, boolean searchProviderHasLogo) {
        return !isTablet && searchProviderHasLogo;
    }

    /**
     * Update the margins for the content when browser controls constraints or bottom control
     *  height are changed.
     */
    private void updateMargins() {
        // TODO(mdjones): can this be merged with BasicNativePage's updateMargins?

        View view = getView();
        ViewGroup.MarginLayoutParams layoutParams =
                ((ViewGroup.MarginLayoutParams) view.getLayoutParams());
        if (layoutParams == null) return;

        // Negative |topControlsDistanceToRest| means the controls Y position is above the rest
        // position and the controls height is increasing with animation, while positive
        // |topControlsDistanceToRest| means the controls Y position is below the rest position and
        // the controls height is decreasing with animation. |getToolbarExtraYOffset()| returns
        // the margin when the controls are at rest, so |getToolbarExtraYOffset()
        // + topControlsDistanceToRest| will give the margin for the current animation frame.
        final int topControlsDistanceToRest = mBrowserControlsStateProvider.getContentOffset()
                - mBrowserControlsStateProvider.getTopControlsHeight();
        final int topMargin = getToolbarExtraYOffset() + topControlsDistanceToRest;

        final int bottomMargin = mBrowserControlsStateProvider.getBottomControlsHeight()
                - mBrowserControlsStateProvider.getBottomControlOffset();

        if (topMargin != layoutParams.topMargin || bottomMargin != layoutParams.bottomMargin) {
            layoutParams.topMargin = topMargin;
            layoutParams.bottomMargin = bottomMargin;
            view.setLayoutParams(layoutParams);
        }

        // Apply negative margin to the top of the N logo (which would otherwise be the height of
        // the top toolbar) when Duet is enabled to remove some of the empty space.
        mNewTabPageLayout.setSearchProviderTopMargin((layoutParams.bottomMargin == 0)
                        ? view.getResources().getDimensionPixelSize(R.dimen.ntp_logo_margin_top)
                        : -view.getResources().getDimensionPixelSize(
                                R.dimen.duet_ntp_logo_top_margin));
    }

    // TODO(sinansahin): This is the same as {@link ToolbarManager#getToolbarExtraYOffset}. So, we
    // should look into sharing the logic.
    /**
     * @return The height that is included in the top controls but not in the toolbar or the tab
     *         strip.
     */
    private int getToolbarExtraYOffset() {
        return mBrowserControlsStateProvider.getTopControlsHeight() - mTabStripAndToolbarHeight;
    }

    /** @return The view container for the new tab layout. */
    @VisibleForTesting
    public NewTabPageLayout getNewTabPageLayout() {
        return mNewTabPageLayout;
    }

    /**
     * Updates whether the NewTabPage should animate on URL focus changes.
     * @param disable Whether to disable the animations.
     */
    public void setUrlFocusAnimationsDisabled(boolean disable) {
        mNewTabPageLayout.setUrlFocusAnimationsDisabled(disable);
    }

    private boolean isInSingleUrlBarMode() {
        return isInSingleUrlBarMode(mIsTablet, mSearchProviderHasLogo);
    }

    private void updateSearchProviderHasLogo() {
        mSearchProviderHasLogo = TemplateUrlServiceFactory.get().doesDefaultSearchEngineHaveLogo();
    }

    private void onSearchEngineUpdated() {
        updateSearchProviderHasLogo();
        setSearchProviderInfoOnView(mSearchProviderHasLogo,
                TemplateUrlServiceFactory.get().isDefaultSearchEngineGoogle());
        mNewTabPageLayout.loadSearchProviderLogo();
    }

    /**
     * Set the search provider info on the main child view, so that it can change layouts if
     * needed.
     * @param hasLogo Whether the search provider has a logo.
     * @param isGoogle Whether the search provider is Google.
     */
    private void setSearchProviderInfoOnView(boolean hasLogo, boolean isGoogle) {
        mNewTabPageLayout.setSearchProviderInfo(hasLogo, isGoogle);
    }

    /**
     * Specifies the percentage the URL is focused during an animation.  1.0 specifies that the URL
     * bar has focus and has completed the focus animation.  0 is when the URL bar is does not have
     * any focus.
     *
     * @param percent The percentage of the URL bar focus animation.
     */
    public void setUrlFocusChangeAnimationPercent(float percent) {
        mNewTabPageLayout.setUrlFocusChangeAnimationPercent(percent);
    }

    /**
     * Get the bounds of the search box in relation to the top level NewTabPage view.
     *
     * @param bounds The current drawing location of the search box.
     * @param translation The translation applied to the search box by the parent view hierarchy up
     *                    to the NewTabPage view.
     */
    public void getSearchBoxBounds(Rect bounds, Point translation) {
        mNewTabPageLayout.getSearchBoxBounds(bounds, translation, getView());
    }

    /**
     * Updates the opacity of the search box when scrolling.
     *
     * @param alpha opacity (alpha) value to use.
     */
    public void setSearchBoxAlpha(float alpha) {
        mNewTabPageLayout.setSearchBoxAlpha(alpha);
    }

    /**
     * Updates the opacity of the search provider logo when scrolling.
     *
     * @param alpha opacity (alpha) value to use.
     */
    public void setSearchProviderLogoAlpha(float alpha) {
        mNewTabPageLayout.setSearchProviderLogoAlpha(alpha);
    }

    /**
     * Set the search box background drawable.
     *
     * @param drawable The search box background.
     */
    public void setSearchBoxBackground(Drawable drawable) {
        mNewTabPageLayout.setSearchBoxBackground(drawable);
    }

    /**
     * @return Whether the location bar is shown in the NTP.
     */
    public boolean isLocationBarShownInNTP() {
        return mNewTabPageManager.isLocationBarShownInNTP();
    }

    /**
     * @return Whether the location bar has been scrolled to top in the NTP.
     */
    public boolean isLocationBarScrolledToTopInNtp() {
        return mNewTabPageLayout.getToolbarTransitionPercentage() == 1;
    }

    /**
     * Sets the listener for search box scroll changes.
     * @param listener The listener to be notified on changes.
     */
    public void setSearchBoxScrollListener(OnSearchBoxScrollListener listener) {
        mNewTabPageLayout.setSearchBoxScrollListener(listener);
    }

    /**
     * Sets the OmniboxStub that this page interacts with.
     */
    public void setOmniboxStub(OmniboxStub omniboxStub) {
        mOmniboxStub = omniboxStub;
        if (mOmniboxStub != null) {
            // The toolbar can't get the reference to the native page until its initialization is
            // finished, so we can't cache it here and transfer it to the view later. We pull that
            // state from the location bar when we get a reference to it as a workaround.
            mNewTabPageLayout.setUrlFocusChangeAnimationPercent(
                    omniboxStub.isUrlBarFocused() ? 1f : 0f);
            mOmniboxStub.addUrlFocusChangeListener(mFeedReliabilityLoggingSignals);
        }

        mVoiceRecognitionHandler = mOmniboxStub.getVoiceRecognitionHandler();
        if (mVoiceRecognitionHandler != null) {
            mVoiceRecognitionHandler.addObserver(this);
            mNewTabPageLayout.updateActionButtonVisibility();
        }
    }

    @Override
    public void onVoiceAvailabilityImpacted() {
        mNewTabPageLayout.updateActionButtonVisibility();
    }

    /** Adds an observer to be notified on most visited tile clicks. */
    public void addMostVisitedTileClickObserver(MostVisitedTileClickObserver observer) {
        mMostVisitedTileClickObservers.addObserver(observer);
    }

    /** Removes the observer. */
    public void removeMostVisitedTileClickObserver(MostVisitedTileClickObserver observer) {
        mMostVisitedTileClickObservers.removeObserver(observer);
    }

    /**
     * Records UMA for the NTP being shown. This includes a fresh page load or being brought to the
     * foreground.
     */
    private void recordNTPShown() {
        mLastShownTimeNs = System.nanoTime();
        RecordUserAction.record("MobileNTPShown");
        mJankTracker.startTrackingScenario(JankScenario.NEW_TAB_PAGE);
        SuggestionsMetrics.recordSurfaceVisible();

        FeatureNotificationUtils.registerIPHCallback(FeatureType.VOICE_SEARCH,
                mNewTabPageLayout::maybeShowFeatureNotificationVoiceSearchIPH);
    }

    /** Records UMA for the NTP being hidden and the time spent on it. */
    private void recordNTPHidden() {
        mJankTracker.finishTrackingScenario(JankScenario.NEW_TAB_PAGE);
        RecordHistogram.recordMediumTimesHistogram("NewTabPage.TimeSpent",
                (System.nanoTime() - mLastShownTimeNs) / TimeUtils.NANOSECONDS_PER_MILLISECOND);
        SuggestionsMetrics.recordSurfaceHidden();
        FeatureNotificationUtils.unregisterIPHCallback(FeatureType.VOICE_SEARCH);
    }

    /**
     * Returns the value of the adapter scroll position that was stored in the last committed
     * navigation entry. Returns {@code RecyclerView.NO_POSITION} if there is no last committed
     * navigation entry, or if no data is found.
     * @param scrollPositionKey The key under which the scroll position has been stored in the
     *                          NavigationEntryExtraData.
     * @param tab A tab that is used to access the NavigationController and the NavigationEntry
     *            extras.
     * @return The adapter scroll position.
     */
    public static int getScrollPositionFromNavigationEntry(String scrollPositionKey, Tab tab) {
        return getIntFromNavigationEntry(scrollPositionKey, tab, RecyclerView.NO_POSITION);
    }

    /**
     * Returns an arbitrary int value stored in the last committed navigation entry. If some step
     * fails then the default is returned instead.
     * @param key The string previously used to tag this piece of data.
     * @param tab A tab that is used to access the NavigationController and the NavigationEntry
     *            extras.
     * @param defaultValue The value to return if lookup or parsing is unsuccessful.
     * @return The value for the given key.
     *
     * TODO(https://crbug.com/941581): Refactor this to be reusable across NativePage components.
     */
    private static int getIntFromNavigationEntry(String key, Tab tab, int defaultValue) {
        if (tab.getWebContents() == null) return defaultValue;

        String stringValue = getStringFromNavigationEntry(tab, key);
        if (stringValue == null || stringValue.isEmpty()) {
            return RecyclerView.NO_POSITION;
        }

        try {
            return Integer.parseInt(stringValue);
        } catch (NumberFormatException e) {
            Log.w(TAG, "Bad data found for %s : %s", key, stringValue, e);
            return RecyclerView.NO_POSITION;
        }
    }

    /**
     * Returns an arbitrary string value stored in the last committed navigation entry. If the look
     * up fails, an empty string is returned.
     * @param tab A tab that is used to access the NavigationController and the NavigationEntry
     *            extras.
     * @param key The string previously used to tag this piece of data.
     * @return The value previously stored with the given key.
     *
     * TODO(https://crbug.com/941581): Refactor this to be reusable across NativePage components.
     */
    public static String getStringFromNavigationEntry(Tab tab, String key) {
        if (tab.getWebContents() == null) return "";
        NavigationController controller = tab.getWebContents().getNavigationController();
        int index = controller.getLastCommittedEntryIndex();
        return controller.getEntryExtraData(index, key);
    }

    /**
     * @return Whether the NTP has finished loaded.
     */
    @VisibleForTesting
    public boolean isLoadedForTests() {
        return mIsLoaded;
    }

    // TemplateUrlServiceObserver overrides

    @Override
    public void onTemplateURLServiceChanged() {
        onSearchEngineUpdated();
    }

    // NativePage overrides

    @Override
    public void destroy() {
        assert !mIsDestroyed;
        assert !ViewCompat
                .isAttachedToWindow(getView()) : "Destroy called before removed from window";
        if (mIsLoaded && !mTab.isHidden()) recordNTPHidden();

        mNewTabPageManager.onDestroy();
        mTileGroupDelegate.destroy();
        TemplateUrlServiceFactory.get().removeObserver(this);
        mTab.removeObserver(mTabObserver);
        mTabObserver = null;
        mActivityLifecycleDispatcher.unregister(mLifecycleObserver);
        mLifecycleObserver = null;
        mBrowserControlsStateProvider.removeObserver(this);
        if (mOmniboxStub != null) {
            mOmniboxStub.removeUrlFocusChangeListener(mFeedReliabilityLoggingSignals);
        }
        mFeedSurfaceProvider.destroy();
        mTab.getWindowAndroid().removeContextMenuCloseListener(mContextMenuManager);
        if (mVoiceRecognitionHandler != null) {
            mVoiceRecognitionHandler.removeObserver(this);
        }
        mIsDestroyed = true;
    }

    @Override
    public String getUrl() {
        return UrlConstants.NTP_URL;
    }

    @Override
    public String getTitle() {
        return mTitle;
    }

    @Override
    public int getBackgroundColor() {
        return mBackgroundColor;
    }

    @Override
    public @ColorInt int getToolbarTextBoxBackgroundColor(@ColorInt int defaultColor) {
        if (isLocationBarShownInNTP()) {
            return isLocationBarScrolledToTopInNtp()
                    ? ChromeColors.getSurfaceColor(mContext, R.dimen.toolbar_text_box_elevation)
                    : ChromeColors.getPrimaryBackgroundColor(mContext, false);
        }
        return defaultColor;
    }

    @Override
    public @ColorInt int getToolbarSceneLayerBackground(@ColorInt int defaultColor) {
        return isLocationBarShownInNTP() ? getBackgroundColor() : defaultColor;
    }

    @Override
    public float getToolbarTextBoxAlpha(float defaultAlpha) {
        return isLocationBarShownInNTP() ? 0.f : defaultAlpha;
    }

    @Override
    public boolean needsToolbarShadow() {
        return !mSearchProviderHasLogo;
    }

    @Override
    public View getView() {
        return mFeedSurfaceProvider.getView();
    }

    @Override
    public String getHost() {
        return UrlConstants.NTP_HOST;
    }

    @Override
    public void updateForUrl(String url) {
    }

    // InvalidationAwareThumbnailProvider

    @Override
    public boolean shouldCaptureThumbnail() {
        return mNewTabPageLayout.shouldCaptureThumbnail()
                || mFeedSurfaceProvider.shouldCaptureThumbnail();
    }

    @Override
    public void captureThumbnail(Canvas canvas) {
        mNewTabPageLayout.onPreCaptureThumbnail();
        mFeedSurfaceProvider.captureThumbnail(canvas);
    }
    // Implements FeedSurfaceDelegate
    @Override
    public FeedSurfaceLifecycleManager createStreamLifecycleManager(
            Activity activity, SurfaceCoordinator coordinator) {
        return new NtpFeedSurfaceLifecycleManager(
                activity, mTab, (FeedSurfaceCoordinator) coordinator);
    }

    @Override
    public boolean onInterceptTouchEvent(MotionEvent ev) {
        return !(mTab != null && DeviceFormFactor.isWindowOnTablet(mTab.getWindowAndroid()))
                && (mOmniboxStub != null && mOmniboxStub.isUrlBarFocused());
    }

    @VisibleForTesting
    public FeedSurfaceCoordinator getCoordinatorForTesting() {
        return (FeedSurfaceCoordinator) mFeedSurfaceProvider;
    }

    @VisibleForTesting
    public void setFeedReliabilityLoggingSignalsForTesting(FeedReliabilityLoggingSignals signals) {
        mFeedReliabilityLoggingSignals = signals;
    }

    @VisibleForTesting
    public NewTabPageManager getNewTabPageManagerForTesting() {
        return mNewTabPageManager;
    }
}
