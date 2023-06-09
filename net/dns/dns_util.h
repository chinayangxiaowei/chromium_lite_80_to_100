// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_DNS_DNS_UTIL_H_
#define NET_DNS_DNS_UTIL_H_

#include <string>
#include <vector>

#include "base/strings/string_piece.h"
#include "base/strings/string_util.h"
#include "base/time/time.h"
#include "net/base/address_family.h"
#include "net/base/ip_endpoint.h"
#include "net/base/net_export.h"
#include "net/base/network_change_notifier.h"
#include "net/dns/public/dns_over_https_server_config.h"
#include "net/dns/public/dns_query_type.h"
#include "net/dns/public/secure_dns_mode.h"
#include "third_party/abseil-cpp/absl/types/optional.h"

namespace base {
class BigEndianReader;
}  // namespace base

namespace net {

// DNSDomainFromDot - convert a domain string to DNS format. From DJB's
// public domain DNS library. |dotted| may include only characters a-z, A-Z,
// 0-9, -, and _.
//
//   dotted: a string in dotted form: "www.google.com"
//   out: a result in DNS form: "\x03www\x06google\x03com\x00"
NET_EXPORT bool DNSDomainFromDot(const base::StringPiece& dotted,
                                 std::string* out);

// DNSDomainFromUnrestrictedDot - convert a domain string to DNS format. Adapted
// from DJB's public domain DNS library. No validation of the characters in
// |dotted| is performed.
//
//   dotted: a string in dotted form: "Foo Printer._tcp.local"
//   out: a result in DNS form: "\x0bFoo Printer\x04_tcp\x05local\x00"
NET_EXPORT bool DNSDomainFromUnrestrictedDot(const base::StringPiece& dotted,
                                             std::string* out);

// Checks that a hostname is valid. Simple wrapper around DNSDomainFromDot.
NET_EXPORT_PRIVATE bool IsValidDNSDomain(const base::StringPiece& dotted);

// Checks that a hostname is valid. Simple wrapper around
// DNSDomainFromUnrestrictedDot.
NET_EXPORT_PRIVATE bool IsValidUnrestrictedDNSDomain(
    const base::StringPiece& dotted);

// Returns true if the character is valid in a DNS hostname label, whether in
// the first position or later in the label.
//
// This function asserts a looser form of the restrictions in RFC 7719 (section
// 2; https://tools.ietf.org/html/rfc7719#section-2): hostnames can include
// characters a-z, A-Z, 0-9, -, and _, and any of those characters (except -)
// are legal in the first position. The looser rules are necessary to support
// service records (initial _), and non-compliant but attested hostnames that
// include _. These looser rules also allow Punycode and hence IDN.
//
// TODO(crbug.com/1065133): In the future, when we can remove support for
// invalid names, this can be a private implementation detail of
// |DNSDomainFromDot|, and need not be NET_EXPORT_PRIVATE.
NET_EXPORT_PRIVATE bool IsValidHostLabelCharacter(char c, bool is_first_char);

// Converts a domain in DNS format to a dotted string. Excludes the dot at the
// end.  Returns nullopt on malformed input.
//
// If `require_complete` is true, input will be considered malformed if it does
// not contain a terminating zero-length label. If false, assumes the standard
// terminating zero-length label at the end if not included in the input.
//
// DNS name compression (see RFC 1035, section 4.1.4) is disallowed and
// considered malformed. To handle a potentially compressed name, in a
// DnsResponse object, use DnsRecordParser::ReadName().
NET_EXPORT absl::optional<std::string> DnsDomainToString(
    base::StringPiece dns_name,
    bool require_complete = false);
NET_EXPORT absl::optional<std::string> DnsDomainToString(
    base::BigEndianReader& reader,
    bool require_complete = false);

// Return the expanded template when no variables have corresponding values.
NET_EXPORT_PRIVATE std::string GetURLFromTemplateWithoutParameters(
    const std::string& server_template);

NET_EXPORT_PRIVATE
base::TimeDelta GetTimeDeltaForConnectionTypeFromFieldTrialOrDefault(
    const char* field_trial_name,
    base::TimeDelta default_delta,
    NetworkChangeNotifier::ConnectionType connection_type);

// Creates a 2-byte string that represents the name pointer defined in Section
// 4.1.1 of RFC 1035 for the given offset. The first two bits in the first byte
// of the name pointer are ones, and the rest 14 bits are given to |offset|,
// which specifies an offset from the start of the message for the pointed name.
// Note that |offset| must be less than 2^14 - 1 by definition.
NET_EXPORT std::string CreateNamePointer(uint16_t offset);

// Convert a DnsQueryType enum to the wire format integer representation.
NET_EXPORT_PRIVATE uint16_t DnsQueryTypeToQtype(DnsQueryType dns_query_type);

NET_EXPORT DnsQueryType
AddressFamilyToDnsQueryType(AddressFamily address_family);

// Uses the hardcoded upgrade mapping to discover DoH service(s) associated
// with a DoT hostname. Providers listed in |excluded_providers| are not
// eligible for upgrade.
NET_EXPORT_PRIVATE std::vector<DnsOverHttpsServerConfig>
GetDohUpgradeServersFromDotHostname(
    const std::string& dot_server,
    const std::vector<std::string>& excluded_providers);

// Uses the hardcoded upgrade mapping to discover DoH service(s) associated
// with a list of insecure DNS servers. Server ordering is preserved across
// the mapping. Providers listed in |excluded_providers| are not
// eligible for upgrade.
NET_EXPORT_PRIVATE std::vector<DnsOverHttpsServerConfig>
GetDohUpgradeServersFromNameservers(
    const std::vector<IPEndPoint>& dns_servers,
    const std::vector<std::string>& excluded_providers);

// Returns the provider id to use in UMA histogram names. If there is no
// provider id that matches |doh_server|, returns "Other".
NET_EXPORT_PRIVATE std::string GetDohProviderIdForHistogramFromServerConfig(
    const DnsOverHttpsServerConfig& doh_server);

// Returns the provider id to use in UMA histogram names. If there is no
// provider id that matches |nameserver|, returns "Other".
NET_EXPORT_PRIVATE std::string GetDohProviderIdForHistogramFromNameserver(
    const IPEndPoint& nameserver);

NET_EXPORT_PRIVATE std::string SecureDnsModeToString(
    const SecureDnsMode secure_dns_mode);

// std::map-compliant Compare for two dotted-format domain names. Returns true
// iff `lhs` is before `rhs` in strict weak ordering.
class NET_EXPORT_PRIVATE DomainNameComparator {
 public:
  bool operator()(base::StringPiece lhs, base::StringPiece rhs) const {
    return base::CompareCaseInsensitiveASCII(lhs, rhs) < 0;
  }
};

}  // namespace net

#endif  // NET_DNS_DNS_UTIL_H_
