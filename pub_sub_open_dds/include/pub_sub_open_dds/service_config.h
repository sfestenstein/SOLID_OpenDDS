// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace pub_sub_open_dds {

// Inputs to Service::pre_activate. Façade types only — no DDS::DomainId_t,
// no ACE_TCHAR**. The runtime adapter is responsible for translating
// `runtime_args` into whatever its underlying transport expects (the OpenDDS
// runtime feeds them into TheParticipantFactoryWithArgs).
//
// `config_file`, when set, is a convenience that the OpenDDS runtime
// converts into `-DCPSConfigFile <path>` before forwarding. Callers can
// instead pass that flag directly via `runtime_args` if they prefer.
struct ServiceConfig {
  int                                    domain_id = 42;
  std::vector<std::string>               runtime_args;
  std::optional<std::filesystem::path>   config_file;
};

} // namespace pub_sub_open_dds
