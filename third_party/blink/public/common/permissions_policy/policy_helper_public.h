// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_PUBLIC_COMMON_PERMISSIONS_POLICY_POLICY_HELPER_PUBLIC_H_
#define THIRD_PARTY_BLINK_PUBLIC_COMMON_PERMISSIONS_POLICY_POLICY_HELPER_PUBLIC_H_

#include "base/containers/flat_map.h"
#include "base/strings/string_piece.h"
#include "third_party/blink/public/common/permissions_policy/permissions_policy.h"
#include "third_party/blink/public/mojom/permissions_policy/permissions_policy.mojom-shared.h"

namespace blink {

// TODO(crbug.com/1297609): Rename to FeatureNameMap and
// GetDefaultFeatureNameMap() after removing the private version of
// GetDefaultFeatureNameMap().
using PublicFeatureNameMap =
    base::flat_map<base::StringPiece, mojom::PermissionsPolicyFeature>;

// This method defines the feature names which will be recognized by the parser
// for the Permissions-Policy HTTP header and the <iframe> "allow" attribute, as
// well as the features which will be recognized by the document or iframe
// policy object.
const PublicFeatureNameMap& GetDefaultFeatureNameMapPublic();

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_PUBLIC_COMMON_PERMISSIONS_POLICY_POLICY_HELPER_PUBLIC_H_
