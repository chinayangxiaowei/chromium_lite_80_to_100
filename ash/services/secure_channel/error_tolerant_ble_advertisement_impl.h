// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_SERVICES_SECURE_CHANNEL_ERROR_TOLERANT_BLE_ADVERTISEMENT_IMPL_H_
#define ASH_SERVICES_SECURE_CHANNEL_ERROR_TOLERANT_BLE_ADVERTISEMENT_IMPL_H_

#include <memory>
#include <string>

// TODO(https://crbug.com/1164001): move to forward declaration.
#include "ash/services/secure_channel/ble_synchronizer_base.h"
#include "ash/services/secure_channel/device_id_pair.h"
#include "ash/services/secure_channel/error_tolerant_ble_advertisement.h"
#include "ash/services/secure_channel/foreground_eid_generator.h"
#include "base/memory/weak_ptr.h"
#include "device/bluetooth/bluetooth_advertisement.h"

namespace chromeos {

namespace secure_channel {

// Concrete ErrorTolerantBleAdvertisement implementation.
class ErrorTolerantBleAdvertisementImpl
    : public ErrorTolerantBleAdvertisement,
      public device::BluetoothAdvertisement::Observer {
 public:
  class Factory {
   public:
    static std::unique_ptr<ErrorTolerantBleAdvertisement> Create(
        const DeviceIdPair& device_id_pair,
        std::unique_ptr<DataWithTimestamp> advertisement_data,
        BleSynchronizerBase* ble_synchronizer);
    static void SetFactoryForTesting(Factory* test_factory);

   protected:
    virtual ~Factory();
    virtual std::unique_ptr<ErrorTolerantBleAdvertisement> CreateInstance(
        const DeviceIdPair& device_id_pair,
        std::unique_ptr<DataWithTimestamp> advertisement_data,
        BleSynchronizerBase* ble_synchronizer) = 0;

   private:
    static Factory* test_factory_;
  };

  ErrorTolerantBleAdvertisementImpl(const ErrorTolerantBleAdvertisementImpl&) =
      delete;
  ErrorTolerantBleAdvertisementImpl& operator=(
      const ErrorTolerantBleAdvertisementImpl&) = delete;

  ~ErrorTolerantBleAdvertisementImpl() override;

 private:
  friend class SecureChannelErrorTolerantBleAdvertisementImplTest;

  ErrorTolerantBleAdvertisementImpl(
      const DeviceIdPair& device_id_pair,
      std::unique_ptr<DataWithTimestamp> advertisement_data,
      BleSynchronizerBase* ble_synchronizer);

  // ErrorTolerantBleAdvertisement:
  void Stop(base::OnceClosure callback) override;
  bool HasBeenStopped() override;

  // device::BluetoothAdvertisement::Observer
  void AdvertisementReleased(
      device::BluetoothAdvertisement* advertisement) override;

  void UpdateRegistrationStatus();
  void AttemptRegistration();
  void AttemptUnregistration();

  std::unique_ptr<device::BluetoothAdvertisement::UUIDList> CreateServiceUuids()
      const;

  std::unique_ptr<device::BluetoothAdvertisement::ServiceData>
  CreateServiceData() const;

  void OnAdvertisementRegistered(
      scoped_refptr<device::BluetoothAdvertisement> advertisement);
  void OnErrorRegisteringAdvertisement(
      device::BluetoothAdvertisement::ErrorCode error_code);
  void OnAdvertisementUnregistered();
  void OnErrorUnregisteringAdvertisement(
      device::BluetoothAdvertisement::ErrorCode error_code);

  const DataWithTimestamp& advertisement_data() const {
    return *advertisement_data_;
  }

  std::unique_ptr<DataWithTimestamp> advertisement_data_;
  BleSynchronizerBase* ble_synchronizer_;

  bool registration_in_progress_ = false;
  bool unregistration_in_progress_ = false;

  scoped_refptr<device::BluetoothAdvertisement> advertisement_;

  bool stopped_ = false;
  base::OnceClosure stop_callback_;

  base::WeakPtrFactory<ErrorTolerantBleAdvertisementImpl> weak_ptr_factory_{
      this};
};

}  // namespace secure_channel

}  // namespace chromeos

// TODO(https://crbug.com/1164001): remove after the migration is finished.
namespace ash::secure_channel {
using ::chromeos::secure_channel::ErrorTolerantBleAdvertisementImpl;
}

#endif  // ASH_SERVICES_SECURE_CHANNEL_ERROR_TOLERANT_BLE_ADVERTISEMENT_IMPL_H_
