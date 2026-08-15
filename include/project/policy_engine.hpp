#ifndef PROJECT_POLICY_ENGINE_HPP_
#define PROJECT_POLICY_ENGINE_HPP_

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "project/anomaly_detector.hpp"
#include "project/expected.hpp"

namespace project
{
  enum class PolicyAction
  {
    Allow,
    Drop,
    Mirror,
    RateLimit,
    Quarantine,
    AlertOnly
  };

  enum class PolicyConfigurationError
  {
    EmptyName,
    DuplicateName,
    MissingRateLimit
  };

  struct PolicyRule
  {
    std::string name;
    AnomalyType anomaly_type = AnomalyType::BroadcastStorm;
    PolicyAction action = PolicyAction::AlertOnly;
    bool enabled = true;
    uint64_t rate_limit_packets_per_second = 0;
  };

  struct PolicyDecision
  {
    std::string rule_name;
    PolicyAction action = PolicyAction::AlertOnly;
    AnomalyEvent anomaly;
    uint64_t hit_count = 0;
    uint64_t rate_limit_packets_per_second = 0;
  };

  class PolicyEngine
  {
   public:
    [[nodiscard]] expected<void, PolicyConfigurationError> set_rules(std::vector<PolicyRule> rules);
    [[nodiscard]] expected<void, PolicyConfigurationError> add_rule(PolicyRule rule);
    [[nodiscard]] bool remove_rule(std::string_view name);
    [[nodiscard]] bool set_enabled(std::string_view name, bool enabled);
    [[nodiscard]] std::vector<PolicyRule> rules() const;
    [[nodiscard]] std::vector<PolicyDecision> evaluate(const std::vector<AnomalyEvent>& anomalies);
    [[nodiscard]] uint64_t hit_count(std::string_view name) const noexcept;
    void reset() noexcept;

   private:
    [[nodiscard]] static expected<void, PolicyConfigurationError> validate(const PolicyRule& rule) noexcept;

    std::vector<PolicyRule> rules_;
    std::unordered_map<std::string, uint64_t> hit_counts_;
  };

  [[nodiscard]] const char* to_string(PolicyAction action) noexcept;
  [[nodiscard]] const char* to_string(PolicyConfigurationError error) noexcept;
}

#endif
