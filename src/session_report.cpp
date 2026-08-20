#include "wirelab/session.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <ctime>
#include <fstream>
#include <ios>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "session_detail.hpp"
#include "wirelab/accelerated_backends.hpp"
#include "wirelab/benchmark.hpp"
#include "wirelab/ethernet_frame.hpp"
#include "wirelab/version.hpp"

// The build type is stamped in by the build system; a translation unit compiled
// without it still has to name something in the provenance block.
#ifndef WIRELAB_BUILD_TYPE
#define WIRELAB_BUILD_TYPE "Unknown"
#endif

namespace wirelab
{
  namespace
  {
    // Second resolution, UTC, ISO-8601: the same spelling the Qt frontend wrote
    // so a report generated before and after the port compares byte for byte.
    std::string iso8601_utc_now()
    {
      const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
      std::tm utc{};
#ifdef _WIN32
      gmtime_s(&utc, &now);
#else
      gmtime_r(&now, &utc);
#endif
      std::array<char, 32> buffer{};
      const size_t written = std::strftime(buffer.data(), buffer.size(), "%Y-%m-%dT%H:%M:%SZ", &utc);
      return std::string(buffer.data(), written);
    }

    std::string json_text_field(const char* key, const std::string& value)
    {
      std::string field = "\"";
      field += key;
      field += "\": \"";
      field += session_detail::json_escape(value);
      field += '"';
      return field;
    }

    // For numbers and arrays, which arrive already spelled the way JSON wants.
    std::string json_raw_field(const char* key, const std::string& value)
    {
      std::string field = "\"";
      field += key;
      field += "\": ";
      field += value;
      return field;
    }

    std::string json_string_array(const std::vector<std::string>& values)
    {
      std::string literal = "[";
      for (size_t index = 0; index < values.size(); ++index)
      {
        if (index != 0)
          literal += ", ";
        literal += '"';
        literal += session_detail::json_escape(values[index]);
        literal += '"';
      }
      literal += ']';
      return literal;
    }

    std::string join_json_fields(const std::vector<std::string>& fields, const char* indent)
    {
      std::string block;
      for (size_t index = 0; index < fields.size(); ++index)
      {
        block += indent;
        block += fields[index];
        block += index + 1 == fields.size() ? "\n" : ",\n";
      }
      return block;
    }

    std::vector<std::string> report_row_json_fields(const ReportRow& row)
    {
      return { json_text_field("backend", row.backend_label),
               json_text_field("backendId", row.backend_id),
               json_text_field("scenario", row.scenario),
               json_raw_field("packets", std::to_string(row.packets)),
               json_raw_field("elapsedNs", std::to_string(row.elapsed_ns)),
               json_raw_field("packetsPerSecond", session_detail::format_json_number(row.packets_per_second)),
               json_raw_field("goodputBitsPerSecond", session_detail::format_json_number(row.goodput_bits_per_second)),
               json_raw_field("lossPercent", session_detail::format_json_number(row.loss_percent)),
               json_raw_field("latencyP50Ns", std::to_string(row.latency_p50_ns)),
               json_raw_field("latencyP95Ns", std::to_string(row.latency_p95_ns)),
               json_raw_field("latencyP99Ns", std::to_string(row.latency_p99_ns)),
               json_raw_field("hostToDeviceNs", std::to_string(row.host_to_device_ns)),
               json_raw_field("kernelNs", std::to_string(row.kernel_ns)),
               json_raw_field("deviceToHostNs", std::to_string(row.device_to_host_ns)),
               json_raw_field("transferInclusiveNs", std::to_string(row.transfer_inclusive_ns)),
               json_raw_field("queueWaitNs", std::to_string(row.queue_wait_ns)),
               json_raw_field("speedup", session_detail::format_json_number(row.speedup)) };
    }

