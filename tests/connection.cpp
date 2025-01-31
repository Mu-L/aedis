/* Copyright (c) 2018-2022 Marcelo Zimbres Silva (mzimbres@gmail.com)
 *
 * Distributed under the Boost Software License, Version 1.0. (See
 * accompanying file LICENSE.txt)
 */

// TODO: Avoid usage of co_await to improve tests is compilers that
// don't support it.
// TODO: Add reconnect test that kills the server and waits some
// seconds.

#include <iostream>
#include <boost/asio.hpp>
#include <boost/system/errc.hpp>
#include <boost/asio/experimental/as_tuple.hpp>

#include <aedis.hpp>
#include <aedis/src.hpp>

#include "check.hpp"

namespace net = boost::asio;

using aedis::resp3::request;
using connection = aedis::connection<>;
using error_code = boost::system::error_code;
using net::experimental::as_tuple;

bool is_host_not_found(boost::system::error_code ec)
{
   if (ec == net::error::netdb_errors::host_not_found) return true;
   if (ec == net::error::netdb_errors::host_not_found_try_again) return true;
   return false;
}

//----------------------------------------------------------------

// Tests whether resolve fails with the correct error.
void test_resolve()
{
   connection::config cfg;
   cfg.host = "Atibaia";
   cfg.port = "6379";
   cfg.resolve_timeout = std::chrono::seconds{100};

   net::io_context ioc;
   connection db{ioc, cfg};
   db.async_run([](auto ec) {
      expect_true(is_host_not_found(ec), "test_resolve");
   });

   ioc.run();
}

//----------------------------------------------------------------

void test_connect()
{
   connection::config cfg;
   cfg.host = "127.0.0.1";
   cfg.port = "1";
   cfg.connect_timeout = std::chrono::seconds{100};

   net::io_context ioc;
   connection db{ioc, cfg};
   db.async_run([](auto ec) {
      expect_error(ec, net::error::basic_errors::connection_refused, "test_connect");
   });
   ioc.run();
}

//----------------------------------------------------------------

// Test if quit causes async_run to exit.
void test_quit1(connection::config const& cfg)
{
   net::io_context ioc;
   auto db = std::make_shared<connection>(ioc, cfg);

   request req;
   req.push("QUIT");

   db->async_exec(req, aedis::adapt(), [](auto ec, auto){
      expect_no_error(ec, "test_quit1");
   });

   db->async_run([](auto ec){
      expect_error(ec, net::error::misc_errors::eof, "test_quit1");
   });

   ioc.run();
}

void test_quit2(connection::config const& cfg)
{
   std::cout << "test_quit2" << std::endl;
   request req;
   req.push("QUIT");

   net::io_context ioc;
   auto db = std::make_shared<connection>(ioc, cfg);
   db->async_run(req, aedis::adapt(), [](auto ec, auto){
      expect_no_error(ec, "test_quit2");
   });

   ioc.run();
}

void test_quit()
{
   connection::config cfg;

   cfg.coalesce_requests = true;
   test_quit1(cfg);

   cfg.coalesce_requests = false;
   test_quit1(cfg);

   cfg.coalesce_requests = true;
   test_quit2(cfg);

   cfg.coalesce_requests = false;
   test_quit2(cfg);
}

// Checks whether we get idle timeout when no push reader is set.
void test_missing_push_reader1(connection::config const& cfg)
{
   std::cout << "test_missing_push_reader1" << std::endl;
   net::io_context ioc;
   auto db = std::make_shared<connection>(ioc, cfg);

   request req;
   req.push("SUBSCRIBE", "channel");

   db->async_run(req, aedis::adapt(), [](auto ec, auto){
      expect_no_error(ec, "test_missing_push_reader1");
   });

   ioc.run();
}

void test_missing_push_reader2(connection::config const& cfg)
{
   std::cout << "test_missing_push_reader2" << std::endl;
   net::io_context ioc;
   auto db = std::make_shared<connection>(ioc, cfg);

   request req; // Wrong command syntax.
   req.push("SUBSCRIBE");

   db->async_run(req, aedis::adapt(), [](auto ec, auto){
      expect_no_error(ec, "test_missing_push_reader2");
   });

   ioc.run();
}

