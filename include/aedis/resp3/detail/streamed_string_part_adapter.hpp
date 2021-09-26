/* Copyright (c) 2019 - 2021 Marcelo Zimbres Silva (mzimbres at gmail dot com)
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#pragma once

#include <aedis/resp3/type.hpp>
#include <aedis/resp3/response_adapter_base.hpp>

namespace aedis { namespace resp3 { namespace detail {

struct streamed_string_part_adapter : public response_adapter_base {
   streamed_string_part_type* result = nullptr;

   streamed_string_part_adapter(streamed_string_part_type* p) : result(p) {}

   void on_streamed_string_part(std::string_view s) override
      { *result += s; }
};

} // detail
} // resp3
} // aedis