    std::string report_json(const ReportProvenance& provenance, const std::vector<ReportRow>& rows)
    {
      const std::vector<std::string> provenance_fields{
        json_text_field("scenario", provenance.scenario),
        json_raw_field("seed", std::to_string(provenance.seed)),
        json_raw_field("packets", std::to_string(provenance.packets)),
        json_raw_field("batchSize", std::to_string(provenance.batch_size)),
        json_raw_field("frameSize", std::to_string(provenance.frame_size)),
        json_raw_field("hostCount", std::to_string(provenance.host_count)),
        json_text_field("generator", provenance.generator),
        json_text_field("version", provenance.version),
        json_text_field("buildType", provenance.build_type),
        json_raw_field("backendsCompiledIn", json_string_array(provenance.backends_compiled_in)),
        json_raw_field("backendsPresent", json_string_array(provenance.backends_present)),
        json_text_field("generatedAt", provenance.generated_at)
      };

      std::string document = "{\n  \"provenance\": {\n";
      document += join_json_fields(provenance_fields, "    ");
      document += "  },\n  \"results\": [";
      if (rows.empty())
      {
        document += "]\n}\n";
        return document;
      }
      document += '\n';
      for (size_t index = 0; index < rows.size(); ++index)
      {
        document += "    {\n";
        document += join_json_fields(report_row_json_fields(rows[index]), "      ");
        document += index + 1 == rows.size() ? "    }\n" : "    },\n";
      }
      document += "  ]\n}\n";
      return document;
    }

    // The CSV carries the same rows in a fixed column order so a spreadsheet and
    // the JSON never disagree about what a column means. The array sizes tie the
    // header to the values: a column added on one side stops the build.
    constexpr std::array<const char*, 16> CSV_COLUMNS{ "backend",        "scenario",            "packets",
                                                       "elapsedNs",      "packetsPerSecond",    "goodputBitsPerSecond",
                                                       "lossPercent",    "latencyP50Ns",        "latencyP95Ns",
                                                       "latencyP99Ns",   "hostToDeviceNs",      "kernelNs",
                                                       "deviceToHostNs", "transferInclusiveNs", "queueWaitNs",
                                                       "speedup" };

    std::array<std::string, CSV_COLUMNS.size()> csv_fields(const ReportRow& row)
    {
      return { row.backend_label,
               row.scenario,
               std::to_string(row.packets),
               std::to_string(row.elapsed_ns),
               session_detail::format_general(row.packets_per_second),
               session_detail::format_general(row.goodput_bits_per_second),
               session_detail::format_general(row.loss_percent),
               std::to_string(row.latency_p50_ns),
               std::to_string(row.latency_p95_ns),
               std::to_string(row.latency_p99_ns),
               std::to_string(row.host_to_device_ns),
               std::to_string(row.kernel_ns),
               std::to_string(row.device_to_host_ns),
               std::to_string(row.transfer_inclusive_ns),
               std::to_string(row.queue_wait_ns),
               session_detail::format_general(row.speedup) };
    }

    template <typename Fields>
    void append_csv_line(std::string& csv, const Fields& fields)
    {
      for (size_t column = 0; column < fields.size(); ++column)
      {
        csv += fields[column];
        csv += column + 1 == fields.size() ? '\n' : ',';
      }
    }

    bool write_text_file(const std::string& path, const std::string& text)
    {
      std::ofstream file(path, std::ios::binary | std::ios::trunc);
      if (!file)
        return false;
      file << text;
      file.flush();
      return file.good();
    }

    // The Qt version chopped a trailing ".json" so a dialog that already added
    // the suffix does not produce report.json.json.
    bool ends_with_json(const std::string& path)
    {
      constexpr std::string_view suffix = ".json";
      if (path.size() < suffix.size())
        return false;
      return std::equal(
          suffix.begin(),
          suffix.end(),
          path.end() - static_cast<std::string::difference_type>(suffix.size()),
          [](char expected, char actual)
          { return expected == static_cast<char>(std::tolower(static_cast<unsigned char>(actual))); });
    }
  }  // namespace

