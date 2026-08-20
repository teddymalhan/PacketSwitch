// Exercises the C ABI the way a foreign frontend does: only through
// wirelab/wirelab_ffi.h, with no C++ type from the core in sight. If this
// passes under ASan, the boundary is sound for a Rust binding.

#include "wirelab/wirelab_ffi.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>

namespace
{
  std::string scenario_path()
  {
    return (std::filesystem::path(PROJECT_SOURCE_DIRECTORY) / "scenarios" / "security-lab.yaml").string();
  }

  std::string text(wirelab_str value)
  {
    return value.ptr == nullptr ? std::string{} : std::string(value.ptr, value.len);
  }

  class FfiSession
  {
   public:
    FfiSession() : handle_(wirelab_session_open(WIRELAB_FFI_ABI_VERSION)) {}
    ~FfiSession()
    {
      wirelab_session_close(handle_);
    }
    FfiSession(const FfiSession&) = delete;
    FfiSession& operator=(const FfiSession&) = delete;

    [[nodiscard]] wirelab_session* get() const noexcept
    {
      return handle_;
    }

   private:
    wirelab_session* handle_;
  };
}  // namespace

TEST(WirelabFfiTest, RefusesAnAbiItDoesNotSpeak)
{
  EXPECT_EQ(wirelab_ffi_abi_version(), WIRELAB_FFI_ABI_VERSION);
  // A binding built against a different header must be turned away at the door
  // rather than left to misread every struct that follows.
  EXPECT_EQ(wirelab_session_open(WIRELAB_FFI_ABI_VERSION + 1u), nullptr);
  EXPECT_EQ(wirelab_session_open(0u), nullptr);
}

TEST(WirelabFfiTest, ReportsItsOwnStructLayout)
{
  wirelab_abi_layout_report layout{};
  wirelab_abi_layout(&layout);

  EXPECT_EQ(layout.abi_version, WIRELAB_FFI_ABI_VERSION);
  EXPECT_EQ(layout.str.size, sizeof(wirelab_str));
  EXPECT_EQ(layout.str.align, alignof(wirelab_str));
  EXPECT_EQ(layout.node.size, sizeof(wirelab_node_view));
  EXPECT_EQ(layout.link.size, sizeof(wirelab_link_view));
  EXPECT_EQ(layout.metric_sample.size, sizeof(wirelab_metric_sample));
  EXPECT_EQ(layout.mac_table.size, sizeof(wirelab_mac_table_view));
  EXPECT_EQ(layout.port_state.size, sizeof(wirelab_port_state_view));
  EXPECT_EQ(layout.packet.size, sizeof(wirelab_packet_view));
  EXPECT_EQ(layout.anomaly.size, sizeof(wirelab_anomaly_view));
  EXPECT_EQ(layout.fault.size, sizeof(wirelab_fault_view));
  EXPECT_EQ(layout.policy.size, sizeof(wirelab_policy_view));
  EXPECT_EQ(layout.policy_action.size, sizeof(wirelab_policy_action_view));
  EXPECT_EQ(layout.enforced_port.size, sizeof(wirelab_enforced_port_view));
  EXPECT_EQ(layout.report_row.size, sizeof(wirelab_report_row_view));
  EXPECT_EQ(layout.provenance.size, sizeof(wirelab_provenance_view));

  // Tolerating a null out-pointer keeps a probing binding from crashing the host.
  wirelab_abi_layout(nullptr);
}

