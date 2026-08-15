#include "project/udp_socket.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <ostream>

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

  
  std::string Endpoint::to_string() const
  {
    return address_ + ":" + std::to_string(port_);
  }

  std::ostream& operator<<(std::ostream& os, const Endpoint& endpoint)
  {
    os << endpoint.to_string();
    return os;
  }


  expected<UdpSocket, UdpError> UdpSocket::create()
  {
    int sockfd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0)
    {
      return unexpected(UdpError::SocketCreationFailed);
    }

    return UdpSocket(SocketHandle(sockfd));
  }

  expected<void, UdpError> UdpSocket::bind(std::string_view address, uint16_t port)
  {
    if (!is_valid())
    {
      return unexpected(UdpError::InvalidSocket);
    }

    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    
    if (inet_pton(AF_INET, address.data(), &addr.sin_addr) != 1)
    {
      return unexpected(UdpError::AddressResolutionFailed);
    }

    
    if (::bind(socket_.get(), reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0)
    {
      return unexpected(UdpError::BindFailed);
    }

    
    local_endpoint_ = Endpoint(std::string(address), port);

    return expected<void, UdpError>();
  }

  expected<size_t, UdpError> UdpSocket::send_to(const std::vector<uint8_t>& data, const Endpoint& endpoint)
  {
    return send_to(data.data(), data.size(), endpoint);
  }

  expected<size_t, UdpError> UdpSocket::send_to(const uint8_t* data, size_t size, const Endpoint& endpoint)
  {
    if (!is_valid())
    {
      return unexpected(UdpError::InvalidSocket);
    }

    if (!endpoint.is_valid())
    {
      return unexpected(UdpError::InvalidEndpoint);
    }

    
    struct sockaddr_in dest_addr;
    std::memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(endpoint.port());

    if (inet_pton(AF_INET, endpoint.address().c_str(), &dest_addr.sin_addr) != 1)
    {
      return unexpected(UdpError::AddressResolutionFailed);
    }

    
    ssize_t sent = ::sendto(socket_.get(), data, size, 0, reinterpret_cast<struct sockaddr*>(&dest_addr), sizeof(dest_addr));

    if (sent < 0)
    {
      return unexpected(UdpError::SendFailed);
    }

    return static_cast<size_t>(sent);
  }

  expected<std::pair<std::vector<uint8_t>, Endpoint>, UdpError> UdpSocket::receive_from()
  {
    
    constexpr size_t DEFAULT_MAX_SIZE = 65536;
    return receive_from(DEFAULT_MAX_SIZE);
  }

  expected<std::pair<std::vector<uint8_t>, Endpoint>, UdpError> UdpSocket::receive_from(size_t max_size)
  {
    if (!is_valid())
    {
      return unexpected(UdpError::InvalidSocket);
    }

    std::vector<uint8_t> buffer(max_size);

    struct sockaddr_in sender_addr;
    socklen_t sender_addr_len = sizeof(sender_addr);
    std::memset(&sender_addr, 0, sizeof(sender_addr));

    
    ssize_t received = ::recvfrom(
        socket_.get(), buffer.data(), buffer.size(), 0, reinterpret_cast<struct sockaddr*>(&sender_addr), &sender_addr_len);

    if (received < 0)
    {
      return unexpected(UdpError::ReceiveFailed);
    }

    
    buffer.resize(static_cast<size_t>(received));

    
    char sender_ip[INET_ADDRSTRLEN];
    if (inet_ntop(AF_INET, &sender_addr.sin_addr, sender_ip, INET_ADDRSTRLEN) == nullptr)
    {
      return unexpected(UdpError::AddressResolutionFailed);
    }

    Endpoint sender_endpoint(sender_ip, ntohs(sender_addr.sin_port));

    return std::make_pair(std::move(buffer), sender_endpoint);
  }

}  