void test_missing_push_reader3(connection::config const& cfg)
{
   std::cout << "test_missing_push_reader3" << std::endl;
   net::io_context ioc;
   auto db = std::make_shared<connection>(ioc, cfg);

   request req; // Wrong command synthax.
   req.push("PING", "Message");
   req.push("SUBSCRIBE");

   db->async_run(req, aedis::adapt(), [](auto ec, auto){
      expect_no_error(ec, "test_missing_push_reader3");
   });

   ioc.run();
}

void test_idle()
{
   std::cout << "test_idle" << std::endl;
   connection::config cfg;
   cfg.resolve_timeout = std::chrono::seconds{1};
   cfg.connect_timeout = std::chrono::seconds{1};
   cfg.ping_interval = std::chrono::seconds{1};

   net::io_context ioc;
   auto db = std::make_shared<connection>(ioc, cfg);

   request req;
   req.push("CLIENT", "PAUSE", 5000);

   db->async_exec(req, aedis::adapt(), [](auto ec, auto){
      expect_no_error(ec, "test_idle");
   });

   db->async_run([](auto ec){
      expect_error(ec, aedis::error::idle_timeout, "test_idle");
   });

   ioc.run();
}

#ifdef BOOST_ASIO_HAS_CO_AWAIT
net::awaitable<void>
push_consumer1(std::shared_ptr<connection> db, bool& received, char const* msg)
{
   {
      auto [ec, ev] = co_await db->async_receive_event(aedis::adapt(), as_tuple(net::use_awaitable));
      expect_no_error(ec, msg);
      received = true;
   }

   {
      auto [ec, ev] = co_await db->async_receive_event(aedis::adapt(), as_tuple(net::use_awaitable));
      expect_error(ec, boost::asio::experimental::channel_errc::channel_cancelled, msg);
   }
}

void test_push_is_received1(connection::config const& cfg)
{
   std::cout << "test_push_is_received1" << std::endl;
   net::io_context ioc;
   auto db = std::make_shared<connection>(ioc, cfg);

   request req;
   req.push("SUBSCRIBE", "channel");
   req.push("QUIT");

   db->async_run(req, aedis::adapt(), [db](auto ec, auto){
      expect_no_error(ec, "test_push_is_received1");
      db->cancel_event_receiver();
   });

   bool received = false;
   net::co_spawn(
      ioc.get_executor(),
      push_consumer1(db, received, "test_push_is_received1"),
      net::detached);

   ioc.run();

   expect_true(received);
}

void test_push_is_received2(connection::config const& cfg)
{
   std::cout << "test_push_is_received2" << std::endl;
   request req1;
   req1.push("PING", "Message1");

   request req2;
   req2.push("SUBSCRIBE", "channel");

   request req3;
   req3.push("PING", "Message2");
   req3.push("QUIT");

   net::io_context ioc;

   auto db = std::make_shared<connection>(ioc, cfg);

   auto handler =[](auto ec, auto...)
      { expect_no_error(ec, "test_push_is_received2"); };

   db->async_exec(req1, aedis::adapt(), handler);
   db->async_exec(req2, aedis::adapt(), handler);
   db->async_exec(req3, aedis::adapt(), handler);

   db->async_run([db](auto ec, auto...) {
      expect_error(ec, net::error::misc_errors::eof, "test_push_is_received2");
      db->cancel_event_receiver();
   });

   bool received = false;
   net::co_spawn(
      ioc.get_executor(),
      push_consumer1(db, received, "test_push_is_received2"),
      net::detached);

   ioc.run();
   expect_true(received);
}

