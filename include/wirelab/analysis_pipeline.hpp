#ifndef PROJECT_ANALYSIS_PIPELINE_HPP_
#define PROJECT_ANALYSIS_PIPELINE_HPP_

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>
#include <vector>

#include "wirelab/anomaly_detector.hpp"
#include "wirelab/packet_analyzer.hpp"
#include "wirelab/policy_enforcer.hpp"
#include "wirelab/policy_engine.hpp"
#include "wirelab/topology_controller.hpp"

namespace wirelab
{
  // Everything one analysed batch produced, in the order it was produced.
  struct AnalysisOutcome
  {
    std::vector<AnomalyEvent> anomalies;
    std::vector<PolicyDecision> decisions;
    // Leases that expired on this evaluation, restoring the operator's fault.
    std::vector<EnforcementAction> released;
    std::vector<EnforcementAction> enforced;
    // The detection clock actually used, after the monotonic clamp.
    uint64_t timestamp_ns = 0;

    [[nodiscard]] bool empty() const noexcept
    {
      return anomalies.empty() && decisions.empty() && released.empty() && enforced.empty();
    }
  };

  // The detection -> policy -> enforcement chain, owned in one place.
  //
  // Every consumer that observes traffic runs the same three stages against the
  // same state, so they live here rather than being rewired by each caller. A
  // pipeline without a topology controller still detects and decides; it just
  // has no port to enforce on, which is the correct behaviour when replaying a
  // capture that no live topology corresponds to.
  class AnalysisPipeline
  {
   public:
    explicit AnalysisPipeline(AnomalyDetectorConfig detector_config = {}, PolicyEnforcerConfig enforcer_config = {});
    AnalysisPipeline(
        AnomalyDetectorConfig detector_config,
        TopologyController& controller,
        PolicyEnforcerConfig enforcer_config = {});

    [[nodiscard]] AnalysisOutcome
    evaluate(const AnalysisBatch& batch, uint64_t timestamp_ns, std::chrono::steady_clock::time_point now);

    void attach(TopologyController& controller) noexcept;
    void detach() noexcept;
    [[nodiscard]] bool enforces() const noexcept;
    // Ends a lease early and restores the port. False when the port holds none.
    [[nodiscard]] bool release(std::string_view port_id);

    [[nodiscard]] AnomalyDetector& detector() noexcept
    {
      return detector_;
    }
    [[nodiscard]] PolicyEngine& policies() noexcept
    {
      return policies_;
    }
    [[nodiscard]] PolicyEnforcer& enforcer() noexcept
    {
      return enforcer_;
    }
    [[nodiscard]] const PolicyEngine& policies() const noexcept
    {
      return policies_;
    }
    [[nodiscard]] const PolicyEnforcer& enforcer() const noexcept
    {
      return enforcer_;
    }

    // Clears detection windows, hit counts and the batch clock. Leases are
    // forgotten rather than restored, because a reset accompanies a topology
    // rebuild in which the leased ports may no longer exist.
    void reset() noexcept;

   private:
    AnomalyDetector detector_;
    PolicyEngine policies_;
    PolicyEnforcer enforcer_;
    std::optional<std::reference_wrapper<TopologyController>> controller_;
    uint64_t last_timestamp_ns_ = 0;
  };
}  // namespace wirelab

#endif
