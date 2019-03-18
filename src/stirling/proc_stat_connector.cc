#include <fstream>

#include "src/common/error.h"
#include "src/stirling/proc_stat_connector.h"

namespace pl {
namespace stirling {

// Temporary data source for M2. We plan to remove this data source
// once the ebpf version is available.
// Using data from /proc/stat
Status ProcStatConnector::InitImpl() {
  std::ifstream input_file(kProcStatFileName);
  if (!input_file.good()) {
    return error::NotFound("[$0] Unable to access $1", source_name(), kProcStatFileName);
  }

  auto parsed_str = GetProcParams();
  return GetProcStat(parsed_str);
}

// Version of InitImpl for FakeProcStatConnector
Status FakeProcStatConnector::InitImpl() {
  auto parsed_str = GetProcParams();
  return GetProcStat(parsed_str);
}

std::vector<std::string> ProcStatConnector::GetProcParams() {
  std::ifstream input_file(kProcStatFileName);
  std::vector<std::string> parsed_str;
  if (input_file.good()) {
    // Parse the first line in proc stat.
    std::string cpu_stat_str;
    std::getline(input_file, cpu_stat_str);

    // Remove whitespaces and split string.
    parsed_str = absl::StrSplit(cpu_stat_str, ' ', absl::SkipWhitespace());
  }
  return parsed_str;
}

Status ProcStatConnector::GetProcStat(const std::vector<std::string>& parsed_str) {
  if (parsed_str.empty()) {
    return error::InvalidArgument("Did not receive data from /proc/stat");
  }

  // parsed_str includes the string cpu at the front in addition to the stats.
  if (parsed_str.size() !=
      static_cast<std::vector<std::string>::size_type>(kNumCPUStatFields + 1)) {
    return error::InvalidArgument("parsed proc stat does not have the expected number of fields");
  }

  // Get the stats
  // user
  auto user_cpu = std::atoi(parsed_str[kUserIdx].c_str());
  // idle + iowait
  auto idle_cpu =
      std::atoi(parsed_str[kIdleIdx].c_str()) + std::atoi(parsed_str[kIOWaitIdx].c_str());

  auto total_cpu = 0;
  for (int i = 1; i <= kNumCPUStatFields; ++i) {
    total_cpu += std::atoi(parsed_str[i].c_str());
  }
  // nice + system + irq + softirq + steal + guest + guest_nice
  auto system_cpu = total_cpu - user_cpu - idle_cpu;

  auto now = std::chrono::steady_clock::now();
  cpu_usage_.time_stamp =
      std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();

  // Change in cpu stats since last poll.
  auto delta_total = static_cast<double>(total_cpu - prev_cpu_usage_.total);
  auto delta_system = static_cast<double>(system_cpu - prev_cpu_usage_.system);
  auto delta_user = static_cast<double>(user_cpu - prev_cpu_usage_.user);
  auto delta_idle = static_cast<double>(idle_cpu - prev_cpu_usage_.idle);

  // Calculate percentage cpu stats between current and previous poll.
  cpu_usage_.system_percent = delta_system * 100.0 / delta_total;
  cpu_usage_.user_percent = delta_user * 100.0 / delta_total;
  cpu_usage_.idle_percent = delta_idle * 100.0 / delta_total;

  prev_cpu_usage_.total = total_cpu;
  prev_cpu_usage_.system = system_cpu;
  prev_cpu_usage_.user = user_cpu;
  prev_cpu_usage_.idle = idle_cpu;

  return Status::OK();
}

void ProcStatConnector::TransferDataImpl(ColumnWrapperRecordBatch* record_batch) {
  auto& columns = *record_batch;

  auto parsed_str = GetProcParams();
  PL_CHECK_OK(GetProcStat(parsed_str));

  std::static_pointer_cast<types::Time64NSValueColumnWrapper>(columns[0])
      ->Append(cpu_usage_.time_stamp);
  std::static_pointer_cast<types::Float64ValueColumnWrapper>(columns[1])
      ->Append(cpu_usage_.system_percent);
  std::static_pointer_cast<types::Float64ValueColumnWrapper>(columns[2])
      ->Append(cpu_usage_.user_percent);
  std::static_pointer_cast<types::Float64ValueColumnWrapper>(columns[3])
      ->Append(cpu_usage_.idle_percent);
}

}  // namespace stirling
}  // namespace pl
