/*
 * Copyright (c) 2016 Brian Smith <brian@linuxfood.net>
 *
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */
#pragma once

#include <algorithm>
#include <string>
#include <vector>

#include <zmq.h>

class ZmqMsg
{
public:
    class Builder
    {
    public:
        Builder( const std::string& s ) : _bytes{} { append( s ); }

        Builder() = default;
        ~Builder() = default;

        Builder( const Builder& ) = default;
        /* Builder( Builder&& ) = default; */

        Builder& operator=( const Builder& ) = default;
        /* Builder& operator=( Builder&& ) = default; */

        void append( const std::string& s ) { std::copy(s.cbegin(), s.cend(), std::back_inserter( _bytes )); }

        // TODO: Figure out how to do this without copying.
        ZmqMsg get() const { return { _bytes }; }
    private:
        std::vector<uint8_t> _bytes;
    };

    ZmqMsg()
    {
        zmq_msg_init( &_msg );
    }
    ~ZmqMsg()
    {
        zmq_msg_close( &_msg );
    }

    ZmqMsg( const ZmqMsg& msg )
    {
        zmq_msg_init( &_msg );
        zmq_msg_copy( &_msg, &const_cast<ZmqMsg&>(msg)._msg );
    }

    ZmqMsg& operator= ( const ZmqMsg& msg )
    {
        zmq_msg_copy( &_msg, &const_cast<ZmqMsg&>(msg)._msg );
        return *this;
    }

    ZmqMsg( ZmqMsg&& msg )
    {
        zmq_msg_init( &_msg );
        zmq_msg_move( &_msg, &msg._msg );
    }

    ZmqMsg& operator= ( ZmqMsg&& msg )
    {
        zmq_msg_move( &_msg, &msg._msg );
        return *this;
    }

    /*
    Construct a msg from a copy of any container that meets: http://en.cppreference.com/w/cpp/concept/Container
    */
    template<class Container>
    ZmqMsg(const Container& c)
    {
        zmq_msg_init_size( &_msg, c.size() );
        std::copy( c.cbegin(), c.cend(), static_cast<char*>(zmq_msg_data( &_msg )) );
    }

    ZmqMsg( const char* m ) : ZmqMsg( std::string( m ) ) {}
    ZmqMsg( const char* m, size_t len )
    {
        zmq_msg_init_size( &_msg, len );
        std::copy( m, m + len, static_cast<char*>( zmq_msg_data( &_msg ) ) );
    }

#if (ZMQ_VERSION >= ZMQ_MAKE_VERSION(4,2,0))
    ZmqMsg& set_client_id( uint32_t id ) { zmq_msg_set_routing_id( &_msg, id ); return *this; }
    uint32_t get_client_id() const { return zmq_msg_routing_id( const_cast<zmq_msg_t*>( &_msg ) ); }
#endif

    size_t size() const { return zmq_msg_size( const_cast<zmq_msg_t*>( &_msg ) ); }

    operator zmq_msg_t*( ) { return &_msg; }

    void* data() const { return zmq_msg_data( const_cast<zmq_msg_t*>( &_msg ) ); }

    operator std::string() const
    {
        return std::string{ static_cast<const char*>( zmq_msg_data( const_cast<zmq_msg_t*>(&_msg) ) ), size() };
    }
        
private:
    zmq_msg_t _msg;
};
