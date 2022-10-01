/* Copyright (c) 2018-2022 Marcelo Zimbres Silva (mzimbres@gmail.com)
 *
 * Distributed under the Boost Software License, Version 1.0. (See
 * accompanying file LICENSE.txt)
 */

#include <iostream>
#include <boost/asio.hpp>
#include <boost/system/errc.hpp>
#include <boost/asio/experimental/as_tuple.hpp>

#define BOOST_TEST_MODULE low level
#include <boost/test/included/unit_test.hpp>

#include <aedis.hpp>
#include <aedis/src.hpp>

namespace net = boost::asio;

using aedis::resp3::request;
using aedis::adapt;
using aedis::endpoint;
using connection = aedis::connection<>;
using error_code = boost::system::error_code;
using net::experimental::as_tuple;

// Checks whether we get idle timeout when no push reader is set.
void test_missing_push_reader1(bool coalesce)
{
   net::io_context ioc;
   auto db = std::make_shared<connection>(ioc);

   request req{{false, coalesce}};
   req.push("SUBSCRIBE", "channel");

   endpoint ep{"127.0.0.1", "6379"};
   db->async_run(ep, req, adapt(), [](auto ec, auto){
      BOOST_CHECK_EQUAL(ec, aedis::error::idle_timeout);
   });

   ioc.run();
}

void test_missing_push_reader2(bool coalesce)
{
   net::io_context ioc;
   auto db = std::make_shared<connection>(ioc);

   request req{{false, coalesce}}; // Wrong command syntax.
   req.push("SUBSCRIBE");

   endpoint ep{"127.0.0.1", "6379"};
   db->async_run(ep, req, adapt(), [](auto ec, auto){
      BOOST_CHECK_EQUAL(ec, aedis::error::idle_timeout);
   });

   ioc.run();
}

void test_missing_push_reader3(bool coalesce)
{
   net::io_context ioc;
   auto db = std::make_shared<connection>(ioc);

   request req{{false, coalesce}}; // Wrong command synthax.
   req.push("PING", "Message");
   req.push("SUBSCRIBE");

   endpoint ep{"127.0.0.1", "6379"};
   db->async_run(ep, req, adapt(), [](auto ec, auto){
      BOOST_CHECK_EQUAL(ec, aedis::error::idle_timeout);
   });

   ioc.run();
}

#ifdef BOOST_ASIO_HAS_CO_AWAIT
net::awaitable<void> push_consumer1(std::shared_ptr<connection> db, bool& push_received)
{
   {
      auto [ec, ev] = co_await db->async_receive_push(adapt(), as_tuple(net::use_awaitable));
      BOOST_TEST(!ec);
   }

   {
      auto [ec, ev] = co_await db->async_receive_push(adapt(), as_tuple(net::use_awaitable));
      BOOST_CHECK_EQUAL(ec, boost::asio::experimental::channel_errc::channel_cancelled);
   }

   push_received = true;
}

struct adapter_error {
   void
   operator()(
      std::size_t, aedis::resp3::node<boost::string_view> const&, boost::system::error_code& ec)
   {
      ec = aedis::error::incompatible_size;
   }

   [[nodiscard]]
   auto get_supported_response_size() const noexcept
      { return static_cast<std::size_t>(-1);}

   [[nodiscard]]
   auto get_max_read_size(std::size_t) const noexcept
      { return static_cast<std::size_t>(-1);}
};

BOOST_AUTO_TEST_CASE(test_push_adapter)
{
   net::io_context ioc;
   auto db = std::make_shared<connection>(ioc);

   request req;
   req.push("PING");
   req.push("SUBSCRIBE", "channel");
   req.push("PING");

   db->async_receive_push(adapter_error{}, [](auto ec, auto) {
      BOOST_CHECK_EQUAL(ec, aedis::error::incompatible_size);
   });

   endpoint ep{"127.0.0.1", "6379"};
   db->async_run(ep, req, adapt(), [db](auto, auto){
      //BOOST_CHECK_EQUAL(ec, net::error::misc_errors::eof);
   });

   ioc.run();

   // TODO: Reset the ioc reconnect and send a quit to ensure
   // reconnection is possible after an error.
}

