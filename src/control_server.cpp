#include "wirelab/control_server.hpp"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <cerrno>
#include <limits>
#include <utility>

namespace
{
#ifdef _WIN32
  using PollDescriptor = WSAPOLLFD;
  using SocketLength = int;
  constexpr short POLL_READ = POLLRDNORM;
  constexpr short POLL_WRITE = POLLWRNORM;

  int poll_descriptors(PollDescriptor* descriptors, size_t count, int timeout) noexcept
  {
    return ::WSAPoll(descriptors, static_cast<ULONG>(count), timeout);
  }

  bool would_block() noexcept
  {
    return ::WSAGetLastError() == WSAEWOULDBLOCK;
  }
#else
  using PollDescriptor = pollfd;
  using SocketLength = socklen_t;
  constexpr short POLL_READ = POLLIN;
  constexpr short POLL_WRITE = POLLOUT;

  int poll_descriptors(PollDescriptor* descriptors, size_t count, int timeout) noexcept
  {
    return ::poll(descriptors, static_cast<nfds_t>(count), timeout);
  }

  bool would_block() noexcept
  {
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
  }
#endif

  constexpr int send_flags() noexcept
  {
#ifdef MSG_NOSIGNAL
    return MSG_NOSIGNAL;
#else
    return 0;
#endif
  }

  int poll_deadline(std::chrono::milliseconds timeout) noexcept
  {
    const auto milliseconds = timeout.count() < 0 ? 0 : timeout.count();
    return milliseconds > std::numeric_limits<int>::max() ? std::numeric_limits<int>::max() : static_cast<int>(milliseconds);
  }

  bool set_non_blocking(wirelab::native_socket_handle socket) noexcept
  {
#ifdef _WIN32
    u_long mode = 1;
    return ::ioctlsocket(static_cast<SOCKET>(socket), FIONBIO, &mode) == 0;
#else
    const int flags = ::fcntl(socket, F_GETFL, 0);
    return flags >= 0 && ::fcntl(socket, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
  }

  // A control peer that vanishes must cost the switch a failed send, not a
  // signal that takes the whole process down with it.
  void suppress_sigpipe(wirelab::native_socket_handle socket) noexcept
  {
#ifdef SO_NOSIGPIPE
    int enabled = 1;
    (void)::setsockopt(socket, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled));
#else
    (void)socket;
#endif
  }

  // Replies are small and latency matters more than packing them, so a reply
  // must not wait on Nagle for a following one.
  void disable_nagle(wirelab::native_socket_handle socket) noexcept
  {
    int enabled = 1;
#ifdef _WIN32
    (void)::setsockopt(
        static_cast<SOCKET>(socket), IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&enabled), sizeof(enabled));
#else
    (void)::setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, &enabled, sizeof(enabled));
#endif
  }

  bool make_address(const std::string& address, uint16_t port, sockaddr_in& out) noexcept
  {
    out = sockaddr_in{};
    out.sin_family = AF_INET;
    out.sin_port = htons(port);
    return ::inet_pton(AF_INET, address.c_str(), &out.sin_addr) == 1;
  }

  long receive_some(wirelab::native_socket_handle socket, char* buffer, size_t size) noexcept
  {
#ifdef _WIN32
    return ::recv(static_cast<SOCKET>(socket), buffer, static_cast<int>(size), 0);
#else
    return static_cast<long>(::recv(socket, buffer, size, 0));
#endif
  }

  long send_some(wirelab::native_socket_handle socket, const char* data, size_t size) noexcept
  {
#ifdef _WIN32
    return ::send(static_cast<SOCKET>(socket), data, static_cast<int>(size), send_flags());
#else
    return static_cast<long>(::send(socket, data, size, send_flags()));
#endif
  }

  // Drains a socket into buffer. Returns false once the peer has closed or the
  // connection failed; a merely empty socket is a successful read of nothing.
  bool drain(wirelab::native_socket_handle socket, std::string& buffer, size_t cap)
  {
    for (;;)
    {
      char chunk[4096];
      const auto received = receive_some(socket, chunk, sizeof(chunk));
      if (received == 0)
      {
        return false;
      }
      if (received < 0)
      {
        return would_block();
      }
      buffer.append(chunk, static_cast<size_t>(received));
      if (buffer.size() > cap)
      {
        return false;
      }
    }
  }

  // Splits complete newline-terminated messages out of buffer, leaving any
  // partial tail for the next read.
  template<typename Handler>
  void take_messages(std::string& buffer, Handler&& handler)
  {
    size_t start = 0;
    for (auto newline = buffer.find('\n', start); newline != std::string::npos; newline = buffer.find('\n', start))
    {
      auto length = newline - start;
      if (length > 0 && buffer[start + length - 1] == '\r')
      {
        --length;
      }
      const std::string_view message(buffer.data() + start, length);
      start = newline + 1;
      if (!message.empty())
      {
        handler(message);
      }
    }
    buffer.erase(0, start);
  }

  // Takes exactly one message, leaving anything that followed it buffered. A
  // reader that asked for one event must not lose the events behind it.
  std::optional<std::string> take_message(std::string& buffer)
  {
    for (auto newline = buffer.find('\n'); newline != std::string::npos; newline = buffer.find('\n'))
    {
      auto length = newline;
      if (length > 0 && buffer[length - 1] == '\r')
      {
        --length;
      }
      std::string message = buffer.substr(0, length);
      buffer.erase(0, newline + 1);
      if (!message.empty())
      {
        return message;
      }
    }
    return std::nullopt;
  }
}  // namespace

