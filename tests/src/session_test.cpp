#include "wirelab/session.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "wirelab/version.hpp"

namespace wirelab
{
  namespace
  {
    std::string scenario_path()
    {
      return (std::filesystem::path(PROJECT_SOURCE_DIRECTORY) / "scenarios" / "security-lab.yaml").string();
    }

    void run_report_to_completion(Session& session)
    {
      // The frontend drives the report from its frame loop; a test drives the
      // same step by hand so the run stays deterministic and needs no clock.
      int steps = 0;
      while (session.report_running() && steps < 10000)
      {
        session.run_report_step();
        ++steps;
      }
    }

    [[nodiscard]] bool contains(const std::string& haystack, const std::string& needle)
    {
      return haystack.find(needle) != std::string::npos;
    }

    [[nodiscard]] bool has_backend(const std::string& label)
    {
      const auto& backends = Session::available_backends();
      return std::find(backends.begin(), backends.end(), label) != backends.end();
    }

    [[nodiscard]] std::string read_file(const std::string& path)
    {
      std::ifstream input(path, std::ios::binary);
      std::ostringstream buffer;
      buffer << input.rdbuf();
      return buffer.str();
    }

    [[nodiscard]] std::vector<std::string> read_lines(const std::string& path)
    {
      std::vector<std::string> lines;
      std::istringstream input(read_file(path));
      std::string line;
      while (std::getline(input, line))
        if (!line.empty())
          lines.push_back(line);
      return lines;
    }
  }  // namespace

  TEST(SessionTest, ExercisesTopologyTrafficTelemetryAndFaultWorkflow)
  {
    Session session;
    session.open_topology(scenario_path());

    ASSERT_TRUE(session.has_topology());
    EXPECT_EQ(session.topology_nodes().size(), 4U);
    EXPECT_EQ(session.topology_links().size(), 3U);

    session.select_node("client-a");
    EXPECT_EQ(session.selection_kind(), SelectionKind::Node);
    session.apply_selected_fault(25, 0.0, false);
    ASSERT_EQ(session.active_faults().size(), 1U);
    EXPECT_EQ(session.active_faults().front().latency_ms, 25);
    EXPECT_FALSE(session.active_faults().front().is_link());

    session.start_traffic("mixed-traffic", 64, 128, 42, "CPU");
    ASSERT_TRUE(session.traffic_running());
    session.run_traffic_step();
    session.stop_traffic();

    ASSERT_EQ(session.metrics_history().size(), 1U);
    EXPECT_FALSE(session.packet_rows().empty());
    EXPECT_FALSE(session.mac_table().empty());
    ASSERT_EQ(session.port_states().size(), 3U);
    EXPECT_TRUE(contains(session.traffic_result(), "64 packets generated"));

    session.clear_fault("client-a", "");
    EXPECT_TRUE(session.active_faults().empty());
  }

  // The dirty mask is the frontend's only change notification, so a mutation
  // that changes state without raising its bit is invisible in the UI.
  TEST(SessionTest, ReportsChangedStateThroughTheDirtyMask)
  {
    Session session;
    (void)session.take_dirty();

    session.open_topology(scenario_path());
    const auto opened = session.take_dirty();
    EXPECT_TRUE(opened & dirty_bit(SessionDirty::Topology));
    EXPECT_TRUE(opened & dirty_bit(SessionDirty::Status));
    EXPECT_TRUE(opened & dirty_bit(SessionDirty::Selection));
    EXPECT_TRUE(opened & dirty_bit(SessionDirty::Telemetry));
    EXPECT_TRUE(opened & dirty_bit(SessionDirty::Faults));
    // Draining is destructive: the same change is never reported twice.
    EXPECT_EQ(session.take_dirty(), 0U);

    session.select_node("client-a");
    EXPECT_TRUE(session.take_dirty() & dirty_bit(SessionDirty::Selection));

    session.apply_selected_fault(10, 1.5, false);
    EXPECT_TRUE(session.take_dirty() & dirty_bit(SessionDirty::Faults));

    session.add_policy("storm-guard", "Broadcast storm", "Quarantine", 0);
    EXPECT_TRUE(session.take_dirty() & dirty_bit(SessionDirty::Policies));

    session.start_traffic("mixed-traffic", 32, 64, 7, "CPU");
    EXPECT_TRUE(session.take_dirty() & dirty_bit(SessionDirty::TrafficState));
    session.run_traffic_step();
    EXPECT_TRUE(session.take_dirty() & dirty_bit(SessionDirty::Telemetry));
  }

