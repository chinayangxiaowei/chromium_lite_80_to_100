// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_QUICK_PAIR_UI_FAST_PAIR_FAST_PAIR_PRESENTER_H_
#define ASH_QUICK_PAIR_UI_FAST_PAIR_FAST_PAIR_PRESENTER_H_

#include "ash/quick_pair/ui/actions.h"
#include "base/callback.h"

namespace message_center {
class MessageCenter;
}  // namespace message_center

namespace ash {
namespace quick_pair {

struct Device;
class DeviceMetadata;

using DiscoveryCallback = base::RepeatingCallback<void(DiscoveryAction)>;
using PairingFailedCallback =
    base::RepeatingCallback<void(PairingFailedAction)>;
using AssociateAccountCallback =
    base::RepeatingCallback<void(AssociateAccountAction)>;
using CompanionAppCallback = base::RepeatingCallback<void(CompanionAppAction)>;

class FastPairPresenter {
 public:
  virtual void ShowDiscovery(scoped_refptr<Device> device,
                             DiscoveryCallback callback) = 0;
  virtual void ShowPairing(scoped_refptr<Device> device) = 0;
  virtual void ShowPairingFailed(scoped_refptr<Device> device,
                                 PairingFailedCallback callback) = 0;
  virtual void ShowAssociateAccount(scoped_refptr<Device> device,
                                    AssociateAccountCallback callback) = 0;
  virtual void ShowCompanionApp(scoped_refptr<Device> device,
                                CompanionAppCallback callback) = 0;
  virtual void RemoveNotifications(scoped_refptr<Device> device) = 0;

  virtual ~FastPairPresenter() = default;
};

}  // namespace quick_pair
}  // namespace ash

#endif  // ASH_QUICK_PAIR_UI_FAST_PAIR_FAST_PAIR_PRESENTER_H_
