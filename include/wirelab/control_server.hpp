#ifndef PROJECT_CONTROL_SERVER_HPP_
#define PROJECT_CONTROL_SERVER_HPP_

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "wirelab/control_service.hpp"
#include "wirelab/expected.hpp"
#include "wirelab/sys_utils.hpp"

namespace wirelab
{
  enum class ControlTransportError
  {
    SocketCreationFailed,
    BindFailed,
    ListenFailed,
    ConnectFailed,
    SendFailed,
    ReceiveFailed,
    AddressResolutionFailed,
    InvalidSocket,
    Disconnected,
    Timeout
  };

  [[nodiscard]] const char* to_string(ControlTransportError error) noexcept;

  struct ControlServerConfig
  {
    // A lab control plane carries operators and a GUI, not a fleet.
    size_t max_clients = 8;
    // A client that never completes a request, or never drains the replies it
    // asked for, is disconnected rather than allowed to grow the switch's
    // memory without bound.
    size_t max_request_bytes = 64 * 1024;
    size_t max_pending_bytes = 1024 * 1024;
  };

  // The switch's control plane, on a socket.
  //
  // Requests and events are newline-delimited JSON: one control message per
  // line, in the encoding control_protocol.hpp already defines. A reply goes to
  // the client that asked; every event is broadcast, because an event describes
  // a change to state all clients share and carries the sequence number they
  // use to order it.
  //
  // Nothing here blocks. The server is polled by the switch's own receive loop,
  // so a client that stops reading must never be able to stall forwarding: its
  // replies queue, and it is disconnected once the queue exceeds the cap.
  class ControlServer
  {
   public:
    [[nodiscard]] static expected<ControlServer, ControlTransportError>
    create(ControlService& service, uint16_t port, std::string_view address = "127.0.0.1", ControlServerConfig config = {});

    ControlServer(ControlServer&&) noexcept = default;
    ControlServer& operator=(ControlServer&&) noexcept = default;
    ControlServer(const ControlServer&) = delete;
    ControlServer& operator=(const ControlServer&) = delete;
    ~ControlServer() = default;

    // Accepts, reads, dispatches and flushes once, waiting at most timeout for
    // something to happen. Returns the number of requests served.
    size_t poll(std::chrono::milliseconds timeout = std::chrono::milliseconds{ 0 });

    void publish_analysis(AnalysisOutcome outcome);
    void publish_supervision(SupervisionSnapshot snapshot);

    [[nodiscard]] uint16_t port() const noexcept
    {
      return port_;
    }
    [[nodiscard]] size_t client_count() const noexcept
    {
      return clients_.size();
    }
    [[nodiscard]] bool is_listening() const noexcept
    {
      return listener_.is_valid();
    }
    void close() noexcept;

   private:
    struct Client
    {
      SocketHandle socket;
      std::string inbox;
      std::string outbox;
      // Set when the client owes more than it is willing to read; the next poll
      // disconnects it.
      bool congested = false;
    };

    ControlServer(ControlService& service, SocketHandle listener, uint16_t port, ControlServerConfig config) noexcept;

    void accept_pending();
    [[nodiscard]] bool serve(Client& client, size_t& served);
    void handle(Client& client, std::string_view request);
    [[nodiscard]] bool flush(Client& client);
    void enqueue(Client& client, const std::string& message);
    void broadcast(const std::string& message);

    std::reference_wrapper<ControlService> service_;
    SocketHandle listener_;
    uint16_t port_ = 0;
    ControlServerConfig config_;
    std::vector<Client> clients_;
  };

  // What a client learned by asking the switch to describe itself again.
  struct ControlResync
  {
    uint64_t topology_revision = 0;
    // The events the queries produced, in arrival order. Replies are the
    // client's own bookkeeping and are not repeated here: what rebuilds a view
    // is the same event stream the client reads when it is not resynchronising.
    std::vector<std::string> messages;
  };

  // The other end of the control channel: what a GUI, a script or a test uses
  // to drive a running switch.
  class ControlClient
  {
   public:
    [[nodiscard]] static expected<ControlClient, ControlTransportError> connect(std::string_view address, uint16_t port);
    // Dials the switch again on the endpoint this client was created with. The
    // connection is the only thing restored; what the client knew about the
    // switch is re-established by resync().
    [[nodiscard]] expected<void, ControlTransportError> reconnect();
    // Asks the switch to describe itself: its metrics, its active faults and
    // its supervision state. A switch that reloaded its topology rejects the
    // first query as stale, and the rejection carries the revision that
    // replaces it, so the query is repeated once against the truth.
    [[nodiscard]] expected<ControlResync, ControlTransportError> resync(std::chrono::milliseconds timeout);

    ControlClient(ControlClient&&) noexcept = default;
    ControlClient& operator=(ControlClient&&) noexcept = default;
    ControlClient(const ControlClient&) = delete;
    ControlClient& operator=(const ControlClient&) = delete;
    ~ControlClient() = default;

    [[nodiscard]] expected<void, ControlTransportError> send(std::string_view json);
    // The next complete message, or nullopt when the timeout lapsed first. A
    // closed connection is an error, not an empty read, so a caller cannot
    // mistake a dead switch for a quiet one.
    [[nodiscard]] expected<std::optional<std::string>, ControlTransportError> receive(std::chrono::milliseconds timeout);

    [[nodiscard]] bool is_connected() const noexcept
    {
      return socket_.is_valid();
    }
    // The revision every command must carry, as last seen on a reply.
    [[nodiscard]] uint64_t topology_revision() const noexcept
    {
      return topology_revision_;
    }
    void close() noexcept;

   private:
    ControlClient(SocketHandle socket, std::string address, uint16_t port) noexcept
        : socket_(std::move(socket)),
          address_(std::move(address)),
          port_(port)
    {
    }

    [[nodiscard]] expected<ControlReply, ControlTransportError> query(
        ControlCommand command,
        const std::string& request_id,
        std::chrono::steady_clock::time_point deadline,
        std::vector<std::string>& events);

    SocketHandle socket_;
    std::string address_;
    uint16_t port_ = 0;
    uint64_t topology_revision_ = 0;
    std::string inbox_;
  };
}  // namespace wirelab

#endif
