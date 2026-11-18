// SPDX-License-Identifier: GPL-3.0-or-later
#include "ps5_http.hpp"

namespace medic {

std::unique_ptr<IHttpClient> make_ps5_http_client() { return make_posix_http_client(); }

}  // namespace medic
