/* Copyright (c) 2019 - 2020 Marcelo Zimbres Silva (mzimbres at gmail dot com)
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include <boost/asio.hpp>
#include <aedis/aedis.hpp>

namespace net = aedis::net;
namespace this_coro = net::this_coro;

using namespace aedis;
using tcp = net::ip::tcp;
using tcp_socket = net::use_awaitable_t<>::as_default_on_t<tcp::socket>;
using stimer = net::use_awaitable_t<>::as_default_on_t<net::steady_timer>;

net::awaitable<void> publisher()
{
   auto ex = co_await this_coro::executor;
   try {
      tcp::resolver resv(ex);
      auto const r = resv.resolve("127.0.0.1", "6379");
      tcp_socket socket {ex};
      co_await async_connect(socket, r);

      std::string buffer;
      for (;;) {
	 resp::request req;
	 req.hello();
	 req.publish("channel", "12345");
	 co_await async_write(socket, req);
	 resp::response_ignore res;
	 co_await resp::async_read(socket, buffer, res);
	 co_await resp::async_read(socket, buffer, res);
	 stimer timer(ex);
	 timer.expires_after(std::chrono::seconds{2});
	 co_await timer.async_wait();
      }
   } catch (std::exception const& e) {
      std::cerr << "Error: " << e.what() << std::endl;
   }
}

net::awaitable<void> subscriber()
{
   auto ex = co_await this_coro::executor;
   try {
      resp::request req;
      req.subscribe("channel");

      tcp::resolver resv(ex);
      auto const r = resv.resolve("127.0.0.1", "6379");
      tcp_socket socket {ex};
      co_await async_connect(socket, r);
      co_await async_write(socket, req);

      std::string buffer;

      // Reads the answer to the subscribe.
      resp::response_ignore res;
      co_await resp::async_read(socket, buffer, res);

      // Reads published messages.
      for (;;) {
	 resp::response_static_array<std::string, 3> res;
	 co_await resp::async_read(socket, buffer, res);
	 print(res.result);
      }
   } catch (std::exception const& e) {
      std::cout << e.what() << std::endl;
   }
}

int main()
{
   net::io_context ioc {1};
   co_spawn(ioc, publisher(), net::detached);
   co_spawn(ioc, subscriber(), net::detached);
   ioc.run();
}
