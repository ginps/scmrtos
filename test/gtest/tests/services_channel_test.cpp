//******************************************************************************
//*
//*     scmRTOS kernel tests: channel<T, Size, S>
//*
//*     The data path is a typed test: the same scenarios for int, uint8_t,
//*     a structure, and a 16-bit index type.
//*
//******************************************************************************
#include "rtos_test.h"

using namespace OS;
using namespace scmtest;
using ::testing::InSequence;

namespace
{
    std::string at(const std::string & what) { return what + "@" + std::to_string(RtosTest::Ticks()); }

    struct TRecord
    {
        uint32_t id;
        char     tag[6];
        bool operator==(const TRecord & o) const { return id == o.id && memcmp(tag, o.tag, sizeof(tag)) == 0; }
    };
    void PrintTo(const TRecord & r, std::ostream * os) { *os << "TRecord{" << r.id << "}"; }

    template<typename T> T make(int i);
    template<> int     make<int>(int i)     { return i * 1000 + 7; }
    template<> uint8_t make<uint8_t>(int i) { return static_cast<uint8_t>(i * 3 + 1); }
    template<> TRecord make<TRecord>(int i) { TRecord r = { static_cast<uint32_t>(i) * 0x01010101u, "rec" }; r.tag[4] = static_cast<char>('a' + i % 26); return r; }

    // T: item type; S: index type of the channel
    template<typename T_, typename S_> struct TCfg { typedef T_ T; typedef S_ S; };
}

//==============================================================================
//
//    Typed data path
//
template<typename Cfg>
class ChannelData : public RtosTest
{
protected:
    typedef typename Cfg::T T;
    typedef typename Cfg::S S;
    typedef channel<T, 4, S> TChannel;
    static TChannel & ch() { static TChannel c; return c; }
};

typedef ::testing::Types<TCfg<int, uint8_t>, TCfg<uint8_t, uint8_t>, TCfg<TRecord, uint8_t>, TCfg<int, uint16_t>> ChannelConfigs;
TYPED_TEST_SUITE(ChannelData, ChannelConfigs);

TYPED_TEST(ChannelData, FifoOrder)
{
    typedef typename TestFixture::T T;
    this->Process(0, [] {
        auto & c = TestFixture::ch();
        for(int i = 0; i < 4; ++i)
            c.push(make<T>(i));
        EXPECT_EQ(c.get_count(), 4);
        EXPECT_EQ(c.get_free_size(), 0);
        for(int i = 0; i < 4; ++i)
        {
            T x;
            ASSERT_TRUE(c.pop(x));
            EXPECT_EQ(x, make<T>(i));
        }
        EXPECT_EQ(c.get_count(), 0);
    });
    this->RunOS();
}

TYPED_TEST(ChannelData, PushFrontAndPopBack)
{
    typedef typename TestFixture::T T;
    this->Process(0, [] {
        auto & c = TestFixture::ch();
        c.push(make<T>(1));
        c.push(make<T>(2));
        c.push_front(make<T>(0));                   // 0 1 2
        T x;
        ASSERT_TRUE(c.pop_back(x)); EXPECT_EQ(x, make<T>(2));
        ASSERT_TRUE(c.pop(x));      EXPECT_EQ(x, make<T>(0));
        ASSERT_TRUE(c.pop(x));      EXPECT_EQ(x, make<T>(1));
    });
    this->RunOS();
}

TYPED_TEST(ChannelData, WrapAroundKeepsTheOrder)
{
    typedef typename TestFixture::T T;
    this->Process(0, [] {
        auto & c = TestFixture::ch();
        int next_in = 0, next_out = 0;
        for(int round = 0; round < 10; ++round)     // the indices go round the buffer several times
        {
            for(int i = 0; i < 3; ++i) c.push(make<T>(next_in++));
            for(int i = 0; i < 3; ++i) { T x; ASSERT_TRUE(c.pop(x)); EXPECT_EQ(x, make<T>(next_out++)); }
        }
    });
    this->RunOS();
}

