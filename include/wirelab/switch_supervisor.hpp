#ifndef PROJECT_SWITCH_SUPERVISOR_HPP_
#define PROJECT_SWITCH_SUPERVISOR_HPP_

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "wirelab/analysis_pipeline.hpp"
#include "wirelab/control_protocol.hpp"
#include "wirelab/packet_analyzer.hpp"
#include "wirelab/topology_controller.hpp"
#include "wirelab/udp_socket.hpp"
#include "wirelab/vswitch.hpp"

namespace wirelab
{
  class ControlServer;

  struct SwitchSupervisorConfig
  {
    // How often batched frames are analysed and leases are reconsidered. Also
    // the switch's idle wakeup interval, so detection advances without traffic.
    std::chrono::milliseconds tick_interval{ 200 };
    // Analyse early when a burst fills the batch, so a storm is contained
    // within the tick rather than after it.
    size_t max_batch_frames = 512;
  };

  // Makes a live VSwitch subject to WireLab analysis.
  //
  // Frames arriving at the switch are attributed to a topology port, recorded
  // for batched analysis, and checked against that port's current fault before
  // they are learned or forwarded. Enforcement therefore closes the loop on real
  // traffic: a policy that quarantines a port stops that port forwarding, and
  // the port recovers by itself when the lease lapses.
  class SwitchSupervisor final : public FrameGate
  {
   public:
    SwitchSupervisor(AnalysisPipeline& pipeline, TopologyController& controller, SwitchSupervisorConfig config = {});

    // Gives the supervisor a control plane to serve and to publish onto. The
    // server is polled from tick(), so the switch stays single-threaded and a
    // control client can never interleave with a frame being forwarded.
    void attach_control(ControlServer& server) noexcept;

    [[nodiscard]] FaultDecision inspect(
        const std::vector<uint8_t>& frame_data,
        const Endpoint& sender,
        std::chrono::steady_clock::time_point arrival) override;
    void tick(std::chrono::steady_clock::time_point now) override;
    [[nodiscard]] std::chrono::milliseconds tick_interval() const noexcept override;

    // Endpoints are bound to topology ports in first-seen order, which is the
    // only ordering a UDP dataplane offers; the binding is reported so an
    // operator can tell which client became which port.
    [[nodiscard]] std::vector<std::pair<std::string, Endpoint>> bindings() const;
    [[nodiscard]] uint64_t analysed_frames() const noexcept
    {
      return analysed_frames_;
    }
    [[nodiscard]] uint64_t blocked_frames() const noexcept
    {
      return blocked_frames_;
    }
    [[nodiscard]] std::vector<PortBinding> port_bindings() const;

   private:
    struct Binding
    {
      uint32_t ingress_port = 0;
      std::string port_id;
    };

    struct BatchedFrame
    {
      std::vector<uint8_t> bytes;
      uint32_t ingress_port = 0;
    };

    [[nodiscard]] const Binding& bind(const Endpoint& sender);
    void publish();

    AnalysisPipeline& pipeline_;
    TopologyController& controller_;
    SwitchSupervisorConfig config_;
    ControlServer* control_ = nullptr;
    CpuPacketAnalyzer analyzer_;
    std::unordered_map<std::string, Binding> bindings_;
    // Frames are copied because analysis runs after the switch has released the
    // receive buffer; views into it would dangle by the time the batch flushes.
    std::vector<BatchedFrame> batch_;
    std::chrono::steady_clock::time_point last_tick_{};
    uint64_t analysed_frames_ = 0;
    uint64_t blocked_frames_ = 0;
    uint64_t published_analysed_frames_ = 0;
    uint64_t published_blocked_frames_ = 0;
    size_t published_bindings_ = 0;
  };
}  // namespace wirelab

#endif
