// SPDX-License-Identifier: Apache-2.0
#pragma once

// Compatibility shim. The previous public header was qos_profile.h; the
// types have moved to qos.h alongside the new opaque WriterQos / ReaderQos
// wrappers. New code should include "pub_sub_open_dds/qos.h" directly.
#include "pub_sub_open_dds/qos.h"