  void Session::run_benchmark_report(
      const std::string& scenario,
      int32_t packets,
      int32_t batch_size,
      int32_t frame_size,
      int32_t seed)
  {
    if (report_running_)
    {
      set_status("A benchmark report is already running.");
      return;
    }
    const auto parsed_scenario = traffic_scenario_from_string(scenario);
    if (!parsed_scenario || packets <= 0 || batch_size <= 0 || seed < 0 ||
        frame_size < static_cast<int32_t>(ETHERNET_HEADER_SIZE) || frame_size > static_cast<int32_t>(MAX_BENCHMARK_FRAME_SIZE))
    {
      set_status("Report settings are invalid.");
      return;
    }

    report_config_ = BenchmarkConfig{};
    report_config_.traffic.scenario = parsed_scenario.value();
    report_config_.traffic.seed = static_cast<uint64_t>(seed);
    report_config_.traffic.frame_size = static_cast<size_t>(frame_size);
    report_config_.packet_count = static_cast<size_t>(packets);
    report_config_.batch_size = static_cast<size_t>(batch_size);
    report_queue_ = available_backends();
    report_index_ = 0;
    report_results_.clear();
    report_rows_.clear();
    report_export_path_.clear();
    report_completed_packets_ = 0;
    report_total_packets_ = report_config_.packet_count * static_cast<uint64_t>(report_queue_.size());
    // Twenty slices per backend keeps the GUI responsive without paying for a
    // tick per batch; a slice never splits a batch, so the counters are the
    // counters of an unsliced run either way.
    report_slice_budget_ = std::max<size_t>(report_config_.batch_size, (report_config_.packet_count + 19) / 20);
    report_running_ = true;
    report_progress_ = 0.0;
    rebuild_report_provenance();
    if (!begin_next_report_backend())
    {
      finish_report();
      return;
    }
    set_status("Benchmark report running on " + std::to_string(report_queue_.size()) + " backend(s).");
    mark(SessionDirty::Report);
  }

  bool Session::begin_next_report_backend()
  {
    while (report_index_ < report_queue_.size())
    {
      BenchmarkConfig config = report_config_;
      config.backend = session_detail::benchmark_backend_id(report_queue_[report_index_]);
      auto run = BenchmarkRun::create(config, accelerated_benchmark_backend_factory());
      if (run)
      {
        report_run_.emplace(std::move(run.value()));
        report_stage_ = "Measuring " + report_queue_[report_index_] + " (" + std::to_string(report_index_ + 1) + " of " +
                        std::to_string(report_queue_.size()) + ")";
        return true;
      }
      // A device can vanish between listing the backends and measuring one; its
      // share of the work still counts as done so progress reaches the end.
      report_completed_packets_ += report_config_.packet_count;
      ++report_index_;
    }
    return false;
  }

  void Session::run_report_step()
  {
    if (!report_running_)
      return;
    if (!report_run_ && !begin_next_report_backend())
    {
      finish_report();
      return;
    }
    const auto before = report_run_->completed_packets();
    report_run_->advance(report_slice_budget_);
    report_completed_packets_ += report_run_->completed_packets() - before;
    if (report_run_->finished())
    {
      report_results_.push_back(report_run_->result());
      report_run_.reset();
      ++report_index_;
      rebuild_report_rows();
      if (!begin_next_report_backend())
      {
        finish_report();
        return;
      }
    }
    report_progress_ = report_total_packets_ == 0
                           ? 1.0
                           : static_cast<double>(report_completed_packets_) / static_cast<double>(report_total_packets_);
    mark(SessionDirty::Report);
  }

  void Session::finish_report()
  {
    report_run_.reset();
    report_running_ = false;
    report_progress_ = 1.0;
    report_stage_ = std::to_string(report_rows_.size()) + " backend(s) measured · " +
                    std::to_string(report_config_.packet_count) + " packets each";
    set_status("Benchmark report complete.");
    mark(SessionDirty::Report);
  }

