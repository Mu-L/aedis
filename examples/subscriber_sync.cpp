/* Copyright (c) 2018-2022 Marcelo Zimbres Silva (mzimbres@gmail.com)
 *
 * Distributed under the Boost Software License, Version 1.0. (See
 * accompanying file LICENSE.txt)
 */

#include <tuple>
#include <string>
#include <boost/asio.hpp>
#include <aedis.hpp>
#include <aedis/experimental/sync.hpp>
#include "print.hpp"

// Include this in no more than one .cpp file.
#include <aedis/src.hpp>

namespace net = boost::asio;
using aedis::resp3::request;
using aedis::experimental::exec;
using aedis::experimental::receive_event;
using connection = aedis::connection<>;
using aedis::resp3::node;
using event = connection::event;

// See subscriber.cpp for more info about how to run this example.

void subscriber(connection& conn)
{
   request req;
   req.push("SUBSCRIBE", "channel");

   for (std::vector<node<std::string>> resp;;) {
      auto const ev = receive_event(conn, aedis::adapt(resp));
      switch (ev) {
         case connection::event::push:
         print_push(resp);
         resp.clear();
         break;

         case connection::event::hello:
         // Subscribes to the channels when a new connection is
         // stablished.
         exec(conn, req);
         break;

         default:;
      }
   }
}

int main()
{
   try {
      net::io_context ioc{1};
      connection conn{ioc};

      conn.get_config().enable_events = true;
      conn.get_config().enable_reconnect = true;

      std::thread thread{[&]() {
         conn.async_run(net::detached);
         ioc.run();
      }};

      subscriber(conn);
      thread.join();

   } catch (std::exception const& e) {
      std::cerr << e.what() << std::endl;
   }
}