namespace wirelab
{
  const char* to_string(ControlTransportError error) noexcept
  {
    switch (error)
    {
      case ControlTransportError::SocketCreationFailed: return "Failed to create control socket";
      case ControlTransportError::BindFailed: return "Failed to bind control socket";
      case ControlTransportError::ListenFailed: return "Failed to listen on control socket";
      case ControlTransportError::ConnectFailed: return "Failed to connect to control socket";
      case ControlTransportError::SendFailed: return "Failed to send control message";
      case ControlTransportError::ReceiveFailed: return "Failed to receive control message";
      case ControlTransportError::AddressResolutionFailed: return "Failed to resolve control address";
      case ControlTransportError::InvalidSocket: return "Invalid control socket";
      case ControlTransportError::Disconnected: return "Control connection closed";
    }
    return "Unknown control transport error";
  }

  ControlServer::ControlServer(
      ControlService& service,
      SocketHandle listener,
      uint16_t port,
      ControlServerConfig config) noexcept
      : service_(service),
        listener_(std::move(listener)),
        port_(port),
        config_(config)
  {
  }

  expected<ControlServer, ControlTransportError>
  ControlServer::create(ControlService& service, uint16_t port, std::string_view address, ControlServerConfig config)
  {
    if (!ensure_socket_runtime())
    {
      return unexpected(ControlTransportError::SocketCreationFailed);
    }

#ifdef _WIN32
    const SOCKET raw = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (raw == INVALID_SOCKET)
    {
      return unexpected(ControlTransportError::SocketCreationFailed);
    }
    SocketHandle listener(static_cast<native_socket_handle>(raw));
#else
    const int raw = ::socket(AF_INET, SOCK_STREAM, 0);
    if (raw < 0)
    {
      return unexpected(ControlTransportError::SocketCreationFailed);
    }
    SocketHandle listener(raw);
#endif

    // A switch restarted on the same control port must not be refused by the
    // previous listener's lingering socket.
    int reuse = 1;
#ifdef _WIN32
    (void)::setsockopt(
        static_cast<SOCKET>(listener.get()), SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));
#else
    (void)::setsockopt(listener.get(), SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#endif

    sockaddr_in local{};
    if (!make_address(std::string(address), port, local))
    {
      return unexpected(ControlTransportError::AddressResolutionFailed);
    }
#ifdef _WIN32
    if (::bind(static_cast<SOCKET>(listener.get()), reinterpret_cast<const sockaddr*>(&local), sizeof(local)) ==
        SOCKET_ERROR)
#else
    if (::bind(listener.get(), reinterpret_cast<const sockaddr*>(&local), sizeof(local)) < 0)
#endif
    {
      return unexpected(ControlTransportError::BindFailed);
    }

#ifdef _WIN32
    if (::listen(static_cast<SOCKET>(listener.get()), SOMAXCONN) == SOCKET_ERROR)
#else
    if (::listen(listener.get(), SOMAXCONN) < 0)
#endif
    {
      return unexpected(ControlTransportError::ListenFailed);
    }

    sockaddr_in bound{};
    SocketLength bound_size = sizeof(bound);
#ifdef _WIN32
    if (::getsockname(static_cast<SOCKET>(listener.get()), reinterpret_cast<sockaddr*>(&bound), &bound_size) == SOCKET_ERROR)
#else
    if (::getsockname(listener.get(), reinterpret_cast<sockaddr*>(&bound), &bound_size) < 0)
#endif
    {
      return unexpected(ControlTransportError::BindFailed);
    }

    if (!set_non_blocking(listener.get()))
    {
      return unexpected(ControlTransportError::SocketCreationFailed);
    }

    return ControlServer(service, std::move(listener), ntohs(bound.sin_port), config);
  }

