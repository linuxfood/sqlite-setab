# Copyright (c) 2016 Brian Smith <brian@linuxfood.net>
# 
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to
# deal in the Software without restriction, including without limitation the
# rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
# sell copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
# 
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
# 
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
# IN THE SOFTWARE.

cxx_library(
    name = 'sqlite',
    srcs = [ 'sqlite3.c' ],
    headers = [ 'sqlite3.h', 'sqlite3ext.h' ],
)

cxx_library(
    name = 'util',
    headers = [
        'ZmqMsg.h',
        'Util.h',
    ]
)

cxx_binary(
    name = 'setab',
    srcs = [
        'setab.cpp',
    ],
    deps = [
        ':sqlite',
        ':util',
        '//externals/zeromq:zeromq',
    ],
)

cxx_binary(
    name = 'stream_maker',
    srcs = [
        'stream_maker.cpp'
    ],
    deps = [
        ':sqlite',
        ':util',
        '//externals/zeromq:zeromq',
    ]
)