TYPED_TEST(ChannelData, WriteAndReadBlocks)
{
    typedef typename TestFixture::T T;
    typedef typename TestFixture::S S;
    this->Process(0, [] {
        auto & c = TestFixture::ch();
        T in[3] = { make<T>(5), make<T>(6), make<T>(7) };
        c.write(in, S(3));
        T out[3];
        ASSERT_TRUE(c.read(out, S(3)));
        for(int i = 0; i < 3; ++i)
            EXPECT_EQ(out[i], in[i]);
    });
    this->RunOS();
}

TYPED_TEST(ChannelData, ProducerBlocksOnAFullChannel)
{
    typedef typename TestFixture::T T;
    this->Process(1, [] {
        auto & c = TestFixture::ch();
        for(int i = 0; i < 6; ++i)
            c.push(make<T>(i));                     // the 5th push blocks until P2 pops
        EXPECT_EQ(RtosTest::Ticks(), 3u);
    });
    this->Process(2, [] {
        auto & c = TestFixture::ch();
        OS::sleep(3);
        for(int i = 0; i < 6; ++i)
        {
            T x;
            ASSERT_TRUE(c.pop(x));
            EXPECT_EQ(x, make<T>(i));
        }
    });
    this->RunOS();
}

TYPED_TEST(ChannelData, ConsumerBlocksOnAnEmptyChannel)
{
    typedef typename TestFixture::T T;
    this->Process(0, [] {
        T x;
        ASSERT_TRUE(TestFixture::ch().pop(x));
        EXPECT_EQ(x, make<T>(42));
        EXPECT_EQ(RtosTest::Ticks(), 2u);
    });
    this->Process(3, [] { OS::sleep(2); TestFixture::ch().push(make<T>(42)); });
    this->RunOS();
}

//==============================================================================
//
//    Blocking, timeouts, interrupts
//
namespace
{
    class Channel : public RtosTest { };
}

TEST_F(Channel, PopTimesOut)
{
    static channel<int, 4> c;
    Process(0, [] { int x = -1; EXPECT_FALSE(c.pop(x, 5)); EXPECT_EQ(Ticks(), 5u); EXPECT_EQ(x, -1); });
    RunOS();
}

TEST_F(Channel, PopBackTimesOut)
{
    static channel<int, 4> c;
    Process(0, [] { int x = -1; EXPECT_FALSE(c.pop_back(x, 3)); EXPECT_EQ(Ticks(), 3u); });
    RunOS();
}

TEST_F(Channel, PopBeforeTheDeadlineLeavesNoTimeout)
{
    static channel<int, 4> c;
    Process(0, [] {
        int x;
        EXPECT_TRUE(c.pop(x, 10));
        EXPECT_EQ(x, 3);
        EXPECT_EQ(Ticks(), 2u);
        EXPECT_FALSE(P(0).is_sleeping());
        OS::sleep(15);
        EXPECT_EQ(Ticks(), 17u);
    });
    Process(1, [] { OS::sleep(2); c.push(3); });
    RunOS();
}

TEST_F(Channel, ReadWaitsForEnoughItems)
{
    static channel<int, 8> c;
    {
        InSequence seq;
        EXPECT_CALL(probe, Mark("push1"));
        EXPECT_CALL(probe, Mark("push2"));
        EXPECT_CALL(probe, Mark("read 1,2,3@6"));
        EXPECT_CALL(probe, Mark("push3"));
    }
    Process(0, [] {
        int buf[3] = {};
        ASSERT_TRUE(c.read(buf, 3));
        Mark(at("read " + std::to_string(buf[0]) + "," + std::to_string(buf[1]) + "," + std::to_string(buf[2])));
    });
    Process(1, [] {
        for(int i = 1; i <= 3; ++i)
        {
            OS::sleep(2);
            c.push(i);
            Mark("push" + std::to_string(i));
        }
    });
    RunOS();
}

