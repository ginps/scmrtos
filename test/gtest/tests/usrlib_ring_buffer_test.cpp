//******************************************************************************
//*
//*     scmRTOS kernel tests: usr::ring_buffer (core/usrlib.h), the storage of
//*     channel<>. Plain unit tests: the kernel is not started.
//*
//******************************************************************************
#include "rtos_test.h"

#include <deque>
#include <random>

using namespace usr;

//==============================================================================
//
//    Typed tests: the same behaviour for several sizes and index types
//
template<typename B> class RingBuffer : public ::testing::Test
{
protected:
    B b;
};

template<uint16_t N, typename S> struct TRb : ring_buffer<int, N, S> { static constexpr int Size = N; };

typedef ::testing::Types<TRb<1, uint8_t>, TRb<2, uint8_t>, TRb<7, uint8_t>, TRb<16, uint8_t>,
                         TRb<100, uint8_t>, TRb<127, uint8_t>, TRb<300, uint16_t>> RingBufferTypes;
TYPED_TEST_SUITE(RingBuffer, RingBufferTypes);

TYPED_TEST(RingBuffer, StartsEmpty)
{
    EXPECT_EQ(this->b.get_count(), 0);
    EXPECT_EQ(this->b.get_free_size(), TypeParam::Size);
}

TYPED_TEST(RingBuffer, FillsUpToSizeAndRejectsMore)
{
    for(int i = 0; i < TypeParam::Size; ++i)
        ASSERT_TRUE(this->b.push_back(i)) << i;
    EXPECT_FALSE(this->b.push_back(-1));
    EXPECT_FALSE(this->b.push_front(-1));
    EXPECT_EQ(this->b.get_count(), TypeParam::Size);
    EXPECT_EQ(this->b.get_free_size(), 0);
    for(int i = 0; i < TypeParam::Size; ++i)
        EXPECT_EQ(this->b.pop_front(), i);
    EXPECT_EQ(this->b.get_count(), 0);
}

TYPED_TEST(RingBuffer, BothEndsAndWrapAround)
{
    // go round the buffer three times using every combination of ends
    std::deque<int> model;
    int v = 0;
    for(int round = 0; round < 3 * TypeParam::Size + 5; ++round)
    {
        if(this->b.get_free_size() == 0)
        {
            EXPECT_EQ(this->b.pop_front(), model.front()); model.pop_front();
        }
        if(round % 2) { ASSERT_TRUE(this->b.push_back(v));  model.push_back(v); }
        else          { ASSERT_TRUE(this->b.push_front(v)); model.push_front(v); }
        ++v;
        if(round % 3 == 0 && !model.empty())
        {
            EXPECT_EQ(this->b.pop_back(), model.back()); model.pop_back();
        }
        ASSERT_EQ(this->b.get_count(), model.size());
    }
    while(!model.empty())
    {
        EXPECT_EQ(this->b.pop_front(), model.front());
        model.pop_front();
    }
}

TYPED_TEST(RingBuffer, IndexOperatorCountsFromTheOldest)
{
    // move First away from 0, then index across the end of the array
    const int n = TypeParam::Size;
    for(int i = 0; i < n / 2 + 1; ++i) this->b.push_back(-1);
    for(int i = 0; i < n / 2 + 1; ++i) this->b.pop_front();
    for(int i = 0; i < n; ++i)         this->b.push_back(i * 10);
    for(int i = 0; i < n; ++i)
        EXPECT_EQ(this->b[i], i * 10) << "index " << i;
    this->b[0] = 777;
    EXPECT_EQ(this->b.pop_front(), 777);
}

