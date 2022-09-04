/* Copyright (c) 2018-2022 Marcelo Zimbres Silva (mzimbres@gmail.com)
 *
 * Distributed under the Boost Software License, Version 1.0. (See
 * accompanying file LICENSE.txt)
 */

#ifndef AEDIS_NET_HPP
#define AEDIS_NET_HPP

#include <array>

#include <boost/system.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/connect.hpp>
#include <boost/assert.hpp>
#include <boost/asio/experimental/parallel_group.hpp>

namespace aedis::detail {

template <class Executor>
using conn_timer_t = boost::asio::basic_waitable_timer<std::chrono::steady_clock, boost::asio::wait_traits<std::chrono::steady_clock>, Executor>;

#include <boost/asio/yield.hpp>

template <
   class Stream,
   class EndpointSequence
   >
struct connect_op {
   Stream* socket;
   conn_timer_t<typename Stream::executor_type>* timer;
   EndpointSequence* endpoints;
   boost::asio::coroutine coro{};

   template <class Self>
   void operator()( Self& self
                  , std::array<std::size_t, 2> order = {}
                  , boost::system::error_code ec1 = {}
                  , typename Stream::protocol_type::endpoint const& ep = {}
                  , boost::system::error_code ec2 = {})
   {
      reenter (coro)
      {
         yield
         boost::asio::experimental::make_parallel_group(
            [this](auto token)
            {
               auto f = [](boost::system::error_code const&, auto const&) { return true; };
               return boost::asio::async_connect(*socket, *endpoints, f, token);
            },
            [this](auto token) { return timer->async_wait(token);}
         ).async_wait(
            boost::asio::experimental::wait_for_one(),
            std::move(self));

         switch (order[0]) {
            case 0: self.complete(ec1, ep); return;
            case 1:
            {
               BOOST_ASSERT_MSG(!ec2, "connect_op: Incompatible state.");
               self.complete(error::connect_timeout, ep);
               return;
            }

            default: BOOST_ASSERT(false);
         }
      }
   }
};

template <class Resolver, class Timer>
struct resolve_op {
   Resolver* resv;
   Timer* timer;
   boost::string_view host;
   boost::string_view port;
   boost::asio::coroutine coro{};

   template <class Self>
   void operator()( Self& self
                  , std::array<std::size_t, 2> order = {}
                  , boost::system::error_code ec1 = {}
                  , boost::asio::ip::tcp::resolver::results_type res = {}
                  , boost::system::error_code ec2 = {})
   {
      reenter (coro)
      {
         yield
         boost::asio::experimental::make_parallel_group(
            [this](auto token) { return resv->async_resolve(host.data(), port.data(), token);},
            [this](auto token) { return timer->async_wait(token);}
         ).async_wait(
            boost::asio::experimental::wait_for_one(),
            std::move(self));

         switch (order[0]) {
            case 0: self.complete(ec1, res); return;

            case 1:
            {
               BOOST_ASSERT_MSG(!ec2, "resolve_op: Incompatible state.");
               self.complete(error::resolve_timeout, {});
               return;
            }

            default: BOOST_ASSERT(false);
         }
      }
   }
};

template <class Channel>
struct send_receive_op {
   Channel* channel;
   boost::asio::coroutine coro{};

   template <class Self>
   void operator()( Self& self
                  , boost::system::error_code ec = {}
                  , std::size_t = 0)
   {
      reenter (coro)
      {
         yield
         channel->async_send(boost::system::error_code{}, 0, std::move(self));
         if (ec) {
            self.complete(ec, 0);
            return;
         }

         yield
         channel->async_receive(std::move(self));
         self.complete(ec, 0);
      }
   }
};

#include <boost/asio/unyield.hpp>

template <
   class Stream,
   class EndpointSequence,
   class CompletionToken
   >
auto async_connect(
      Stream& socket,
      conn_timer_t<typename Stream::executor_type>& timer,
      EndpointSequence ep,
      CompletionToken&& token)
{
   return boost::asio::async_compose
      < CompletionToken
      , void(boost::system::error_code, typename Stream::protocol_type::endpoint const&)
      >(connect_op<Stream, EndpointSequence>
            {&socket, &timer, &ep}, token, socket, timer);
}

template <
   class Resolver,
   class Timer,
   class CompletionToken =
      boost::asio::default_completion_token_t<typename Resolver::executor_type>
   >
auto async_resolve(
      Resolver& resv,
      Timer& timer,
      boost::string_view host,
      boost::string_view port,
      CompletionToken&& token = CompletionToken{})
{
   return boost::asio::async_compose
      < CompletionToken
      , void(boost::system::error_code, boost::asio::ip::tcp::resolver::results_type)
      >(resolve_op<Resolver, Timer>{&resv, &timer, host, port}, token, resv, timer);
}

template <
   class Channel,
   class CompletionToken =
      boost::asio::default_completion_token_t<typename Channel::executor_type>
   >
auto async_send_receive(Channel& channel, CompletionToken&& token = CompletionToken{})
{
   return boost::asio::async_compose
      < CompletionToken
      , void(boost::system::error_code, std::size_t)
      >(send_receive_op<Channel>{&channel}, token, channel);
}
} // aedis::detail

#endif // AEDIS_NET_HPP
