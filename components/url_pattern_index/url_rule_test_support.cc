// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/url_pattern_index/url_rule_test_support.h"

#include "base/check.h"
#include "base/strings/string_piece.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace url_pattern_index {
namespace testing {

proto::UrlRule MakeUrlRule(const UrlPattern& url_pattern) {
  proto::UrlRule rule;

  rule.set_semantics(proto::RULE_SEMANTICS_BLOCKLIST);
  rule.set_source_type(proto::SOURCE_TYPE_ANY);
  rule.set_element_types(kAllElementTypes);

  rule.set_url_pattern_type(url_pattern.type());
  rule.set_anchor_left(url_pattern.anchor_left());
  rule.set_anchor_right(url_pattern.anchor_right());
  rule.set_match_case(url_pattern.match_case());
  rule.set_url_pattern(std::string(url_pattern.url_pattern()));

  return rule;
}

void AddInitiatorDomains(const std::vector<std::string>& initiator_domains,
                         proto::UrlRule* rule) {
  for (std::string domain_pattern : initiator_domains) {
    DCHECK(!domain_pattern.empty());
    auto* domain = rule->add_initiator_domains();
    if (domain_pattern[0] == '~') {
      domain_pattern.erase(0, 1);
      domain->set_exclude(true);
    }
    domain->set_domain(std::move(domain_pattern));
  }
}

url::Origin GetOrigin(base::StringPiece origin_string) {
  return !origin_string.empty() ? url::Origin::Create(GURL(origin_string))
                                : url::Origin();
}

bool IsThirdParty(const GURL& url, const url::Origin& first_party_origin) {
  return first_party_origin.opaque() ||
         !net::registry_controlled_domains::SameDomainOrHost(
             url, first_party_origin,
             net::registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES);
}

}  // namespace testing
}  // namespace url_pattern_index
