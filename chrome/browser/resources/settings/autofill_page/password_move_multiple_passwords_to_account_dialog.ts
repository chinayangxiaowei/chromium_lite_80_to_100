// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview 'password-move-multiple-passwords-to-account-dialog' is the
 * dialog that allows moving multiple passwords stored on the user device to the
 * account.
 */

import './avatar_icon.js';
import './password_list_item.js';
import 'chrome://resources/cr_elements/cr_button/cr_button.m.js';
import 'chrome://resources/cr_elements/cr_checkbox/cr_checkbox.m.js';
import 'chrome://resources/cr_elements/cr_dialog/cr_dialog.m.js';

import {CrDialogElement} from 'chrome://resources/cr_elements/cr_dialog/cr_dialog.m.js';
import {PolymerElement} from 'chrome://resources/polymer/v3_0/polymer/polymer_bundled.min.js';

import {MultiStorePasswordUiEntry} from './multi_store_password_ui_entry.js';
import {PasswordManagerImpl} from './password_manager_proxy.js';
import {getTemplate} from './password_move_multiple_passwords_to_account_dialog.html.js';
import {MoveToAccountStoreTrigger} from './password_move_to_account_dialog.js';

export interface PasswordMoveMultiplePasswordsToAccountDialogElement {
  $: {
    dialog: CrDialogElement,
    moveButton: HTMLElement,
    cancelButton: HTMLElement,
  };
}

export class PasswordMoveMultiplePasswordsToAccountDialogElement extends
    PolymerElement {
  static get is() {
    return 'password-move-multiple-passwords-to-account-dialog';
  }

  static get template() {
    return getTemplate();
  }

  static get properties() {
    return {
      passwordsToMove: {
        type: Array,
        value: () => [],
      },

      accountEmail: String,
    };
  }

  passwordsToMove: Array<MultiStorePasswordUiEntry>;
  accountEmail: string;

  /** @return Whether the user confirmed the dialog. */
  wasConfirmed(): boolean {
    return this.$.dialog.getNative().returnValue === 'success';
  }

  connectedCallback() {
    super.connectedCallback();

    chrome.metricsPrivate.recordEnumerationValue(
        'PasswordManager.AccountStorage.MoveToAccountStoreFlowOffered',
        MoveToAccountStoreTrigger
            .EXPLICITLY_TRIGGERED_FOR_MULTIPLE_PASSWORDS_IN_SETTINGS,
        MoveToAccountStoreTrigger.COUNT);
  }

  private onMoveButtonClick_() {
    const checkboxes = this.$.dialog.querySelectorAll('cr-checkbox');
    const selectedPasswords: Array<number> = [];
    checkboxes.forEach((checkbox) => {
      if (checkbox.checked) {
        selectedPasswords.push(Number(checkbox.dataset['id']));
      }
    });
    PasswordManagerImpl.getInstance().movePasswordsToAccount(selectedPasswords);
    chrome.metricsPrivate.recordSmallCount(
        'PasswordManager.AccountStorage.MoveToAccountStorePasswordsCount',
        selectedPasswords.length);
    this.$.dialog.close();
  }

  private onCancelButtonClick_() {
    this.$.dialog.cancel();
  }
}

declare global {
  interface HTMLElementTagNameMap {
    'password-move-multiple-passwords-to-account-dialog':
        PasswordMoveMultiplePasswordsToAccountDialogElement;
  }
}

customElements.define(
    PasswordMoveMultiplePasswordsToAccountDialogElement.is,
    PasswordMoveMultiplePasswordsToAccountDialogElement);
