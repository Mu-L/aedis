/* Copyright (c) 2018-2022 Marcelo Zimbres Silva (mzimbres@gmail.com)
 *
 * Distributed under the Boost Software License, Version 1.0. (See
 * accompanying file LICENSE.txt)
 */

#include <string>
#include <iostream>
#include <boost/asio.hpp>
#include <aedis/aedis.hpp>
#include <aedis/src.hpp>

namespace net = boost::asio;
using aedis::adapt;
using aedis::command;
using aedis::resp3::request;
using tcp_socket = net::use_awaitable_t<>::as_default_on_t<net::ip::tcp::socket>;
using tcp_acceptor = net::use_awaitable_t<>::as_default_on_t<net::ip::tcp::acceptor>;
using connection = aedis::connection<tcp_socket>;

net::awaitable<void> echo_loop(tcp_socket socket, std::shared_ptr<connection> db)
{
   try {
      for (std::string buffer;;) {
         auto n = co_await net::async_read_until(socket, net::dynamic_buffer(buffer, 1024), "\n");
         request req;
         req.push(command::ping, buffer);
         std::tuple<std::string> resp;
         co_await db->async_exec(req, adapt(resp));
         co_await net::async_write(socket, net::buffer(std::get<0>(resp)));
         buffer.erase(0, n);
      }
   } catch (std::exception const& e) {
      std::cout << e.what() << std::endl;
   }
}

net::awaitable<void> listener(bool coalesce_requests)
{
   auto ex = co_await net::this_coro::executor;
   connection::config cfg;
   cfg.coalesce_requests = coalesce_requests;
   auto db = std::make_shared<connection>(ex, cfg);
   db->async_run("127.0.0.1", "6379", net::detached);

   tcp_acceptor acc(ex, {net::ip::tcp::v4(), 55555});
   for (;;)
      net::co_spawn(ex, echo_loop(co_await acc.async_accept(), db), net::detached);
}

int main(int argc, char* argv[])
{
   try {
      net::io_context ioc;
      co_spawn(ioc, listener(argc == 1), net::detached);
      ioc.run();
   } catch (std::exception const& e) {
      std::cerr << e.what() << std::endl;
   }
}