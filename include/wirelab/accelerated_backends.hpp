#ifndef PROJECT_ACCELERATED_BACKENDS_HPP_
#define PROJECT_ACCELERATED_BACKENDS_HPP_

#include "wirelab/benchmark.hpp"

namespace wirelab
{
  // The core library links neither CUDA nor Metal, so backend selection lives in
  // the one translation unit that is compiled with those options and knows which
  // of them exist. Every host that runs benchmarks - the CLI and the control
  // service - asks here, so a backend name means the same thing in all of them.
  [[nodiscard]] BenchmarkBackendFactory accelerated_benchmark_backend_factory();

  // Same rule for the frames a benchmark analyses: only the translation unit
  // compiled with the accelerators knows which GPU generators exist.
  [[nodiscard]] TrafficSourceFactory accelerated_traffic_source_factory();

  // True when this build could construct the named backend at all, ignoring
  // whether the machine currently has the device. Lets a caller tell "this build
  // has no CUDA" apart from "this machine has no CUDA device".
  [[nodiscard]] bool benchmark_backend_is_compiled_in(std::string_view backend) noexcept;
}  // namespace wirelab

#endif
