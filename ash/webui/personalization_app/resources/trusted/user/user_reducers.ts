// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {Actions} from '../personalization_actions.js';
import {ReducerFunction} from '../personalization_reducers.js';
import {PersonalizationState} from '../personalization_state.js';

import {UserActionName} from './user_actions.js';
import {UserState} from './user_state.js';

export function imageReducer(
    state: UserState['image'], action: Actions,
    _: PersonalizationState): UserState['image'] {
  switch (action.name) {
    case UserActionName.SET_USER_IMAGE:
      return action.image;
    default:
      return state;
  }
}

export function defaultUserImagesReducer(
    state: UserState['defaultUserImages'], action: Actions,
    _: PersonalizationState): UserState['defaultUserImages'] {
  switch (action.name) {
    case UserActionName.SET_DEFAULT_USER_IMAGES:
      return action.defaultUserImages;
    default:
      return state;
  }
}

export function infoReducer(
    state: UserState['info'], action: Actions,
    _: PersonalizationState): UserState['info'] {
  switch (action.name) {
    case UserActionName.SET_USER_INFO:
      return action.user_info;
    default:
      return state;
  }
}

export function profileImageReducer(
    state: UserState['profileImage'], action: Actions,
    _: PersonalizationState): UserState['profileImage'] {
  switch (action.name) {
    case UserActionName.SET_PROFILE_IMAGE:
      return action.profileImage;
    default:
      return state;
  }
}

export function isCameraPresentReducer(
    state: UserState['isCameraPresent'], action: Actions,
    _: PersonalizationState): UserState['isCameraPresent'] {
  switch (action.name) {
    case UserActionName.SET_IS_CAMERA_PRESENT:
      return action.isCameraPresent;
    default:
      return state;
  }
}

export const userReducers:
    {[K in keyof UserState]: ReducerFunction<UserState[K]>} = {
      defaultUserImages: defaultUserImagesReducer,
      image: imageReducer,
      info: infoReducer,
      profileImage: profileImageReducer,
      isCameraPresent: isCameraPresentReducer,
    };