  TEST(SessionTest, RunsTrafficOnMetalBackendWhenAvailable)
  {
    if (!has_backend("Metal"))
      GTEST_SKIP() << "Metal backend not built or no Metal device";

    Session session;
    session.open_topology(scenario_path());
    ASSERT_TRUE(session.has_topology());
    EXPECT_TRUE(has_backend("CPU"));

    session.start_traffic("mixed-traffic", 64, 128, 42, "Metal");
    ASSERT_TRUE(session.traffic_running());
    EXPECT_EQ(session.active_backend(), "Metal");
    session.run_traffic_step();
    EXPECT_FALSE(session.packet_rows().empty());
    session.stop_traffic();

    session.start_traffic("mixed-traffic", 16, 128, 7, "DoesNotExist");
    EXPECT_FALSE(session.traffic_running());
    EXPECT_TRUE(contains(session.status_message(), "not available"));
  }

  TEST(SessionTest, RejectsInvalidTrafficSettings)
  {
    Session session;
    session.start_traffic("mixed-traffic", 64, 128, 42, "CPU");
    EXPECT_FALSE(session.traffic_running());
    EXPECT_TRUE(contains(session.status_message(), "topology"));

    session.open_topology(scenario_path());
    session.start_traffic("no-such-scenario", 64, 128, 42, "CPU");
    EXPECT_FALSE(session.traffic_running());
    EXPECT_TRUE(contains(session.status_message(), "invalid"));

    // A frame shorter than an Ethernet header is a configuration mistake, not
    // a workload.
    session.start_traffic("mixed-traffic", 64, 8, 42, "CPU");
    EXPECT_FALSE(session.traffic_running());

    // Stepping a stopped session must be inert, not a crash.
    session.run_traffic_step();
    EXPECT_TRUE(session.metrics_history().empty());
  }

  TEST(SessionTest, OpensTopologyFromFileUrlString)
  {
    Session session;
    session.open_topology("file://" + scenario_path());
    EXPECT_TRUE(session.has_topology());
    EXPECT_EQ(session.topology_name(), "security-lab");
  }

  TEST(SessionTest, EditsValidTopologyAndRejectsInvalidChanges)
  {
    Session session;
    session.open_topology(scenario_path());

    session.add_node("client-c", "host");
    ASSERT_EQ(session.topology_nodes().size(), 5U);
    session.add_link("client-c", "core-switch", 3);
    EXPECT_EQ(session.topology_links().size(), 4U);

    // A second switch is not a topology this lab can model, so the edit is
    // rejected whole rather than half-applied.
    session.add_node("core-switch", "switch");
    EXPECT_EQ(session.topology_nodes().size(), 5U);
    EXPECT_TRUE(contains(session.status_message(), "validation failed"));

    session.select_node("client-c");
    session.remove_selected();
    EXPECT_EQ(session.topology_nodes().size(), 4U);
    EXPECT_EQ(session.topology_links().size(), 3U);
  }

  TEST(SessionTest, SavesTopologyItCanReadBack)
  {
    Session session;
    session.open_topology(scenario_path());

    const auto path = (std::filesystem::temp_directory_path() / "wirelab-session-topology.yaml").string();
    std::filesystem::remove(path);
    session.save_topology(path);
    ASSERT_TRUE(std::filesystem::exists(path));

    Session reloaded;
    reloaded.open_topology(path);
    ASSERT_TRUE(reloaded.has_topology());
    EXPECT_EQ(reloaded.topology_name(), session.topology_name());
    EXPECT_EQ(reloaded.topology_nodes().size(), session.topology_nodes().size());
    EXPECT_EQ(reloaded.topology_links().size(), session.topology_links().size());
    std::filesystem::remove(path);
  }

  TEST(SessionTest, ManagesPolicyRules)
  {
    Session session;
    session.add_policy("storm-guard", "Broadcast storm", "Quarantine", 0);
    ASSERT_EQ(session.policy_rules().size(), 1U);
    EXPECT_EQ(session.policy_rules().front().name, "storm-guard");
    EXPECT_EQ(session.policy_rules().front().anomaly_type, AnomalyType::BroadcastStorm);
    EXPECT_EQ(session.policy_rules().front().action, PolicyAction::Quarantine);
    EXPECT_TRUE(session.policy_rules().front().enabled);

    session.set_policy_enabled("storm-guard", false);
    EXPECT_FALSE(session.policy_rules().front().enabled);

    session.add_policy("bogus", "No such anomaly", "Quarantine", 0);
    EXPECT_EQ(session.policy_rules().size(), 1U);
    EXPECT_TRUE(contains(session.status_message(), "Unknown anomaly type or policy action."));

    session.remove_policy("storm-guard");
    EXPECT_TRUE(session.policy_rules().empty());
    session.remove_policy("storm-guard");
    EXPECT_TRUE(contains(session.status_message(), "No policy named"));
  }

