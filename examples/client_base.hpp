/* Copyright (c) 2019 - 2021 Marcelo Zimbres Silva (mzimbres at gmail dot com)
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#pragma once

#include <queue>

#include <aedis/aedis.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>

using namespace aedis::net::experimental::awaitable_operators;

namespace aedis {
namespace resp3 {

/*  Example: A general purpose redis client.
  
    This class is meant to be an example. Users are meant to derive
    from this class and override 
  
       1. on_event.
       2. on_push.
  
    The ReponseId type is required to provide the get_command() member
    function.
 */
template <class ResponseId>
class client_base
   : public std::enable_shared_from_this<client_base<ResponseId>> {
protected:
   // The response used for push types.
   std::vector<node> push_resp_;

   // Hello response.
   std::vector<node> hello_;

private:
   using tcp_socket = net::use_awaitable_t<>::as_default_on_t<net::ip::tcp::socket>;
   std::queue<serializer<ResponseId>> srs_;
   tcp_socket socket_;

   // Timer used to inform the write coroutine that it can write the
   // next message in the output queue.
   net::steady_timer timer_;

   // A coroutine that keeps reading the socket. When a message
   // arrives it calls on_event.
   net::awaitable<void> reader()
   {
      // Writes and reads continuosly from the socket.
      for (std::string buffer;;) {
         // Keeps reading while there is no messages queued waiting to be sent.
         do {
            // Loops to consume the response to all commands in the request.
            do {
               auto const t = co_await async_read_type(socket_, buffer);

               if (t == type::push) {
                  co_await async_read(socket_, buffer, adapt(push_resp_));
                  on_push();
               } else {
                  auto adapter = srs_.front().commands.front().adapter;
                  co_await async_read(socket_, buffer, adapter);
                  on_event(srs_.front().commands.front());
                  srs_.front().commands.pop();
               }

            } while (!std::empty(srs_) && !std::empty(srs_.front().commands));

            // We may exit the loop above either because we are done
            // with the response or because we received a server push
            // while the queue was empty.
            if (!std::empty(srs_))
               srs_.pop();

         } while (std::empty(srs_));

         // Writes the request in the socket.
         co_await async_write_some(socket_, srs_);
      }
   }

   // Write coroutine. It is kept suspended until there are messages
   // that can be sent.
   net::awaitable<void> writer()
   {
      while (socket_.is_open()) {
         boost::system::error_code ec;
         co_await timer_.async_wait(net::redirect_error(net::use_awaitable, ec));
         co_await async_write_some(socket_, srs_);
      }
   }

   net::awaitable<void> say_hello()
   {
      serializer<command> sr;
      sr.push(command::hello, 3);
      co_await async_write(socket_, net::buffer(sr.request()));

      std::string buffer;
      co_await async_read(socket_, buffer, adapt(hello_));
   }

   // The connection manager. It keeps trying the reconnect to the
   // server when the connection is lost.
   net::awaitable<void> connection_manager()
   {
      using tcp_resolver = aedis::net::use_awaitable_t<>::as_default_on_t<aedis::net::ip::tcp::resolver>;
      for (;;) {
         tcp_resolver resolver{socket_.get_executor()};
         auto const res = co_await resolver.async_resolve("127.0.0.1", "6379");
         co_await aedis::net::async_connect(socket_, res);

         co_await say_hello();

         timer_.expires_at(std::chrono::steady_clock::time_point::max());
         co_await (reader() && writer());

         socket_.close();
         timer_.cancel();

         timer_.expires_after(std::chrono::seconds{1});
         boost::system::error_code ec;
         co_await timer_.async_wait(net::redirect_error(net::use_awaitable, ec));
      }
   }

   /* Prepares the back of the queue to receive further commands. 
    *
    * If true is returned the request in the front of the queue can be
    * sent to the server. See async_write_some.
    */
   bool prepare_next(std::queue<serializer<ResponseId>>& reqs)
   {
      if (std::empty(reqs)) {
         reqs.push({});
         return true;
      }

      if (std::size(reqs) == 1) {
         reqs.push({});
         return false;
      }

      return false;
   }

public:
   // Constructor.
   client_base(net::any_io_executor ex)
   : socket_{ex}
   , timer_{ex}
   { }

   // Destructor.
   virtual ~client_base() { }

   /* Starts the client.
    *
    *  Stablishes a connection with the redis server and keeps waiting for messages to send.
    */
   void start()
   {
      net::co_spawn(socket_.get_executor(),
          [self = this->shared_from_this()]{ return self->connection_manager(); },
          net::detached);
   }

   /* \brief Adds commands the requests queue and sends if possible.
    *
    *  The filler callable get a request by reference, for example
    *
    *  @code
    *  void f(serializer& req)
    *  {
    *     req.push(command::ping);
    *     ...
    *  }
    *  @endcode
    *
    *  It will be called with the request that is at the back of the queue of
    *  outgoing requests.
    */
   template <class Filler>
   void send(Filler filler)
   {
      // Prepares the back of the queue for a new command.
      auto const can_write = prepare_next(srs_);

      filler(srs_.back());

      if (can_write)
         timer_.cancel_one();
   }

   /* \brief Called when the response to a specific command is received.
    *
    *  Override this function to receive events in your derived class.
    */
   virtual void on_event(ResponseId) {};

   /* \brief Called when server push is received.
    *
    *  Override this function to receive push events in the derived class.
    */
   virtual void on_push() {};
};

} // resp3
} // aedis