TEST(WirelabFfiTest, PublishesTheStaticTables)
{
  ASSERT_GT(wirelab_backend_count(), 0U);
  EXPECT_EQ(text(wirelab_backend_name(0)), "CPU");
  EXPECT_TRUE(text(wirelab_backend_name(wirelab_backend_count())).empty());

  ASSERT_EQ(wirelab_scenario_count(), 7U);
  EXPECT_EQ(text(wirelab_scenario_name(3)), "mixed-traffic");

  ASSERT_EQ(wirelab_anomaly_type_count(), 7U);
  EXPECT_EQ(text(wirelab_anomaly_type_name(0)), "Broadcast storm");

  // Weakest action first: the order the Reports and Policies forms offer.
  ASSERT_EQ(wirelab_policy_action_name_count(), 6U);
  EXPECT_EQ(text(wirelab_policy_action_name(0)), "Alert only");
  EXPECT_EQ(text(wirelab_policy_action_name(5)), "Allow");

  EXPECT_EQ(text(wirelab_node_type_label(WIRELAB_NODE_SWITCH)), "switch");
  EXPECT_EQ(text(wirelab_classification_label(WIRELAB_CLASSIFICATION_BROADCAST)), "Broadcast");
  EXPECT_EQ(text(wirelab_validity_label(WIRELAB_VALIDITY_VALID)), "Valid");
  EXPECT_EQ(text(wirelab_anomaly_type_label(WIRELAB_ANOMALY_PORT_SCAN)), "Port scan");
  EXPECT_EQ(text(wirelab_policy_action_label(WIRELAB_POLICY_QUARANTINE)), "Quarantine");
  EXPECT_EQ(text(wirelab_enforcement_kind_label(WIRELAB_ENFORCEMENT_BLACKHOLE)), "blackhole");
  EXPECT_TRUE(text(wirelab_classification_label(99u)).empty());
}

TEST(WirelabFfiTest, SurvivesEveryNullAndOutOfRangeArgument)
{
  // A binding under development will call all of these wrong at least once.
  wirelab_session_close(nullptr);
  EXPECT_EQ(wirelab_session_take_dirty(nullptr), 0U);
  EXPECT_EQ(wirelab_session_status_message(nullptr).ptr, nullptr);
  EXPECT_FALSE(wirelab_topology_loaded(nullptr));
  EXPECT_EQ(wirelab_topology_node_count(nullptr), 0U);
  EXPECT_EQ(wirelab_packet_count(nullptr), 0U);
  EXPECT_FALSE(wirelab_report_export(nullptr, "/tmp/never"));
  size_t count = 12345;
  EXPECT_EQ(wirelab_metrics_history(nullptr, &count), nullptr);
  EXPECT_EQ(count, 0U);
  wirelab_traffic_step(nullptr);
  wirelab_report_step(nullptr);

  FfiSession session;
  ASSERT_NE(session.get(), nullptr);
  // Out-of-range indices answer with a zeroed row instead of trapping.
  EXPECT_EQ(wirelab_topology_node_at(session.get(), 99).id.ptr, nullptr);
  EXPECT_EQ(wirelab_packet_at(session.get(), 99).protocol, 0U);
  EXPECT_EQ(wirelab_report_row_at(session.get(), 99).packets, 0U);
  // A null string argument is the empty string, not a dereference.
  wirelab_topology_open(session.get(), nullptr);
  wirelab_select_node(session.get(), nullptr);
  wirelab_fault_clear(session.get(), nullptr, nullptr);
  EXPECT_FALSE(wirelab_topology_loaded(session.get()));
}