net::awaitable<void> test_reconnect_impl(std::shared_ptr<connection> db)
{
   request req;
   req.push("QUIT");

   for (auto i = 0;;) {
      auto ev = co_await db->async_receive_event(aedis::adapt(), net::use_awaitable);
      expect_eq(ev, connection::event::resolve, "test_reconnect.");

       ev = co_await db->async_receive_event(aedis::adapt(), net::use_awaitable);
      expect_eq(ev, connection::event::connect, "test_reconnect.");

      ev = co_await db->async_receive_event(aedis::adapt(), net::use_awaitable);
      expect_eq(ev, connection::event::hello, "test_reconnect.");

      co_await db->async_exec(req, aedis::adapt(), net::use_awaitable);

      // Test 5 reconnetions and returns.

      ++i;
      if (i == 5) {
         db->get_config().enable_reconnect = false;
         co_return;
      }
   }

   co_return;
}

// Test whether the client works after a reconnect.
void test_reconnect()
{
   std::cout << "Start: test_reconnect" << std::endl;
   net::io_context ioc;
   auto db = std::make_shared<connection>(ioc.get_executor());
   db->get_config().enable_events = true;
   db->get_config().enable_reconnect = true;
   db->get_config().reconnect_interval = std::chrono::milliseconds{100};

   net::co_spawn(ioc, test_reconnect_impl(db), net::detached);

   db->async_run([](auto ec) {
      expect_error(ec, net::error::misc_errors::eof, "test_reconnect.");
   });

   ioc.run();
   std::cout << "End: test_reconnect()" << std::endl;
}

net::awaitable<void>
push_consumer3(std::shared_ptr<connection> db)
{
   for (;;)
      co_await db->async_receive_event(aedis::adapt(), net::use_awaitable);
}

// Test many subscribe requests.
void test_push_many_subscribes(connection::config const& cfg)
{
   std::cout << "test_push_many_subscribes" << std::endl;
   request req0;
   req0.push("HELLO", 3);

   request req1;
   req1.push("PING", "Message1");

   request req2;
   req2.push("SUBSCRIBE", "channel");

   request req3;
   req3.push("QUIT");

   auto handler =[](auto ec, auto...)
   {
      expect_no_error(ec, "test_push_many_subscribes");
   };

   net::io_context ioc;
   auto db = std::make_shared<connection>(ioc, cfg);
   db->async_exec(req0, aedis::adapt(), handler);
   db->async_exec(req1, aedis::adapt(), handler);
   db->async_exec(req2, aedis::adapt(), handler);
   db->async_exec(req2, aedis::adapt(), handler);
   db->async_exec(req1, aedis::adapt(), handler);
   db->async_exec(req2, aedis::adapt(), handler);
   db->async_exec(req1, aedis::adapt(), handler);
   db->async_exec(req2, aedis::adapt(), handler);
   db->async_exec(req2, aedis::adapt(), handler);
   db->async_exec(req1, aedis::adapt(), handler);
   db->async_exec(req2, aedis::adapt(), handler);
   db->async_exec(req3, aedis::adapt(), handler);

   db->async_run([db](auto ec, auto...) {
      expect_error(ec, net::error::misc_errors::eof, "test_push_many_subscribes");
      db->cancel_event_receiver();
   });

   net::co_spawn(ioc.get_executor(), push_consumer3(db), net::detached);
   ioc.run();
}

#endif

void test_push()
{
   connection::config cfg;

   cfg.coalesce_requests = true;
#ifdef BOOST_ASIO_HAS_CO_AWAIT
   test_push_is_received1(cfg);
   test_push_is_received2(cfg);
   test_push_many_subscribes(cfg);
#endif
   test_missing_push_reader1(cfg);
   test_missing_push_reader3(cfg);

   cfg.coalesce_requests = false;
#ifdef BOOST_ASIO_HAS_CO_AWAIT
   test_push_is_received1(cfg);
   test_push_is_received2(cfg);
   test_push_many_subscribes(cfg);
#endif
   test_missing_push_reader2(cfg);
   test_missing_push_reader3(cfg);
}


int main()
{
   test_resolve();
   test_connect();
   test_quit();
   test_push();
#ifdef BOOST_ASIO_HAS_CO_AWAIT
   test_reconnect();
#endif

   // Must come last as it sends a client pause.
   test_idle();
}

