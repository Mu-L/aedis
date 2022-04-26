/* Copyright (c) 2018-2022 Marcelo Zimbres Silva (mzimbres@gmail.com)
 *
 * Distributed under the Boost Software License, Version 1.0. (See
 * accompanying file LICENSE.txt)
 */

#ifndef AEDIS_RESP3_READ_HPP
#define AEDIS_RESP3_READ_HPP

#include <aedis/resp3/type.hpp>
#include <aedis/resp3/detail/parser.hpp>
#include <aedis/resp3/detail/read_ops.hpp>

#include <boost/asio/read.hpp>
#include <boost/asio/compose.hpp>
#include <boost/asio/async_result.hpp>
#include <boost/asio/yield.hpp>

namespace aedis {
namespace resp3 {

/** \brief Reads a complete response to a command sychronously.
 *  \ingroup any
 *
 *  This function reads a complete response to a command or a
 *  server push synchronously. For example
 *
 *  @code
 *  std::string buffer, resp;
 *  resp3::read(socket, dynamic_buffer(buffer), adapt(resp));
 *  @endcode
 *
 *  For a complete example see low_level/sync_intro.cpp. This function
 *  is implemented in terms of one or more calls to @c
 *  asio::read_until and @c asio::read functions, and is known as a @a
 *  composed @a operation. Furthermore (Quoted from Beast docs)
 *
 *  > The implementation may read additional bytes from the stream that
 *  > lie past the end of the message being read. These additional
 *  > bytes are stored in the dynamic buffer, which must be preserved
 *  > for subsequent reads.
 *
 *  \param stream The stream from which to read e.g. a tcp socket.
 *  \param buf Dynamic buffer (version 2).
 *  \param adapter The response adapter, see more on \ref low-level-responses.
 *  \param ec If an error occurs, it will be assigned to this paramter.
 *  \returns The number of bytes that have been consumed from the dynamic buffer.
 *
 *  \remark This function calls buf.consume() in each chunk of data
 *  after it has been passed to the adapter.
 *
 *  TODO: Describe how buffers are consumed.
 */
template <
  class SyncReadStream,
  class DynamicBuffer,
  class ResponseAdapter
  >
std::size_t
read(
   SyncReadStream& stream,
   DynamicBuffer buf,
   ResponseAdapter adapter,
   boost::system::error_code& ec)
{
   detail::parser<ResponseAdapter> p {adapter};
   std::size_t n = 0;
   std::size_t consumed = 0;
   do {
      if (p.bulk() == type::invalid) {
	 n = boost::asio::read_until(stream, buf, "\r\n", ec);
	 if (ec)
	    return 0;

	 if (n < 3) {
            ec = error::unexpected_read_size;
            return 0;
         }
      } else {
	 auto const s = buf.size();
	 auto const l = p.bulk_length();
	 if (s < (l + 2)) {
	    auto const to_read = l + 2 - s;
	    buf.grow(to_read);
	    n = boost::asio::read(stream, buf.data(s, to_read), ec);
	    if (ec)
	       return 0;

            if (n < to_read) {
               ec = error::unexpected_read_size;
               return 0;
            }
	 }
      }

      auto const* data = (char const*) buf.data(0, n).data();
      n = p.consume(data, n, ec);
      if (ec)
         return 0;

      buf.consume(n);
      consumed += n;
   } while (!p.done());

   return consumed;
}

/** \brief Reads a complete response to a command sychronously.
 *  \ingroup any
 *  
 *  Same as the other error-code overload but throws on error.
 */
template<
   class SyncReadStream,
   class DynamicBuffer,
   class ResponseAdapter = detail::ignore_response>
std::size_t
read(
   SyncReadStream& stream,
   DynamicBuffer buf,
   ResponseAdapter adapter = ResponseAdapter{})
{
   boost::system::error_code ec;
   auto const n = resp3::read(stream, buf, adapter, ec);

   if (ec)
       BOOST_THROW_EXCEPTION(boost::system::system_error{ec});

   return n;
}

/** @brief Reads a complete response to a Redis command asynchronously.
 *  \ingroup any
 *
 *  This function reads a complete response to a command or a
 *  server push asynchronously. For example
 *
 *  @code
 *  std::string buffer;
 *  std::vector<std::string> response;
 *  co_await resp3::async_read(socket, dynamic_buffer(buffer), adapt(response));
 *  @endcode
 *
 *  For a complete example see low_level/async_intro.cpp. This
 *  function is implemented in terms of one or more calls to @c
 *  asio::async_read_until and @c asio::async_read functions, and is
 *  known as a @a composed @a operation. Furthermore (Quoted from
 *  Beast docs)
 *
 *  > The implementation may read additional bytes from the stream that
 *  > lie past the end of the message being read. These additional
 *  > bytes are stored in the dynamic buffer, which must be preserved
 *  > for subsequent reads.
 *
 *  \param stream The stream from which to read e.g. a tcp socket.
 *  \param buffer Dynamic buffer (version 2).
 *  \param adapter The response adapter, see more on \ref low-level-responses.
 *  \param token The completion token.
 *
 *  The completion handler will receive as a parameter the total
 *  number of bytes transferred from the stream and must have the
 *  following signature
 *
 *  @code
 *  void(boost::system::error_code, std::size_t);
 *  @endcode
 *
 *  \remark This function calls buf.consume() in each chunk of data
 *  after it has been passed to the adapter.
 *  TODO: Describe how buffers are consumed.
 */
template <
   class AsyncReadStream,
   class DynamicBuffer,
   class ResponseAdapter = detail::ignore_response,
   class CompletionToken = boost::asio::default_completion_token_t<typename AsyncReadStream::executor_type>
   >
auto async_read(
   AsyncReadStream& stream,
   DynamicBuffer buffer,
   ResponseAdapter adapter = ResponseAdapter{},
   CompletionToken&& token =
      boost::asio::default_completion_token_t<typename AsyncReadStream::executor_type>{})
{
   return boost::asio::async_compose
      < CompletionToken
      , void(boost::system::error_code, std::size_t)
      >(detail::parse_op<AsyncReadStream, DynamicBuffer, ResponseAdapter> {stream, buffer, adapter},
        token,
        stream);
}

} // resp3
} // aedis

#include <boost/asio/unyield.hpp>

#endif // AEDIS_RESP3_READ_HPP