  TEST(SessionTest, ReportsBenchmarkRowsForEveryAvailableBackend)
  {
    Session session;
    EXPECT_FALSE(session.report_running());
    const auto& scenarios = scenario_names();
    EXPECT_NE(std::find(scenarios.begin(), scenarios.end(), "mixed-traffic"), scenarios.end());

    session.run_benchmark_report("mixed-traffic", 512, 64, 128, 7);
    ASSERT_TRUE(session.report_running());
    run_report_to_completion(session);
    ASSERT_FALSE(session.report_running());
    EXPECT_DOUBLE_EQ(session.report_progress(), 1.0);

    const auto& rows = session.report_rows();
    // Every backend the session offers has to end up measured, so an
    // accelerator that is listed but never runs is a failure, not a gap.
    ASSERT_EQ(rows.size(), Session::available_backends().size());
    const auto& cpu = rows.front();
    EXPECT_EQ(cpu.backend_label, "CPU");
    EXPECT_EQ(cpu.backend_id, "cpu");
    EXPECT_EQ(cpu.packets, 512U);
    EXPECT_GT(cpu.packets_per_second, 0.0);
    EXPECT_GT(cpu.goodput_bits_per_second, 0.0);
    EXPECT_DOUBLE_EQ(cpu.speedup, 1.0);
    EXPECT_GE(cpu.latency_p95_ns, cpu.latency_p50_ns);
    EXPECT_GE(cpu.latency_p99_ns, cpu.latency_p95_ns);

    // Rows follow the backend order the session advertises, so CPU is first and
    // every accelerator row is measured against it.
    const auto& backends = Session::available_backends();
    ptrdiff_t previous_position = -1;
    for (const auto& row : rows)
    {
      const auto found = std::find(backends.begin(), backends.end(), row.backend_label);
      ASSERT_NE(found, backends.end());
      const auto position = std::distance(backends.begin(), found);
      EXPECT_GT(position, previous_position);
      previous_position = position;
      EXPECT_GT(row.speedup, 0.0);
    }
  }

  TEST(SessionTest, ReportProvenanceNamesTheRunAndTheBuild)
  {
    Session session;
    session.run_benchmark_report("broadcast", 256, 32, 64, 11);
    run_report_to_completion(session);

    const auto& provenance = session.report_provenance();
    EXPECT_EQ(provenance.scenario, "broadcast");
    EXPECT_EQ(provenance.seed, 11U);
    EXPECT_EQ(provenance.packets, 256U);
    EXPECT_EQ(provenance.batch_size, 32U);
    EXPECT_EQ(provenance.frame_size, 64U);
    EXPECT_EQ(provenance.version, WIRELAB_VERSION);
    EXPECT_FALSE(provenance.build_type.empty());
    EXPECT_FALSE(provenance.generated_at.empty());
    EXPECT_EQ(provenance.generated_at.back(), 'Z');
    EXPECT_EQ(provenance.backends_present, Session::available_backends());
    // A backend can be compiled in without the machine having the device, never
    // the other way round.
    for (const auto& present : provenance.backends_present)
      EXPECT_NE(
          std::find(provenance.backends_compiled_in.begin(), provenance.backends_compiled_in.end(), present),
          provenance.backends_compiled_in.end());
  }

  TEST(SessionTest, ExportsReportAsJsonAndCsv)
  {
    Session session;
    EXPECT_FALSE(session.export_report("/tmp/wirelab-never-written.json"));
    EXPECT_TRUE(contains(session.report_export_path(), "before exporting"));

    session.run_benchmark_report("port-scan", 256, 32, 64, 3);
    run_report_to_completion(session);
    const auto row_count = session.report_rows().size();
    ASSERT_GT(row_count, 0U);

    const auto base = (std::filesystem::temp_directory_path() / "wirelab-session-report").string();
    const auto json_path = base + ".json";
    const auto csv_path = base + ".csv";
    std::filesystem::remove(json_path);
    std::filesystem::remove(csv_path);

    ASSERT_TRUE(session.export_report(json_path));
    EXPECT_EQ(session.report_export_path(), json_path);

    const auto json = read_file(json_path);
    EXPECT_TRUE(contains(json, "\"provenance\""));
    EXPECT_TRUE(contains(json, "\"scenario\": \"port-scan\""));
    EXPECT_TRUE(contains(json, "\"results\""));
    EXPECT_TRUE(contains(json, "\"backend\": \"CPU\""));
    EXPECT_TRUE(contains(json, "\"backendId\": \"cpu\""));

    const auto csv = read_lines(csv_path);
    ASSERT_EQ(csv.size(), row_count + 1);
    EXPECT_EQ(csv.front().rfind("backend,scenario,packets", 0), 0U);
    EXPECT_EQ(csv.at(1).rfind("CPU,port-scan,256", 0), 0U);

    std::filesystem::remove(json_path);
    std::filesystem::remove(csv_path);
  }

  TEST(SessionTest, RejectsInvalidReportSettings)
  {
    Session session;
    session.run_benchmark_report("no-such-scenario", 256, 32, 64, 1);
    EXPECT_FALSE(session.report_running());
    EXPECT_TRUE(contains(session.status_message(), "invalid"));

    session.run_benchmark_report("mixed-traffic", 256, 32, 8, 1);
    EXPECT_FALSE(session.report_running());
    EXPECT_TRUE(session.report_rows().empty());
  }
}  // namespace wirelab