TEST_F(Channel, ReadTimeoutLeavesTheData)
{
    static channel<int, 8> c;
    Process(0, [] {
        int buf[3];
        EXPECT_FALSE(c.read(buf, 3, 4));
        EXPECT_EQ(Ticks(), 4u);
        EXPECT_EQ(c.get_count(), 2);
    });
    Process(1, [] { c.push(1); c.push(2); });
    RunOS();
}

TEST_F(Channel, WriteWaitsForEnoughRoom)
{
    static channel<int, 4> c;
    Process(1, [] {
        c.push(0); c.push(0); c.push(0);
        const int data[3] = { 1, 2, 3 };
        c.write(data, 3);                           // needs 3 free places
        EXPECT_EQ(Ticks(), 4u);
    });
    Process(2, [] {
        int x;
        OS::sleep(2); c.pop(x);                     // 1 free: not enough
        EXPECT_TRUE(P(1).is_suspended());
        OS::sleep(2); c.pop(x); c.pop(x);           // 3 free
    });
    RunOS();
}

TEST_F(Channel, TryOperationsNeverBlock)
{
    static channel<int, 2> c;
    Process(0, [] {
        int x = 0;
        EXPECT_FALSE(c.try_pop(x));
        EXPECT_TRUE(c.try_push(1));
        EXPECT_TRUE(c.try_push_front(0));
        EXPECT_FALSE(c.try_push(2));
        EXPECT_FALSE(c.try_push_front(2));
        EXPECT_TRUE(c.try_pop(x));
        EXPECT_EQ(x, 0);
        EXPECT_EQ(c.get_count(), 1);
        EXPECT_EQ(Ticks(), 0u);
    });
    RunOS();
}

TEST_F(Channel, TryPushWakesAConsumer)
{
    static channel<int, 2> c;
    {
        InSequence seq;
        EXPECT_CALL(probe, Mark("P0 got 5"));
        EXPECT_CALL(probe, Mark("P1"));
    }
    Process(0, [] { int x; c.pop(x); Mark("P0 got " + std::to_string(x)); });
    Process(1, [] { EXPECT_TRUE(c.try_push(5)); Mark("P1"); });
    RunOS();
}

TEST_F(Channel, TryPopWakesAProducer)
{
    static channel<int, 1> c;
    Process(0, [] { c.push(1); c.push(2); EXPECT_EQ(Ticks(), 1u); });
    Process(1, [] { OS::sleep(1); int x; EXPECT_TRUE(c.try_pop(x)); EXPECT_EQ(x, 1); });
    RunOS();
}

TEST_F(Channel, FlushEmptiesAndWakesProducers)
{
    static channel<int, 2> c;
    Process(1, [] { c.push(1); c.push(2); c.push(3); EXPECT_EQ(Ticks(), 2u); });
    Process(2, [] {
        OS::sleep(2);
        c.flush();                                  // P1's third push goes in
        EXPECT_EQ(c.get_count(), 1);
        int x; c.pop(x); EXPECT_EQ(x, 3);
    });
    RunOS();
}

TEST_F(Channel, CompetingConsumersEachGetOneItem)
{
    static channel<int, 4> c;
    {
        InSequence seq;
        EXPECT_CALL(probe, Mark("P1 got 10"));
        EXPECT_CALL(probe, Mark("P2 got 20"));
    }
    Process(1, [] { int x; c.pop(x); Mark("P1 got " + std::to_string(x)); });
    Process(2, [] { int x; c.pop(x); Mark("P2 got " + std::to_string(x)); });
    Process(3, [] { c.push(10); c.push(20); });
    RunOS();
}

TEST_F(Channel, ConsumerThatLosesTheItemWaitsAgain)
{
    // P0 takes the item that woke P2 before P2 runs: P2 goes back to waiting
    static channel<int, 4> c;
    Process(2, [] {
        int x;
        EXPECT_TRUE(c.pop(x));
        EXPECT_EQ(x, 2);
        EXPECT_EQ(Ticks(), 4u);
    });
    Process(0, [] {
        OS::sleep(1);
        c.push(1);                                  // P2 becomes ready, P0 keeps the CPU
        int x;
        EXPECT_TRUE(c.try_pop(x));
        EXPECT_EQ(x, 1);
        OS::sleep(3);
        c.push(2);
    });
    RunOS();
}