  size_t ControlServer::poll(std::chrono::milliseconds timeout)
  {
    size_t served = 0;
    if (!listener_.is_valid())
    {
      return served;
    }

    std::vector<PollDescriptor> watched;
    watched.reserve(clients_.size() + 1);
    PollDescriptor listener_descriptor{};
    listener_descriptor.fd = listener_.get();
    listener_descriptor.events = POLL_READ;
    watched.push_back(listener_descriptor);
    for (const auto& client : clients_)
    {
      PollDescriptor descriptor{};
      descriptor.fd = client.socket.get();
      descriptor.events = static_cast<short>(POLL_READ | (client.outbox.empty() ? 0 : POLL_WRITE));
      watched.push_back(descriptor);
    }

    const int ready = poll_descriptors(watched.data(), watched.size(), poll_deadline(timeout));
    if (ready <= 0)
    {
      // A signal cutting the wait short is not an error: the switch loop calls
      // back on its next pass.
      return served;
    }

    // Accepting appends, so the descriptors already polled keep their indices.
    const size_t polled = clients_.size();
    if (watched[0].revents != 0)
    {
      accept_pending();
    }

    for (size_t index = polled; index-- > 0;)
    {
      const auto revents = watched[index + 1].revents;
      if (revents == 0)
      {
        continue;
      }

      auto& client = clients_[index];
      bool alive = (revents & (POLLERR | POLLNVAL)) == 0;
      if (alive && (revents & POLL_READ) != 0)
      {
        alive = serve(client, served);
      }
      if (alive && !client.outbox.empty())
      {
        alive = flush(client);
      }
      if (alive && (revents & POLLHUP) != 0 && client.inbox.empty())
      {
        alive = false;
      }
      if (!alive || client.congested)
      {
        clients_.erase(clients_.begin() + static_cast<std::ptrdiff_t>(index));
      }
    }
    return served;
  }

  void ControlServer::accept_pending()
  {
    for (;;)
    {
      sockaddr_in peer{};
      SocketLength peer_size = sizeof(peer);
#ifdef _WIN32
      const SOCKET raw = ::accept(static_cast<SOCKET>(listener_.get()), reinterpret_cast<sockaddr*>(&peer), &peer_size);
      if (raw == INVALID_SOCKET)
      {
        return;
      }
      SocketHandle accepted(static_cast<native_socket_handle>(raw));
#else
      const int raw = ::accept(listener_.get(), reinterpret_cast<sockaddr*>(&peer), &peer_size);
      if (raw < 0)
      {
        return;
      }
      SocketHandle accepted(raw);
#endif
      if (clients_.size() >= config_.max_clients)
      {
        // Closing here is the refusal: the cap exists so a runaway client
        // cannot make the switch hold sockets it will never serve.
        continue;
      }
      if (!set_non_blocking(accepted.get()))
      {
        continue;
      }
      suppress_sigpipe(accepted.get());
      disable_nagle(accepted.get());

      Client client;
      client.socket = std::move(accepted);
      clients_.push_back(std::move(client));
    }
  }

  bool ControlServer::serve(Client& client, size_t& served)
  {
    const bool open = drain(client.socket.get(), client.inbox, config_.max_request_bytes);
    take_messages(
        client.inbox,
        [this, &client, &served](std::string_view message)
        {
          handle(client, message);
          ++served;
        });
    return open;
  }

  void ControlServer::handle(Client& client, std::string_view request)
  {
    auto dispatch = service_.get().dispatch(request);
    enqueue(client, to_json(dispatch.reply));
    if (dispatch.metrics_event)
    {
      broadcast(to_json(*dispatch.metrics_event));
    }
    for (const auto& fault : dispatch.fault_events)
    {
      broadcast(to_json(fault));
    }
    if (dispatch.topology_event)
    {
      broadcast(to_json(*dispatch.topology_event));
    }
  }

  bool ControlServer::flush(Client& client)
  {
    size_t sent_total = 0;
    while (sent_total < client.outbox.size())
    {
      const auto sent = send_some(client.socket.get(), client.outbox.data() + sent_total, client.outbox.size() - sent_total);
      if (sent <= 0)
      {
        client.outbox.erase(0, sent_total);
        return would_block();
      }
      sent_total += static_cast<size_t>(sent);
    }
    client.outbox.clear();
    return true;
  }

  void ControlServer::enqueue(Client& client, const std::string& message)
  {
    if (client.congested)
    {
      return;
    }
    if (client.outbox.size() + message.size() + 1 > config_.max_pending_bytes)
    {
      client.congested = true;
      client.outbox.clear();
      return;
    }
    client.outbox.append(message);
    client.outbox.push_back('\n');
  }

  void ControlServer::broadcast(const std::string& message)
  {
    for (auto& client : clients_)
    {
      enqueue(client, message);
      if (!client.outbox.empty())
      {
        // Events are pushed rather than parked until the next poll: an
        // enforcement a client is watching for is worth a write attempt now.
        (void)flush(client);
      }
    }
  }