TYPED_TEST(RingBuffer, WriteIsAllOrNothing)
{
    const int n = TypeParam::Size;
    std::vector<int> data(n + 1);
    for(int i = 0; i <= n; ++i) data[i] = i;
    EXPECT_FALSE(this->b.write(data.data(), n + 1)) << "more than the size";
    EXPECT_EQ(this->b.get_count(), 0);
    EXPECT_TRUE(this->b.write(data.data(), n));
    EXPECT_FALSE(this->b.write(data.data(), 1));
    std::vector<int> out(n);
    this->b.read(out.data(), n);
    for(int i = 0; i < n; ++i)
        EXPECT_EQ(out[i], i);
}

TYPED_TEST(RingBuffer, ReadTakesAtMostTheCount)
{
    this->b.push_back(5);
    int out[3] = { -1, -1, -1 };
    this->b.read(out, 3);
    EXPECT_EQ(out[0], 5);
    EXPECT_EQ(out[1], -1) << "read() must not invent items";
    EXPECT_EQ(this->b.get_count(), 0);
}

TYPED_TEST(RingBuffer, FlushEmpties)
{
    this->b.push_back(1);
    this->b.flush();
    EXPECT_EQ(this->b.get_count(), 0);
    EXPECT_EQ(this->b.get_free_size(), TypeParam::Size);
    ASSERT_TRUE(this->b.push_back(2));
    EXPECT_EQ(this->b.pop_front(), 2);
}

TYPED_TEST(RingBuffer, PopFromEmptyChangesNothing)
{
    (void)this->b.pop_front();
    (void)this->b.pop_back();
    EXPECT_EQ(this->b.get_count(), 0);
    EXPECT_EQ(this->b.get_free_size(), TypeParam::Size);
}

//==============================================================================
//
//    Model-based test against std::deque: random operation sequences
//
template<typename B, int N>
static void model_check(unsigned seed, int steps)
{
    B b;
    std::deque<int> m;
    std::mt19937 rng(seed);
    for(int step = 0; step < steps; ++step)
    {
        const int op = static_cast<int>(rng() % 8);
        const int v  = static_cast<int>(rng() % 100000);
        switch(op)
        {
            case 0: case 1:
                ASSERT_EQ(b.push_back(v), m.size() < N) << "step " << step;
                if(m.size() < N) m.push_back(v);
                break;
            case 2:
                ASSERT_EQ(b.push_front(v), m.size() < N) << "step " << step;
                if(m.size() < N) m.push_front(v);
                break;
            case 3: case 4:
                if(!m.empty()) { ASSERT_EQ(b.pop_front(), m.front()) << "step " << step; m.pop_front(); }
                break;
            case 5:
                if(!m.empty()) { ASSERT_EQ(b.pop_back(), m.back()) << "step " << step; m.pop_back(); }
                break;
            case 6:
                if(!m.empty())
                {
                    const size_t i = rng() % m.size();
                    ASSERT_EQ(b[static_cast<decltype(b.get_count())>(i)], m[i]) << "step " << step << " index " << i;
                }
                break;
            case 7:
                if(rng() % 50 == 0) { b.flush(); m.clear(); }
                break;
        }
        ASSERT_EQ(static_cast<size_t>(b.get_count()), m.size()) << "step " << step;
        ASSERT_EQ(static_cast<size_t>(b.get_free_size()), N - m.size()) << "step " << step;
    }
}

class RingBufferModel : public ::testing::TestWithParam<unsigned> { };

TEST_P(RingBufferModel, Size5)          { model_check<ring_buffer<int, 5>, 5>(GetParam(), 20000); }
TEST_P(RingBufferModel, Size64)         { model_check<ring_buffer<int, 64>, 64>(GetParam(), 20000); }
TEST_P(RingBufferModel, Size127)        { model_check<ring_buffer<int, 127>, 127>(GetParam(), 20000); }
TEST_P(RingBufferModel, Size1000Index16){ model_check<ring_buffer<int, 1000, uint16_t>, 1000>(GetParam(), 40000); }

INSTANTIATE_TEST_SUITE_P(Seeds, RingBufferModel, ::testing::Range(1u, 6u));