void test_push_is_received1(bool coalesce)
{
   net::io_context ioc;
   auto db = std::make_shared<connection>(ioc);

   request req{{false, coalesce}};
   req.push("SUBSCRIBE", "channel");
   req.push("QUIT");

   endpoint ep{"127.0.0.1", "6379"};
   db->async_run(ep, req, adapt(), [db](auto ec, auto){
      BOOST_CHECK_EQUAL(ec, net::error::misc_errors::eof);
      db->cancel(connection::operation::receive_push);
   });

   bool push_received = false;
   net::co_spawn(
      ioc.get_executor(),
      push_consumer1(db, push_received),
      net::detached);

   ioc.run();

   BOOST_TEST(push_received);
}

void test_push_is_received2(bool coalesce)
{
   request req1{{false, coalesce}};
   req1.push("PING", "Message1");

   request req2{{false, coalesce}};
   req2.push("SUBSCRIBE", "channel");

   request req3{{false, coalesce}};
   req3.push("PING", "Message2");
   req3.push("QUIT");

   net::io_context ioc;

   auto db = std::make_shared<connection>(ioc);

   auto handler =[](auto ec, auto...)
   {
      BOOST_TEST(!ec);
   };

   db->async_exec(req1, adapt(), handler);
   db->async_exec(req2, adapt(), handler);
   db->async_exec(req3, adapt(), handler);

   endpoint ep{"127.0.0.1", "6379"};
   db->async_run(ep, [db](auto ec, auto...) {
      BOOST_CHECK_EQUAL(ec, net::error::misc_errors::eof);
      db->cancel(connection::operation::receive_push);
   });

   bool push_received = false;
   net::co_spawn(
      ioc.get_executor(),
      push_consumer1(db, push_received),
      net::detached);

   ioc.run();

   BOOST_TEST(push_received);
}

net::awaitable<void> push_consumer3(std::shared_ptr<connection> db)
{
   for (;;)
      co_await db->async_receive_push(adapt(), net::use_awaitable);
}

// Test many subscribe requests.
void test_push_many_subscribes(bool coalesce)
{
   request req0{{false, coalesce}};
   req0.push("HELLO", 3);

   request req1{{false, coalesce}};
   req1.push("PING", "Message1");

   request req2{{false, coalesce}};
   req2.push("SUBSCRIBE", "channel");

   request req3{{false, coalesce}};
   req3.push("QUIT");

   auto handler =[](auto ec, auto...)
   {
      BOOST_TEST(!ec);
   };

   net::io_context ioc;
   auto db = std::make_shared<connection>(ioc);
   db->async_exec(req0, adapt(), handler);
   db->async_exec(req1, adapt(), handler);
   db->async_exec(req2, adapt(), handler);
   db->async_exec(req2, adapt(), handler);
   db->async_exec(req1, adapt(), handler);
   db->async_exec(req2, adapt(), handler);
   db->async_exec(req1, adapt(), handler);
   db->async_exec(req2, adapt(), handler);
   db->async_exec(req2, adapt(), handler);
   db->async_exec(req1, adapt(), handler);
   db->async_exec(req2, adapt(), handler);
   db->async_exec(req3, adapt(), handler);

   endpoint ep{"127.0.0.1", "6379"};
   db->async_run(ep, [db](auto ec, auto...) {
      BOOST_CHECK_EQUAL(ec, net::error::misc_errors::eof);
      db->cancel(connection::operation::receive_push);
   });

   net::co_spawn(ioc.get_executor(), push_consumer3(db), net::detached);
   ioc.run();
}

#endif

BOOST_AUTO_TEST_CASE(test_push)
{
#ifdef BOOST_ASIO_HAS_CO_AWAIT
   test_push_is_received1(true);
   test_push_is_received2(true);
   test_push_many_subscribes(true);
#endif
   test_missing_push_reader1(true);
   test_missing_push_reader2(false);
   test_missing_push_reader3(true);

#ifdef BOOST_ASIO_HAS_CO_AWAIT
   test_push_is_received1(true);
   test_push_is_received2(false);
   test_push_many_subscribes(false);
#endif
   test_missing_push_reader1(true);
   test_missing_push_reader2(false);
   test_missing_push_reader3(false);
}

