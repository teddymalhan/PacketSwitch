#include "wirelab/analysis_pipeline.hpp"

#include <algorithm>
#include <utility>

namespace wirelab
{
  AnalysisPipeline::AnalysisPipeline(AnomalyDetectorConfig detector_config, PolicyEnforcerConfig enforcer_config)
      : detector_(detector_config),
        enforcer_(enforcer_config)
  {
  }

  AnalysisPipeline::AnalysisPipeline(
      AnomalyDetectorConfig detector_config,
      TopologyController& controller,
      PolicyEnforcerConfig enforcer_config)
      : detector_(detector_config),
        enforcer_(enforcer_config),
        controller_(controller)
  {
  }

  AnalysisOutcome
  AnalysisPipeline::evaluate(const AnalysisBatch& batch, uint64_t timestamp_ns, std::chrono::steady_clock::time_point now)
  {
    AnalysisOutcome outcome;
    // Captures and out-of-order arrivals can move a batch clock backwards; the
    // detector's sliding window requires it not to, so the clock is clamped
    // forward here once instead of in each caller.
    last_timestamp_ns_ = std::max(last_timestamp_ns_, timestamp_ns);
    outcome.timestamp_ns = last_timestamp_ns_;
    outcome.anomalies = detector_.evaluate(batch, last_timestamp_ns_);
    outcome.decisions = policies_.evaluate(outcome.anomalies);
    if (!controller_)
    {
      return outcome;
    }

    auto& controller = controller_->get();
    // Expiry runs before application so a decision arriving in the same batch
    // that a lease ends in re-enforces from the operator's restored baseline.
    outcome.released = enforcer_.release_expired(controller, now);
    outcome.enforced = enforcer_.apply(outcome.decisions, controller, now);
    return outcome;
  }

  void AnalysisPipeline::attach(TopologyController& controller) noexcept
  {
    controller_ = controller;
  }

  void AnalysisPipeline::detach() noexcept
  {
    controller_.reset();
  }

  bool AnalysisPipeline::enforces() const noexcept
  {
    return controller_.has_value();
  }

  bool AnalysisPipeline::release(std::string_view port_id)
  {
    return controller_ && enforcer_.release(port_id, controller_->get());
  }

  void AnalysisPipeline::reset() noexcept
  {
    detector_.reset();
    policies_.reset();
    enforcer_.forget();
    last_timestamp_ns_ = 0;
  }
}  // namespace wirelab
