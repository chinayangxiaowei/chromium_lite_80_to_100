// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_DNS_PUBLIC_UTIL_H_
#define NET_DNS_PUBLIC_UTIL_H_

#include <string>

#include "net/base/address_family.h"
#include "net/base/ip_endpoint.h"
#include "net/base/net_export.h"

namespace url {
class SchemeHostPort;
}

// Basic utility functions for interaction with MDNS and host resolution.
namespace net::dns_util {

// Gets the endpoint for the multicast group a socket should join to receive
// MDNS messages. Such sockets should also bind to the endpoint from
// GetMDnsReceiveEndPoint().
//
// This is also the endpoint messages should be sent to to send MDNS messages.
NET_EXPORT IPEndPoint GetMdnsGroupEndPoint(AddressFamily address_family);

// Gets the endpoint sockets should be bound to to receive MDNS messages. Such
// sockets should also join the multicast group from GetMDnsGroupEndPoint().
NET_EXPORT IPEndPoint GetMdnsReceiveEndPoint(AddressFamily address_family);

// Determine the new hostname for an HTTPS record query by performing "Port
// Prefix Naming" as defined by draft-ietf-dnsop-svcb-https-08, Section 9.1.
NET_EXPORT std::string GetNameForHttpsQuery(const url::SchemeHostPort& host);

}  // namespace net::dns_util

#endif  // NET_DNS_PUBLIC_UTIL_H_