  void ControlServer::publish_analysis(AnalysisOutcome outcome)
  {
    if (clients_.empty())
    {
      return;
    }
    auto events = service_.get().analysis_events(std::move(outcome));
    for (const auto& anomaly : events.anomaly_events)
    {
      broadcast(to_json(anomaly));
    }
    for (const auto& policy : events.policy_events)
    {
      broadcast(to_json(policy));
    }
    for (const auto& fault : events.fault_events)
    {
      broadcast(to_json(fault));
    }
  }

  void
  ControlServer::publish_supervision(uint64_t analysed_frames, uint64_t blocked_frames, std::vector<PortBinding> bindings)
  {
    if (clients_.empty())
    {
      return;
    }
    broadcast(to_json(service_.get().supervision_event(analysed_frames, blocked_frames, std::move(bindings))));
  }

  void ControlServer::close() noexcept
  {
    clients_.clear();
    listener_.close();
    port_ = 0;
  }

  expected<ControlClient, ControlTransportError> ControlClient::connect(std::string_view address, uint16_t port)
  {
    if (!ensure_socket_runtime())
    {
      return unexpected(ControlTransportError::SocketCreationFailed);
    }

#ifdef _WIN32
    const SOCKET raw = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (raw == INVALID_SOCKET)
    {
      return unexpected(ControlTransportError::SocketCreationFailed);
    }
    SocketHandle socket(static_cast<native_socket_handle>(raw));
#else
    const int raw = ::socket(AF_INET, SOCK_STREAM, 0);
    if (raw < 0)
    {
      return unexpected(ControlTransportError::SocketCreationFailed);
    }
    SocketHandle socket(raw);
#endif

    sockaddr_in remote{};
    if (!make_address(std::string(address), port, remote))
    {
      return unexpected(ControlTransportError::AddressResolutionFailed);
    }
#ifdef _WIN32
    if (::connect(static_cast<SOCKET>(socket.get()), reinterpret_cast<const sockaddr*>(&remote), sizeof(remote)) ==
        SOCKET_ERROR)
#else
    if (::connect(socket.get(), reinterpret_cast<const sockaddr*>(&remote), sizeof(remote)) < 0)
#endif
    {
      return unexpected(ControlTransportError::ConnectFailed);
    }

    suppress_sigpipe(socket.get());
    disable_nagle(socket.get());
    if (!set_non_blocking(socket.get()))
    {
      return unexpected(ControlTransportError::SocketCreationFailed);
    }
    return ControlClient(std::move(socket));
  }

  expected<void, ControlTransportError> ControlClient::send(std::string_view json)
  {
    if (!is_connected())
    {
      return unexpected(ControlTransportError::InvalidSocket);
    }

    std::string message(json);
    message.push_back('\n');
    size_t sent_total = 0;
    while (sent_total < message.size())
    {
      const auto sent = send_some(socket_.get(), message.data() + sent_total, message.size() - sent_total);
      if (sent > 0)
      {
        sent_total += static_cast<size_t>(sent);
        continue;
      }
      if (!would_block())
      {
        return unexpected(ControlTransportError::SendFailed);
      }

      PollDescriptor watched{};
      watched.fd = socket_.get();
      watched.events = POLL_WRITE;
      if (poll_descriptors(&watched, 1, poll_deadline(std::chrono::seconds(1))) < 0)
      {
        return unexpected(ControlTransportError::SendFailed);
      }
    }
    return {};
  }

  expected<std::optional<std::string>, ControlTransportError> ControlClient::receive(std::chrono::milliseconds timeout)
  {
    if (!is_connected())
    {
      return unexpected(ControlTransportError::InvalidSocket);
    }

    const auto deadline = std::chrono::steady_clock::now() + timeout;
    for (;;)
    {
      if (auto message = take_message(inbox_))
      {
        return message;
      }

      const auto remaining =
          std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
      if (remaining.count() < 0)
      {
        return std::optional<std::string>{};
      }

      PollDescriptor watched{};
      watched.fd = socket_.get();
      watched.events = POLL_READ;
      const int ready = poll_descriptors(&watched, 1, poll_deadline(remaining));
      if (ready < 0)
      {
        return unexpected(ControlTransportError::ReceiveFailed);
      }
      if (ready == 0)
      {
        return std::optional<std::string>{};
      }
      if (!drain(socket_.get(), inbox_, std::numeric_limits<size_t>::max()))
      {
        return unexpected(ControlTransportError::Disconnected);
      }
    }
  }

  void ControlClient::close() noexcept
  {
    socket_.close();
    inbox_.clear();
  }
}  // namespace wirelab