  void Session::rebuild_report_rows()
  {
    double cpu_packets_per_second = 0.0;
    for (const auto& result : report_results_)
      if (result.backend == "cpu")
        cpu_packets_per_second = result.packets_per_second;
    report_rows_.clear();
    for (const auto& result : report_results_)
    {
      ReportRow row;
      row.backend_label = session_detail::benchmark_backend_label(result.backend);
      row.backend_id = result.backend;
      row.scenario = result.scenario;
      row.packets = result.completed_packets;
      row.elapsed_ns = result.elapsed_ns;
      row.packets_per_second = result.packets_per_second;
      row.goodput_bits_per_second = result.goodput_bits_per_second;
      row.loss_percent = result.loss_percentage;
      row.latency_p50_ns = result.batch_analysis_latency_p50_ns;
      row.latency_p95_ns = result.batch_analysis_latency_p95_ns;
      row.latency_p99_ns = result.batch_analysis_latency_p99_ns;
      row.host_to_device_ns = result.timing.host_to_device_ns;
      row.kernel_ns = result.timing.kernel_ns;
      row.device_to_host_ns = result.timing.device_to_host_ns;
      row.transfer_inclusive_ns = result.timing.transfer_inclusive_ns;
      row.queue_wait_ns = result.timing.queue_wait_ns;
      // Without a CPU measurement there is nothing to be a multiple of.
      row.speedup = cpu_packets_per_second <= 0.0 ? 0.0 : result.packets_per_second / cpu_packets_per_second;
      report_rows_.push_back(std::move(row));
    }
  }

  void Session::rebuild_report_provenance()
  {
    ReportProvenance provenance;
    for (const char* label : { "CPU", "CUDA", "Metal", "Metal (live)" })
      if (benchmark_backend_is_compiled_in(session_detail::benchmark_backend_id(label)))
        provenance.backends_compiled_in.emplace_back(label);
    provenance.scenario = to_string(report_config_.traffic.scenario);
    provenance.seed = report_config_.traffic.seed;
    provenance.packets = report_config_.packet_count;
    provenance.batch_size = report_config_.batch_size;
    provenance.frame_size = report_config_.traffic.frame_size;
    provenance.host_count = report_config_.traffic.host_count;
    provenance.generator = report_config_.generator;
    provenance.version = WIRELAB_VERSION;
    provenance.build_type = WIRELAB_BUILD_TYPE;
    provenance.backends_present = available_backends();
    provenance.generated_at = iso8601_utc_now();
    report_provenance_ = std::move(provenance);
  }

  bool Session::export_report(const std::string& path)
  {
    const auto fail = [this](std::string message)
    {
      report_export_path_ = message;
      set_status(std::move(message));
      mark(SessionDirty::Report);
      return false;
    };
    if (report_rows_.empty())
      return fail("Run a benchmark report before exporting.");

    std::string local_path = session_detail::strip_file_url(path);
    if (ends_with_json(local_path))
      local_path.resize(local_path.size() - 5);
    if (local_path.empty())
      return fail("Choose a file name for the report.");
    const std::string json_path = local_path + ".json";
    const std::string csv_path = local_path + ".csv";

    if (!write_text_file(json_path, report_json(report_provenance_, report_rows_)))
      return fail("Could not write " + json_path);

    std::string csv;
    append_csv_line(csv, CSV_COLUMNS);
    for (const auto& row : report_rows_)
      append_csv_line(csv, csv_fields(row));
    if (!write_text_file(csv_path, csv))
      return fail("Could not write " + csv_path);

    report_export_path_ = json_path;
    set_status(
        "Exported report to " + session_detail::file_name_of(json_path) + " and " + session_detail::file_name_of(csv_path));
    mark(SessionDirty::Report);
    return true;
  }
}  // namespace wirelab