TEST(WirelabFfiTest, DrivesTheWholeTopologyTrafficAndFaultWorkflow)
{
  FfiSession session;
  ASSERT_NE(session.get(), nullptr);
  (void)wirelab_session_take_dirty(session.get());

  wirelab_topology_open(session.get(), scenario_path().c_str());
  const uint32_t opened = wirelab_session_take_dirty(session.get());
  EXPECT_TRUE(opened & WIRELAB_DIRTY_TOPOLOGY);
  EXPECT_TRUE(opened & WIRELAB_DIRTY_STATUS);
  EXPECT_EQ(wirelab_session_take_dirty(session.get()), 0U);

  ASSERT_TRUE(wirelab_topology_loaded(session.get()));
  EXPECT_EQ(text(wirelab_topology_name(session.get())), "security-lab");
  ASSERT_EQ(wirelab_topology_node_count(session.get()), 4U);
  ASSERT_EQ(wirelab_topology_link_count(session.get()), 3U);

  bool saw_switch = false;
  for (size_t index = 0; index < wirelab_topology_node_count(session.get()); ++index)
  {
    const auto node = wirelab_topology_node_at(session.get(), index);
    EXPECT_FALSE(text(node.id).empty());
    EXPECT_GE(node.x, 0.0);
    EXPECT_LE(node.x, 1.0);
    EXPECT_GE(node.y, 0.0);
    EXPECT_LE(node.y, 1.0);
    saw_switch = saw_switch || node.type == WIRELAB_NODE_SWITCH;
  }
  EXPECT_TRUE(saw_switch);
  EXPECT_GT(wirelab_topology_link_at(session.get(), 0).latency_ms, 0);

  wirelab_select_node(session.get(), "client-a");
  EXPECT_EQ(wirelab_selection_kind_of(session.get()), WIRELAB_SELECTION_NODE);
  EXPECT_EQ(text(wirelab_selected_id(session.get())), "client-a");
  EXPECT_FALSE(text(wirelab_selected_summary(session.get())).empty());

  wirelab_fault_apply_selected(session.get(), 25, 0.0, false);
  ASSERT_EQ(wirelab_fault_count(session.get()), 1U);
  const auto fault = wirelab_fault_at(session.get(), 0);
  EXPECT_EQ(text(fault.first), "client-a");
  EXPECT_FALSE(fault.is_link);
  EXPECT_EQ(fault.latency_ms, 25);

  wirelab_traffic_start(session.get(), "mixed-traffic", 64, 128, 42, "CPU");
  ASSERT_TRUE(wirelab_traffic_running(session.get()));
  EXPECT_EQ(text(wirelab_active_backend(session.get())), "CPU");
  wirelab_traffic_step(session.get());
  wirelab_traffic_stop(session.get());

  size_t sample_count = 0;
  const wirelab_metric_sample* samples = wirelab_metrics_history(session.get(), &sample_count);
  ASSERT_EQ(sample_count, 1U);
  ASSERT_NE(samples, nullptr);
  EXPECT_EQ(samples[0].sequence, 1U);
  EXPECT_GE(samples[0].throughput_mbps, 0.0);

  ASSERT_GT(wirelab_packet_count(session.get()), 0U);
  const auto packet = wirelab_packet_at(session.get(), 0);
  EXPECT_EQ(text(packet.source_mac).size(), 17U);
  EXPECT_FALSE(text(packet.source_ip).empty());
  EXPECT_GT(packet.bytes, 0U);

  EXPECT_GT(wirelab_mac_table_count(session.get()), 0U);
  ASSERT_EQ(wirelab_port_state_count(session.get()), 3U);
  EXPECT_NE(text(wirelab_traffic_result(session.get())).find("64 packets generated"), std::string::npos);

  wirelab_fault_clear(session.get(), "client-a", "");
  EXPECT_EQ(wirelab_fault_count(session.get()), 0U);
}

TEST(WirelabFfiTest, ManagesPoliciesThroughDisplayNames)
{
  FfiSession session;
  ASSERT_NE(session.get(), nullptr);

  wirelab_policy_add(session.get(), "storm-guard", "Broadcast storm", "Quarantine", 0);
  ASSERT_EQ(wirelab_policy_count(session.get()), 1U);
  const auto rule = wirelab_policy_at(session.get(), 0);
  EXPECT_EQ(text(rule.name), "storm-guard");
  EXPECT_EQ(rule.anomaly_type, WIRELAB_ANOMALY_BROADCAST_STORM);
  EXPECT_EQ(rule.action, WIRELAB_POLICY_QUARANTINE);
  EXPECT_TRUE(rule.enabled);

  wirelab_policy_set_enabled(session.get(), "storm-guard", false);
  EXPECT_FALSE(wirelab_policy_at(session.get(), 0).enabled);

  wirelab_policy_add(session.get(), "bogus", "Not an anomaly", "Quarantine", 0);
  EXPECT_EQ(wirelab_policy_count(session.get()), 1U);
  EXPECT_NE(
      text(wirelab_session_status_message(session.get())).find("Unknown anomaly type or policy action."),
      std::string::npos);

  wirelab_policy_remove(session.get(), "storm-guard");
  EXPECT_EQ(wirelab_policy_count(session.get()), 0U);
  EXPECT_EQ(wirelab_enforced_port_count(session.get()), 0U);
  EXPECT_EQ(wirelab_policy_action_count(session.get()), 0U);
}

