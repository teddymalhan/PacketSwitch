#include "wirelab/switch_supervisor.hpp"

#include <algorithm>
#include <utility>

namespace wirelab
{
  SwitchSupervisor::SwitchSupervisor(
      AnalysisPipeline& pipeline,
      TopologyController& controller,
      SwitchSupervisorConfig config)
      : pipeline_(pipeline),
        controller_(controller),
        config_(config)
  {
    pipeline_.attach(controller_);
    batch_.reserve(config_.max_batch_frames);
  }

  const SwitchSupervisor::Binding& SwitchSupervisor::bind(const Endpoint& sender)
  {
    const auto key = sender.to_string();
    const auto existing = bindings_.find(key);
    if (existing != bindings_.end())
    {
      return existing->second;
    }

    Binding binding;
    binding.ingress_port = static_cast<uint32_t>(bindings_.size());
    binding.port_id = controller_.port_id_at(binding.ingress_port).value_or(std::string{});
    return bindings_.emplace(key, std::move(binding)).first->second;
  }

  FaultDecision SwitchSupervisor::inspect(
      const std::vector<uint8_t>& frame_data,
      const Endpoint& sender,
      std::chrono::steady_clock::time_point arrival)
  {
    const auto& binding = bind(sender);

    // The frame is recorded before it is judged: containment must not blind the
    // detector to the traffic that justifies keeping the port contained.
    if (batch_.size() < config_.max_batch_frames)
    {
      batch_.push_back({ frame_data, binding.ingress_port });
    }
    if (batch_.size() >= config_.max_batch_frames)
    {
      tick(arrival);
    }

    FaultDecision decision;
    decision.delivery_count = 1;
    decision.delivery_times[0] = arrival;
    if (binding.port_id.empty())
    {
      // More clients than the topology declares ports: still analysed, but there
      // is no port whose fault could be applied to it.
      return decision;
    }

    auto evaluated = controller_.evaluate_port(binding.port_id, frame_data.size(), arrival);
    if (!evaluated)
    {
      return decision;
    }
    if (evaluated.value().dropped || evaluated.value().delivery_count == 0)
    {
      ++blocked_frames_;
    }
    return evaluated.value();
  }

  void SwitchSupervisor::tick(std::chrono::steady_clock::time_point now)
  {
    const bool due = last_tick_ == std::chrono::steady_clock::time_point{} || now - last_tick_ >= config_.tick_interval;
    if (!due && batch_.size() < config_.max_batch_frames)
    {
      return;
    }
    last_tick_ = now;

    std::vector<PacketView> views;
    views.reserve(batch_.size());
    for (const auto& frame : batch_)
    {
      views.push_back({ frame.bytes.data(), frame.bytes.size(), frame.ingress_port });
    }

    const auto analysis = analyzer_.analyze(views.data(), views.size());
    analysed_frames_ += analysis.received_packets;
    batch_.clear();

    // Detection runs on the wall clock the leases use, so a window and a lease
    // measured in seconds mean the same seconds.
    const auto timestamp_ns =
        static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count());
    (void)pipeline_.evaluate(analysis, timestamp_ns, now);
  }

  std::chrono::milliseconds SwitchSupervisor::tick_interval() const noexcept
  {
    return config_.tick_interval;
  }

  std::vector<std::pair<std::string, Endpoint>> SwitchSupervisor::bindings() const
  {
    std::vector<std::pair<std::string, Endpoint>> bound;
    bound.reserve(bindings_.size());
    for (const auto& [endpoint, binding] : bindings_)
    {
      if (binding.port_id.empty())
      {
        continue;
      }
      const auto separator = endpoint.rfind(':');
      const auto address = separator == std::string::npos ? endpoint : endpoint.substr(0, separator);
      const auto port =
          separator == std::string::npos ? 0 : static_cast<uint16_t>(std::stoul(endpoint.substr(separator + 1)));
      bound.emplace_back(binding.port_id, Endpoint(address, port));
    }
    std::sort(bound.begin(), bound.end(), [](const auto& left, const auto& right) { return left.first < right.first; });
    return bound;
  }
}  // namespace wirelab
