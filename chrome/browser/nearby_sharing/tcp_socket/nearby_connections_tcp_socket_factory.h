// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_NEARBY_SHARING_TCP_SOCKET_NEARBY_CONNECTIONS_TCP_SOCKET_FACTORY_H_
#define CHROME_BROWSER_NEARBY_SHARING_TCP_SOCKET_NEARBY_CONNECTIONS_TCP_SOCKET_FACTORY_H_

#include "ash/services/nearby/public/mojom/tcp_socket_factory.mojom.h"
#include "base/callback.h"
#include "base/memory/weak_ptr.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/system/data_pipe.h"
#include "net/base/ip_endpoint.h"
#include "services/network/public/mojom/network_context.mojom.h"
#include "services/network/public/mojom/tcp_socket.mojom.h"
#include "third_party/abseil-cpp/absl/types/optional.h"

namespace ash {
namespace nearby {
class TcpServerSocketPort;
}  // namespace nearby
}  // namespace ash

namespace net {
class AddressList;
struct MutableNetworkTrafficAnnotationTag;
class IPAddress;
}  // namespace net

// An implementation of the mojo service used to create TCP sockets for the
// Nearby Connections WifiLan medium. We guarantee that callbacks will not be
// invoked after this class is destroyed.
class NearbyConnectionsTcpSocketFactory
    : public sharing::mojom::TcpSocketFactory {
 public:
  using NetworkContextGetter =
      base::RepeatingCallback<network::mojom::NetworkContext*()>;

  explicit NearbyConnectionsTcpSocketFactory(
      NetworkContextGetter network_context_getter);
  NearbyConnectionsTcpSocketFactory(const NearbyConnectionsTcpSocketFactory&) =
      delete;
  NearbyConnectionsTcpSocketFactory& operator=(
      const NearbyConnectionsTcpSocketFactory&) = delete;
  ~NearbyConnectionsTcpSocketFactory() override;

  // sharing::mojom:TcpSocketFactory:
  void CreateTCPServerSocket(
      const net::IPAddress& local_addr,
      const ash::nearby::TcpServerSocketPort& port,
      uint32_t backlog,
      const net::MutableNetworkTrafficAnnotationTag& traffic_annotation,
      mojo::PendingReceiver<network::mojom::TCPServerSocket> receiver,
      CreateTCPServerSocketCallback callback) override;
  void CreateTCPConnectedSocket(
      const absl::optional<net::IPEndPoint>& local_addr,
      const net::AddressList& remote_addr_list,
      network::mojom::TCPConnectedSocketOptionsPtr tcp_connected_socket_options,
      const net::MutableNetworkTrafficAnnotationTag& traffic_annotation,
      mojo::PendingReceiver<network::mojom::TCPConnectedSocket> receiver,
      mojo::PendingRemote<network::mojom::SocketObserver> observer,
      CreateTCPConnectedSocketCallback callback) override;

 private:
  // Wrapper callbacks that are bound with weak pointers. Used to guarantee that
  // input callbacks are not invoked after this class is destroyed.
  void OnTcpServerSocketCreated(
      CreateTCPServerSocketCallback callback,
      int32_t result,
      const absl::optional<net::IPEndPoint>& local_addr);
  void OnTcpConnectedSocketCreated(
      CreateTCPConnectedSocketCallback callback,
      int32_t result,
      const absl::optional<net::IPEndPoint>& local_addr,
      const absl::optional<net::IPEndPoint>& peer_addr,
      mojo::ScopedDataPipeConsumerHandle receive_stream,
      mojo::ScopedDataPipeProducerHandle send_stream);

  NetworkContextGetter network_context_getter_;
  base::WeakPtrFactory<NearbyConnectionsTcpSocketFactory> weak_ptr_factory_{
      this};
};

#endif  // CHROME_BROWSER_NEARBY_SHARING_TCP_SOCKET_NEARBY_CONNECTIONS_TCP_SOCKET_FACTORY_H_