TEST(WirelabFfiTest, RunsAndExportsABenchmarkReport)
{
  FfiSession session;
  ASSERT_NE(session.get(), nullptr);

  wirelab_report_start(session.get(), "port-scan", 256, 32, 64, 3);
  ASSERT_TRUE(wirelab_report_running(session.get()));
  int steps = 0;
  while (wirelab_report_running(session.get()) && steps < 10000)
  {
    wirelab_report_step(session.get());
    ++steps;
  }
  ASSERT_FALSE(wirelab_report_running(session.get()));
  EXPECT_DOUBLE_EQ(wirelab_report_progress(session.get()), 1.0);
  EXPECT_FALSE(text(wirelab_report_stage(session.get())).empty());

  ASSERT_EQ(wirelab_report_row_count(session.get()), wirelab_backend_count());
  const auto row = wirelab_report_row_at(session.get(), 0);
  EXPECT_EQ(text(row.backend_label), "CPU");
  EXPECT_EQ(text(row.backend_id), "cpu");
  EXPECT_EQ(text(row.scenario), "port-scan");
  EXPECT_EQ(row.packets, 256U);
  EXPECT_GT(row.packets_per_second, 0.0);
  EXPECT_DOUBLE_EQ(row.speedup, 1.0);

  const auto provenance = wirelab_report_provenance(session.get());
  EXPECT_EQ(text(provenance.scenario), "port-scan");
  EXPECT_EQ(provenance.seed, 3U);
  EXPECT_EQ(provenance.packets, 256U);
  EXPECT_FALSE(text(provenance.version).empty());
  EXPECT_FALSE(text(provenance.generated_at).empty());
  ASSERT_EQ(wirelab_report_present_count(session.get()), wirelab_backend_count());
  EXPECT_EQ(text(wirelab_report_present_at(session.get(), 0)), "CPU");
  EXPECT_GE(wirelab_report_compiled_in_count(session.get()), wirelab_report_present_count(session.get()));

  const auto base = (std::filesystem::temp_directory_path() / "wirelab-ffi-report").string();
  std::filesystem::remove(base + ".json");
  std::filesystem::remove(base + ".csv");
  ASSERT_TRUE(wirelab_report_export(session.get(), (base + ".json").c_str()));
  EXPECT_TRUE(std::filesystem::exists(base + ".json"));
  EXPECT_TRUE(std::filesystem::exists(base + ".csv"));
  EXPECT_EQ(text(wirelab_report_export_path(session.get())), base + ".json");
  std::filesystem::remove(base + ".json");
  std::filesystem::remove(base + ".csv");
}

// Borrowed strings are the boundary's sharpest edge: this pins down exactly how
// long they are good for.
TEST(WirelabFfiTest, KeepsBorrowedRowsValidUntilTheNextMutation)
{
  FfiSession session;
  ASSERT_NE(session.get(), nullptr);
  wirelab_topology_open(session.get(), scenario_path().c_str());

  const auto node = wirelab_topology_node_at(session.get(), 0);
  const std::string identifier = text(node.id);
  ASSERT_FALSE(identifier.empty());

  // Pure getters do not invalidate anything.
  (void)wirelab_topology_link_count(session.get());
  (void)wirelab_selection_kind_of(session.get());
  EXPECT_EQ(text(node.id), identifier);

  // After a mutation the caller must re-read; the copy taken above is what
  // survives.
  wirelab_topology_add_node(session.get(), "client-c", "host");
  EXPECT_EQ(wirelab_topology_node_count(session.get()), 5U);
  EXPECT_EQ(text(wirelab_topology_node_at(session.get(), 0).id), identifier);
}
