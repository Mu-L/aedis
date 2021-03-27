/* Copyright (c) 2019 - 2021 Marcelo Zimbres Silva (mzimbres at gmail dot com)
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include <aedis/aedis.hpp>
#include <aedis/detail/utils.hpp>

using namespace aedis;

// Low level async example.

net::awaitable<void> example()
{
   try {
      auto ex = co_await net::this_coro::executor;

      request req;
      req.rpush("list", {1, 2, 3});
      req.lrange("list", 0, -1);
      req.sadd("set", std::set<int>{3, 4, 5});
      req.smembers("set");
      req.quit();

      net::ip::tcp::resolver resv(ex);
      auto const results = resv.resolve("127.0.0.1", "6379");
      net::ip::tcp::socket socket {ex};
      co_await net::async_connect(socket, results, net::use_awaitable);
      co_await async_write(socket, req, net::use_awaitable);

      std::string buffer;
      for (;;) {
         switch (req.cmds.front()) {
            case command::lrange:
            {
               resp::response_array res;
               co_await async_read(socket, buffer, res, net::use_awaitable);
            } break;
            default:
            {
               resp::response_ignore res;
               co_await async_read(socket, buffer, res, net::use_awaitable);
            }
         }
         req.cmds.pop();
      }
   } catch (std::exception const& e) {
      std::cerr << e.what() << std::endl;
   }
}

int main()
{
   net::io_context ioc {1};
   net::co_spawn(ioc, example(), net::detached);
   ioc.run();
}
