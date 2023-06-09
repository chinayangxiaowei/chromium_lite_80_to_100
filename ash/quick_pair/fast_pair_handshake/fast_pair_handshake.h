// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_QUICK_PAIR_FAST_PAIR_HANDSHAKE_FAST_PAIR_HANDSHAKE_H_
#define ASH_QUICK_PAIR_FAST_PAIR_HANDSHAKE_FAST_PAIR_HANDSHAKE_H_

#include <memory>

#include "ash/quick_pair/common/pair_failure.h"
#include "base/callback.h"
#include "base/memory/scoped_refptr.h"
#include "third_party/abseil-cpp/absl/types/optional.h"

namespace device {
class BluetoothAdapter;
}  // namespace device

namespace ash {
namespace quick_pair {

struct Device;
class FastPairDataEncryptor;
class FastPairGattServiceClient;

// This class performs the Fast Pair handshake procedure upon creation and
// calls |on_complete| once finished. It also exposes the
// |FastPairDataEncryptor| and |FastPairGattServiceClient| instances that were
// used during the handshake.
//
// The procedure steps are as follows:
//  1. Create a GATT connection to the device.
//  2. Create a data encryptor instance with the appropriate keys.
//  3. Write the Key-Based Pairing Request to the characteristic
//  (https://developers.google.com/nearby/fast-pair/spec#table1.1)
//  4. Decrypt the response.
//  5. Validate response.
//  6. Set classic address field on |Device| instance.
//  7. Complete.
class FastPairHandshake {
 public:
  using OnCompleteCallback =
      base::OnceCallback<void(scoped_refptr<Device>,
                              absl::optional<PairFailure>)>;

  FastPairHandshake(
      scoped_refptr<device::BluetoothAdapter> adapter,
      scoped_refptr<Device> device,
      OnCompleteCallback on_complete,
      std::unique_ptr<FastPairDataEncryptor> data_encryptor,
      std::unique_ptr<FastPairGattServiceClient> gatt_service_client);
  FastPairHandshake(const FastPairHandshake&) = delete;
  FastPairHandshake& operator=(const FastPairHandshake&) = delete;
  virtual ~FastPairHandshake();

  bool completed_successfully() { return completed_successfully_; }

  FastPairDataEncryptor* fast_pair_data_encryptor() {
    return fast_pair_data_encryptor_.get();
  }

  // Returns whether or not this handshake has an active GATT connection.
  virtual bool IsConnected() = 0;

 protected:
  bool completed_successfully_ = false;
  scoped_refptr<device::BluetoothAdapter> adapter_;
  scoped_refptr<Device> device_;
  OnCompleteCallback on_complete_callback_;
  std::unique_ptr<FastPairDataEncryptor> fast_pair_data_encryptor_;
  std::unique_ptr<FastPairGattServiceClient> fast_pair_gatt_service_client_;
};

}  // namespace quick_pair
}  // namespace ash

#endif  // ASH_QUICK_PAIR_FAST_PAIR_HANDSHAKE_FAST_PAIR_HANDSHAKE_H_
