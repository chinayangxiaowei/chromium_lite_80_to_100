// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_SERVICES_SECURE_CHANNEL_PUBLIC_CPP_CLIENT_PRESENCE_MONITOR_CLIENT_H_
#define ASH_SERVICES_SECURE_CHANNEL_PUBLIC_CPP_CLIENT_PRESENCE_MONITOR_CLIENT_H_

#include "ash/services/secure_channel/public/cpp/shared/presence_monitor.h"
#include "chromeos/components/multidevice/remote_device_ref.h"

namespace chromeos {
namespace secure_channel {

// Provides clients access to the PresenceMonitor API.
class PresenceMonitorClient {
 public:
  virtual ~PresenceMonitorClient() = default;

  virtual void SetPresenceMonitorCallbacks(
      PresenceMonitor::ReadyCallback ready_callback,
      PresenceMonitor::DeviceSeenCallback device_seen_callback) = 0;
  virtual void StartMonitoring(
      const multidevice::RemoteDeviceRef& remote_device_ref,
      const multidevice::RemoteDeviceRef& local_device_ref) = 0;
  virtual void StopMonitoring() = 0;

 protected:
  PresenceMonitorClient() = default;

 private:
  PresenceMonitorClient(const PresenceMonitorClient&) = delete;
  PresenceMonitorClient& operator=(const PresenceMonitorClient&) = delete;
};

}  // namespace secure_channel
}  // namespace chromeos

// TODO(https://crbug.com/1164001): remove after the //chrome/browser/chromeos
// source migration is finished.
namespace ash {
namespace secure_channel {
using ::chromeos::secure_channel::PresenceMonitorClient;
}
}  // namespace ash

#endif  // ASH_SERVICES_SECURE_CHANNEL_PUBLIC_CPP_CLIENT_PRESENCE_MONITOR_CLIENT_H_
