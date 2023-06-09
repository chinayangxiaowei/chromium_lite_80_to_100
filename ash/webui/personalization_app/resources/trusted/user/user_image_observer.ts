// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {Url} from 'chrome://resources/mojo/url/mojom/url.mojom-webui.js';

import {UserImageObserverInterface, UserImageObserverReceiver, UserProviderInterface} from '../personalization_app.mojom-webui.js';
import {PersonalizationStore} from '../personalization_store.js';
import {setIsCameraPresentAction, setProfileImageAction, setUserImageAction} from './user_actions.js';

import {getUserProvider} from './user_interface_provider.js';

/** @fileoverview listens for updates on user's avatar image. */

let instance: UserImageObserver|null = null;

/**
 * Listens for changes to user image and saves updates to PersonalizationStore.
 */
export class UserImageObserver implements UserImageObserverInterface {
  static initUserImageObserverIfNeeded(): void {
    if (!instance) {
      instance = new UserImageObserver();
    }
  }

  static shutdown() {
    if (instance) {
      instance.receiver_.$.close();
      instance = null;
    }
  }

  receiver_: UserImageObserverReceiver = this.initReceiver_(getUserProvider());

  private initReceiver_(userProvider: UserProviderInterface):
      UserImageObserverReceiver {
    const receiver = new UserImageObserverReceiver(this);
    userProvider.setUserImageObserver(receiver.$.bindNewPipeAndPassRemote());
    return receiver;
  }

  onUserImageChanged(image: Url) {
    const store = PersonalizationStore.getInstance();
    store.dispatch(setUserImageAction(image));
  }

  onUserProfileImageUpdated(profileImage: Url) {
    const store = PersonalizationStore.getInstance();
    store.dispatch(setProfileImageAction(profileImage));
  }

  onCameraPresenceCheckDone(isCameraPresent: boolean) {
    const store = PersonalizationStore.getInstance();
    store.dispatch(setIsCameraPresentAction(isCameraPresent));
  }
}
