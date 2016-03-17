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
#include "Util.h"

#include <folly/Format.h>
#include <folly/Range.h>
#include <folly/String.h>

static vector<string> messageValues = {
    "mass-blaster",
    "horsey",
    "merble",
    "blamo mac-n-cheese",
    "wiz-kid thelma the cool"
};

int main(int argc, char** argv) {
    int port = 6000;
    milliseconds jitterMs(1500);
    if (argc == 3) {
        char* end = nullptr;
        port = std::strtol(argv[1], &end, 10);
        if (end == argv[1] || !port) {
            std::cout << "Port is bad.\n";
            return 1;
        }
        end = nullptr;
        milliseconds jitterMsArg(std::strtoll(argv[2], &end, 10));
        if (end != argv[2] && jitterMsArg.count()) {
            jitterMs = jitterMsArg;
        }
    }

    void* zctx = zmq_ctx_new();
    void* zsock = zmq_socket(zctx, ZMQ_PUSH);

    string addr{folly::sformat("tcp://127.0.0.1:{:d}", port)};
    std::cout << "Connecting to: " << addr << "\n";
    if (zmq_connect(zsock, addr.c_str()) == -1) {
        std::cout << "Unable to connect: " << zmq_strerror(zmq_errno()) << "\n";
        zmq_ctx_term(zctx);
        return 1;
    }

    while (true) {
        vector<string> msgContent = {
            std::to_string(nowMs().count()),
            messageValues[randomValue(0UL, messageValues.size()-1)],
            std::to_string(randomValue(1500, 12000))
        };
        string content = joinVector(msgContent, string(1, '\036'));
        ZmqMsg m(content);
        std::cout << "Sending: `" << folly::cEscape<string>(folly::StringPiece(content)) << "`\n";
        if (zmq_msg_send((zmq_msg_t*)m, zsock, 0) == -1) {
            std::cout << "Unable to send message: " << zmq_strerror(zmq_errno()) << "\n";
        }
        std::this_thread::sleep_for(milliseconds(randomValue(0L, jitterMs.count())));

    }
     
    return 0;
}
