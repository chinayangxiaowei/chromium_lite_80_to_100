// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_SERVICES_SECURE_CHANNEL_FAKE_ONE_SHOT_TIMER_H_
#define ASH_SERVICES_SECURE_CHANNEL_FAKE_ONE_SHOT_TIMER_H_

#include "base/callback_forward.h"
#include "base/timer/mock_timer.h"
#include "base/unguessable_token.h"

namespace chromeos {

namespace secure_channel {

// Fake base::OneShotTimer implementation, which extends MockTimer and provides
// a mechanism for alerting its creator when it is destroyed.
class FakeOneShotTimer : public base::MockOneShotTimer {
 public:
  FakeOneShotTimer(base::OnceCallback<void(const base::UnguessableToken&)>
                       destructor_callback);

  FakeOneShotTimer(const FakeOneShotTimer&) = delete;
  FakeOneShotTimer& operator=(const FakeOneShotTimer&) = delete;

  ~FakeOneShotTimer() override;

  const base::UnguessableToken& id() const { return id_; }

 private:
  base::OnceCallback<void(const base::UnguessableToken&)> destructor_callback_;
  base::UnguessableToken id_;
};

}  // namespace secure_channel

}  // namespace chromeos

// TODO(https://crbug.com/1164001): remove after the migration is finished.
namespace ash::secure_channel {
using ::chromeos::secure_channel::FakeOneShotTimer;
}

#endif  // ASH_SERVICES_SECURE_CHANNEL_FAKE_ONE_SHOT_TIMER_H_
