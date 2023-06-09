// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/components/onc/onc_utils.h"

#include <string>

#include "base/check.h"
#include "base/files/file_path.h"
#include "base/json/json_file_value_serializer.h"
#include "base/json/json_reader.h"
#include "base/logging.h"
#include "base/notreached.h"
#include "base/path_service.h"
#include "base/strings/string_number_conversions.h"
#include "base/values.h"
#include "chromeos/components/onc/onc_signature.h"
#include "chromeos/components/onc/onc_test_utils.h"
#include "chromeos/components/onc/variable_expander.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace chromeos {
namespace onc {

TEST(ONCDecrypterTest, BrokenEncryptionIterations) {
  base::Value encrypted_onc =
      test_utils::ReadTestDictionaryValue("broken-encrypted-iterations.onc");

  base::Value decrypted_onc = Decrypt("test0000", encrypted_onc);

  EXPECT_TRUE(decrypted_onc.is_none());
}

TEST(ONCDecrypterTest, BrokenEncryptionZeroIterations) {
  base::Value encrypted_onc = test_utils::ReadTestDictionaryValue(
      "broken-encrypted-zero-iterations.onc");

  base::Value decrypted_onc = Decrypt("test0000", encrypted_onc);

  EXPECT_TRUE(decrypted_onc.is_none());
}

TEST(ONCDecrypterTest, LoadEncryptedOnc) {
  base::Value encrypted_onc =
      test_utils::ReadTestDictionaryValue("encrypted.onc");
  base::Value expected_decrypted_onc =
      test_utils::ReadTestDictionaryValue("decrypted.onc");

  std::string error;
  base::Value actual_decrypted_onc = Decrypt("test0000", encrypted_onc);

  base::Value emptyDict;
  EXPECT_TRUE(
      test_utils::Equals(&expected_decrypted_onc, &actual_decrypted_onc));
}

namespace {

const char* kLoginId = "hans";
const char* kLoginEmail = "hans@my.domain.com";

std::map<std::string, std::string> GetTestStringSubstutions() {
  std::map<std::string, std::string> substitutions;
  substitutions[::onc::substitutes::kLoginID] = kLoginId;
  substitutions[::onc::substitutes::kLoginEmail] = kLoginEmail;
  return substitutions;
}

}  // namespace

TEST(ONCStringExpansion, OpenVPN) {
  base::Value vpn_onc =
      test_utils::ReadTestDictionaryValue("valid_openvpn.onc");

  VariableExpander variable_expander(GetTestStringSubstutions());
  ExpandStringsInOncObject(kNetworkConfigurationSignature, variable_expander,
                           &vpn_onc);

  std::string* actual_expanded = vpn_onc.FindStringPath("VPN.OpenVPN.Username");
  ASSERT_TRUE(actual_expanded);
  EXPECT_EQ(*actual_expanded, std::string("abc ") + kLoginEmail + " def");
}

TEST(ONCStringExpansion, WiFi_EAP) {
  base::Value wifi_onc =
      test_utils::ReadTestDictionaryValue("wifi_clientcert_with_cert_pems.onc");

  VariableExpander variable_expander(GetTestStringSubstutions());
  ExpandStringsInOncObject(kNetworkConfigurationSignature, variable_expander,
                           &wifi_onc);

  std::string* actual_expanded = wifi_onc.FindStringPath("WiFi.EAP.Identity");
  ASSERT_TRUE(actual_expanded);
  EXPECT_EQ(*actual_expanded,
            std::string("abc ") + kLoginId + "@my.domain.com");
}

TEST(ONCResolveServerCertRefs, ResolveServerCertRefs) {
  base::Value test_cases = test_utils::ReadTestDictionaryValue(
      "network_configs_with_resolved_certs.json");

  CertPEMsByGUIDMap certs;
  certs["cert_google"] = "pem_google";
  certs["cert_webkit"] = "pem_webkit";

  for (auto iter : test_cases.DictItems()) {
    SCOPED_TRACE("Test case: " + iter.first);

    const base::Value& test_case = iter.second;
    ASSERT_TRUE(test_case.is_dict());

    const base::Value* networks_with_cert_refs =
        test_case.FindListKey("WithCertRefs");
    ASSERT_TRUE(networks_with_cert_refs);
    const base::Value* expected_resolved_onc =
        test_case.FindListKey("WithResolvedRefs");
    ASSERT_TRUE(expected_resolved_onc);

    bool expected_success =
        (networks_with_cert_refs->GetListDeprecated().size() ==
         expected_resolved_onc->GetListDeprecated().size());

    base::Value actual_resolved_onc(networks_with_cert_refs->Clone());
    bool success = ResolveServerCertRefsInNetworks(certs, &actual_resolved_onc);
    EXPECT_EQ(expected_success, success);
    EXPECT_TRUE(
        test_utils::Equals(expected_resolved_onc, &actual_resolved_onc));
  }
}

TEST(ONCUtils, SetHiddenSSIDField_WithNoValueSet) {
  // WiFi configuration that doesn't have HiddenSSID field set.
  base::Value wifi_onc =
      test_utils::ReadTestDictionaryValue("wifi_clientcert_with_cert_pems.onc");
  base::Value* wifi_fields = wifi_onc.FindDictKey("WiFi");
  ASSERT_TRUE(wifi_fields);

  ASSERT_FALSE(wifi_fields->FindKey(::onc::wifi::kHiddenSSID));
  SetHiddenSSIDField(wifi_fields);
  base::Value* hidden_ssid_field =
      wifi_fields->FindKey(::onc::wifi::kHiddenSSID);
  ASSERT_TRUE(hidden_ssid_field);
  EXPECT_FALSE(hidden_ssid_field->GetBool());
}

TEST(ONCUtils, SetHiddenSSIDField_WithValueSetFalse) {
  // WiFi configuration that have HiddenSSID field set to false.
  base::Value wifi_onc = test_utils::ReadTestDictionaryValue(
      "translation_of_shill_wifi_with_state.onc");
  base::Value* wifi_fields = wifi_onc.FindDictKey("WiFi");
  ASSERT_TRUE(wifi_fields);

  ASSERT_TRUE(wifi_fields->FindKey(::onc::wifi::kHiddenSSID));
  SetHiddenSSIDField(wifi_fields);
  EXPECT_FALSE(wifi_fields->FindKey(::onc::wifi::kHiddenSSID)->GetBool());
}

TEST(ONCUtils, SetHiddenSSIDField_WithValueSetTrue) {
  // WiFi configuration that have HiddenSSID field set to true.
  base::Value wifi_onc =
      test_utils::ReadTestDictionaryValue("wifi_with_hidden_ssid.onc");
  base::Value* wifi_fields = wifi_onc.FindDictKey("WiFi");
  ASSERT_TRUE(wifi_fields);

  ASSERT_TRUE(wifi_fields->FindKey(::onc::wifi::kHiddenSSID));
  SetHiddenSSIDField(wifi_fields);
  EXPECT_TRUE(wifi_fields->FindKey(::onc::wifi::kHiddenSSID)->GetBool());
}

}  // namespace onc
}  // namespace chromeos
