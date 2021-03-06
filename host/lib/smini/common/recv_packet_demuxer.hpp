//
// Copyright 2011,2014 Ettus Research LLC
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//

#ifndef INCLUDED_LIBSHD_SMINI_COMMON_RECV_PACKET_DEMUXER_HPP
#define INCLUDED_LIBSHD_SMINI_COMMON_RECV_PACKET_DEMUXER_HPP

#include <shd/config.hpp>
#include <shd/transport/zero_copy.hpp>
#include <boost/shared_ptr.hpp>
#include <stdint.h>

namespace shd{ namespace smini{

    class recv_packet_demuxer{
    public:
        typedef boost::shared_ptr<recv_packet_demuxer> sptr;

        virtual ~recv_packet_demuxer(void) = 0;

        //! Make a new demuxer from a transport and parameters
        static sptr make(transport::zero_copy_if::sptr transport, const size_t size, const uint32_t sid_base);

        //! Get a buffer at the given index from the transport
        virtual transport::managed_recv_buffer::sptr get_recv_buff(const size_t index, const double timeout) = 0;
    };

}} //namespace shd::smini

#endif /* INCLUDED_LIBSHD_SMINI_COMMON_RECV_PACKET_DEMUXER_HPP */