TEST_F(Channel, TimeoutKeptWhenTheItemIsStolen)
{
    // Regression for defect 2: P1 waits with pop(x, 5). At tick 4 a push wakes
    // it, but P0 (higher) takes the item first, and P1's timeout would run out
    // before it gets the CPU. The wait must still end with false at the
    // deadline (tick 5, or tick 6 when the timer skips ready processes),
    // never turn into an endless wait.
    static channel<int, 4> c;
    static int      result = -1;
    static unsigned tick   = 0;
    Process(1, [] { int x; result = c.pop(x, 5); tick = Ticks(); });
    Process(0, [] {
        OS::sleep(4);
        c.push(1);                                  // P1 becomes ready, P0 keeps the CPU
        Tick();                                     // tick 5
        int x;
        c.pop(x);
        OS::sleep(20);
    });
    AtEnd([] {
        EXPECT_EQ(result, 0);
        EXPECT_TRUE(tick == 5 || tick == 6) << "tick " << tick;
    });
    RunOS();
}

TEST_F(Channel, ForceWakeUpEndsAPopWithFalse)
{
    static channel<int, 4> c;
    Process(0, [] { int x; EXPECT_FALSE(c.pop(x)); EXPECT_EQ(Ticks(), 2u); });
    Process(1, [] { OS::sleep(2); P(0).force_wake_up(); });
    RunOS();
}

TEST_F(Channel, IsrWriteAndRead)
{
    static channel<int, 4> c;
    static uint8_t written = 0, got = 0;
    static int     buf[4] = {};
    {
        InSequence seq;
        EXPECT_CALL(irq, Handler(0));
        EXPECT_CALL(probe, Mark("P0 got 1"));
        EXPECT_CALL(irq, Handler(1));
    }
    Irq(0, [] { const int d[6] = { 1, 2, 3, 4, 5, 6 }; written = c.write_isr(d, 6); });
    Irq(1, [] { got = c.read_isr(buf, 4); });
    Process(0, [] { int x; c.pop(x); Mark("P0 got " + std::to_string(x)); });
    Process(1, [] {
        Raise(0);                                   // 4 of 6 fit; P0 takes one at once
        EXPECT_EQ(written, 4);
        Raise(1);
        EXPECT_EQ(got, 3);
        EXPECT_EQ(buf[0], 2);
        EXPECT_EQ(buf[2], 4);
        EXPECT_EQ(c.get_count(), 0);
    });
    RunOS();
}

TEST_F(Channel, StressProducersAndConsumers)
{
    // two producers, two consumers, a small channel and ticks everywhere:
    // every item arrives once; each consumer sees the items of one producer
    // in the order they were pushed
    static channel<int, 3> c;
    static int seen[2][100];
    static int last[2][2] = { { -1, -1 }, { -1, -1 } };     // [consumer][producer]
    auto producer = [](int id) {
        for(int i = 0; i < 100; ++i)
        {
            c.push(id * 1000 + i);
            if(i % 7 == 0) OS::sleep(1);
        }
    };
    auto consumer = [](int me) {
        for(int i = 0; i < 100; ++i)
        {
            int v;
            ASSERT_TRUE(c.pop(v));
            int id = v / 1000, n = v % 1000;
            ASSERT_TRUE(id == 0 || id == 1);
            EXPECT_GT(n, last[me][id]) << "order of producer " << id;
            last[me][id] = n;
            ++seen[id][n];
            if(i % 5 == 0) OS::sleep(1);
        }
    };
    Process(0, [=] { consumer(0); });
    Process(1, [=] { producer(0); });
    Process(2, [=] { consumer(1); });
    Process(3, [=] { producer(1); });
    AtEnd([] {
        for(int id = 0; id < 2; ++id)
            for(int n = 0; n < 100; ++n)
                EXPECT_EQ(seen[id][n], 1) << id << ":" << n;
    });
    RunOS();
}
