#include <cstring>
#include <string>
#include <vector>

#include "test.hpp"
#include "chatterbox.hpp"

using namespace std;
using namespace elga;

// ZMQMessage must stay valid when moved. ZeroMQ stores small messages (up to
// ~33 bytes) inside the zmq_msg_t itself, so a moved message must not keep a
// data pointer into the object it was moved from.

static string as_string(const ZMQMessage& m) { return string(m.data(), m.size()); }

TEST(zmq_message_moves) {
    void* ctx = zmq_ctx_new();
    void* tx = zmq_socket(ctx, ZMQ_PAIR);
    void* rx = zmq_socket(ctx, ZMQ_PAIR);
    EQ(zmq_bind(rx, "inproc://test_zmq_message_moves"), 0);
    EQ(zmq_connect(tx, "inproc://test_zmq_message_moves"), 0);

    const string small1 = "abc";
    const string large(1000, 'x');
    const string small2 = "defghij";
    for (const string* s : {&small1, &large, &small2})
        EQ(zmq_send(tx, s->data(), s->size(), 0), (int)s->size());

    {
        // push_back without reserve: messages are moved again on each regrowth
        vector<ZMQMessage> msgs;
        for (int k = 0; k < 3; ++k) msgs.push_back(ZMQMessage(rx));
        EQ(as_string(msgs[0]), small1);
        EQ(as_string(msgs[1]), large);
        EQ(as_string(msgs[2]), small2);

        // Move construction and move assignment (which must release the old message)
        ZMQMessage m = move(msgs[0]);
        EQ(as_string(m), small1);
        m = move(msgs[2]);
        EQ(as_string(m), small2);
        m = move(msgs[1]);
        EQ(as_string(m), large);
        EQ(msgs[1].size(), 0);

        // An empty, non-blocking read gives a closed message, which can be moved too
        ZMQMessage none(rx, false);
        ZMQMessage none2 = move(none);
        EQ(none2.size(), 0);
    }

    zmq_close(tx);
    zmq_close(rx);
    zmq_ctx_term(ctx);

    TEST_PASS
}

// A received message can be forwarded on another socket without copying,
// and is empty afterwards
TEST(zmq_message_forward) {
    void* ctx = zmq_ctx_new();
    void* a = zmq_socket(ctx, ZMQ_PAIR);
    void* b = zmq_socket(ctx, ZMQ_PAIR);
    void* c = zmq_socket(ctx, ZMQ_PAIR);
    void* d = zmq_socket(ctx, ZMQ_PAIR);
    EQ(zmq_bind(b, "inproc://test_zmq_message_forward_1"), 0);
    EQ(zmq_connect(a, "inproc://test_zmq_message_forward_1"), 0);
    EQ(zmq_bind(d, "inproc://test_zmq_message_forward_2"), 0);
    EQ(zmq_connect(c, "inproc://test_zmq_message_forward_2"), 0);

    for (const string& s : {string("tiny"), string(5000, 'y')}) {
        EQ(zmq_send(a, s.data(), s.size(), 0), (int)s.size());
        {
            ZMQMessage m(b);          // received on b ...
            EQ(as_string(m), s);
            m.send(c);                // ... forwarded on c
            EQ(m.size(), 0);
        }
        ZMQMessage out(d);
        EQ(as_string(out), s);
    }

    for (void* s : {a, b, c, d}) zmq_close(s);
    zmq_ctx_term(ctx);

    TEST_PASS
}

TESTS_BEGIN
    RUN_TEST(zmq_message_moves)
    RUN_TEST(zmq_message_forward)
TESTS_END
