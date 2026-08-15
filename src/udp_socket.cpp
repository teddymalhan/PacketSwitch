#include "project/udp_socket.hpp"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

#include <cstring>
#include <limits>
#include <ostream>

namespace
{
#ifdef _WIN32
  bool ensure_winsock() noexcept
  {
    struct Runtime
    {
      bool valid = false;
      Runtime()
      {
        WSADATA data{};
        valid = ::WSAStartup(MAKEWORD(2, 2), &data) == 0;
      }
      ~Runtime()
      {
        if (valid) ::WSACleanup();
      }
    };
    static Runtime runtime;
    return runtime.valid;
  }
#else
  bool ensure_winsock() noexcept { return true; }
#endif
}

namespace project
{
  const char* to_string(UdpError error) noexcept
  {
    switch (error)
    {
      case UdpError::SocketCreationFailed: return "Failed to create socket";
      case UdpError::BindFailed: return "Failed to bind socket";
      case UdpError::SendFailed: return "Failed to send data";
      case UdpError::ReceiveFailed: return "Failed to receive data";
      case UdpError::InvalidEndpoint: return "Invalid endpoint";
      case UdpError::AddressResolutionFailed: return "Failed to resolve address";
      case UdpError::InvalidSocket: return "Invalid socket";
      default: return "Unknown UDP error";
    }
  }

  std::string Endpoint::to_string() const { return address_ + ":" + std::to_string(port_); }
  std::ostream& operator<<(std::ostream& os, const Endpoint& endpoint) { return os << endpoint.to_string(); }

  expected<UdpSocket, UdpError> UdpSocket::create()
  {
    if (!ensure_winsock()) return unexpected(UdpError::SocketCreationFailed);
#ifdef _WIN32
    const SOCKET socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket == INVALID_SOCKET) return unexpected(UdpError::SocketCreationFailed);
    return UdpSocket(SocketHandle(static_cast<native_socket_handle>(socket)));
#else
    const int socket = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (socket < 0) return unexpected(UdpError::SocketCreationFailed);
    return UdpSocket(SocketHandle(socket));
#endif
  }

  expected<void, UdpError> UdpSocket::bind(std::string_view address, uint16_t port)
  {
    if (!is_valid()) return unexpected(UdpError::InvalidSocket);
    const std::string address_string(address);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (::inet_pton(AF_INET, address_string.c_str(), &addr.sin_addr) != 1)
      return unexpected(UdpError::AddressResolutionFailed);
#ifdef _WIN32
    if (::bind(static_cast<SOCKET>(socket_.get()), reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR)
#else
    if (::bind(socket_.get(), reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) < 0)
#endif
      return unexpected(UdpError::BindFailed);

    sockaddr_in actual{};
#ifdef _WIN32
    int actual_size = sizeof(actual);
    if (::getsockname(static_cast<SOCKET>(socket_.get()), reinterpret_cast<sockaddr*>(&actual), &actual_size) == SOCKET_ERROR)
#else
    socklen_t actual_size = sizeof(actual);
    if (::getsockname(socket_.get(), reinterpret_cast<sockaddr*>(&actual), &actual_size) < 0)
#endif
      return unexpected(UdpError::BindFailed);
    local_endpoint_ = Endpoint(address_string, ntohs(actual.sin_port));
    return {};
  }

  expected<size_t, UdpError> UdpSocket::send_to(const std::vector<uint8_t>& data, const Endpoint& endpoint)
  {
    return send_to(data.data(), data.size(), endpoint);
  }

  expected<size_t, UdpError> UdpSocket::send_to(const uint8_t* data, size_t size, const Endpoint& endpoint)
  {
    if (!is_valid()) return unexpected(UdpError::InvalidSocket);
    if (!endpoint.is_valid()) return unexpected(UdpError::InvalidEndpoint);
    if (size > static_cast<size_t>(std::numeric_limits<int>::max())) return unexpected(UdpError::SendFailed);
    sockaddr_in dest{};
    dest.sin_family = AF_INET;
    dest.sin_port = htons(endpoint.port());
    if (::inet_pton(AF_INET, endpoint.address().c_str(), &dest.sin_addr) != 1)
      return unexpected(UdpError::AddressResolutionFailed);
#ifdef _WIN32
    const int sent = ::sendto(static_cast<SOCKET>(socket_.get()), reinterpret_cast<const char*>(data),
                              static_cast<int>(size), 0, reinterpret_cast<const sockaddr*>(&dest), sizeof(dest));
    if (sent == SOCKET_ERROR) return unexpected(UdpError::SendFailed);
#else
    const auto sent = ::sendto(socket_.get(), data, size, 0, reinterpret_cast<const sockaddr*>(&dest), sizeof(dest));
    if (sent < 0) return unexpected(UdpError::SendFailed);
#endif
    return static_cast<size_t>(sent);
  }

  expected<std::pair<std::vector<uint8_t>, Endpoint>, UdpError> UdpSocket::receive_from()
  {
    return receive_from(65536);
  }

  expected<std::pair<std::vector<uint8_t>, Endpoint>, UdpError> UdpSocket::receive_from(size_t max_size)
  {
    if (!is_valid()) return unexpected(UdpError::InvalidSocket);
    if (max_size > static_cast<size_t>(std::numeric_limits<int>::max()))
      return unexpected(UdpError::ReceiveFailed);
    std::vector<uint8_t> buffer(max_size);
    sockaddr_in sender{};
#ifdef _WIN32
    int sender_size = sizeof(sender);
    const int received = ::recvfrom(static_cast<SOCKET>(socket_.get()), reinterpret_cast<char*>(buffer.data()),
                                    static_cast<int>(buffer.size()), 0, reinterpret_cast<sockaddr*>(&sender), &sender_size);
    if (received == SOCKET_ERROR) return unexpected(UdpError::ReceiveFailed);
#else
    socklen_t sender_size = sizeof(sender);
    const auto received = ::recvfrom(socket_.get(), buffer.data(), buffer.size(), 0,
                                     reinterpret_cast<sockaddr*>(&sender), &sender_size);
    if (received < 0) return unexpected(UdpError::ReceiveFailed);
#endif
    buffer.resize(static_cast<size_t>(received));
    char sender_ip[INET_ADDRSTRLEN]{};
    if (::inet_ntop(AF_INET, &sender.sin_addr, sender_ip, sizeof(sender_ip)) == nullptr)
      return unexpected(UdpError::AddressResolutionFailed);
    return std::make_pair(std::move(buffer), Endpoint(sender_ip, ntohs(sender.sin_port)));
  }
}
