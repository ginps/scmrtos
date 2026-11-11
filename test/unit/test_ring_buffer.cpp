//******************************************************************************
//*
//*     scmRTOS unit tests: usr::ring_buffer (core/usrlib.h)
//*
//*     Plain unit tests (the kernel is not started) and a model-based test:
//*     random operation sequences are applied both to ring_buffer and to
//*     std::deque, and the two are compared after every step.
//*
//******************************************************************************
#include "utest.h"
#include <deque>
#include <stdio.h>
#include <stdint.h>

using usr::ring_buffer;

//==============================================================================
//
//    Basic operations
//
UT_TEST(ring_buffer, empty_after_construction)
{
    ring_buffer<int, 8> b;
    CHECK_EQ(b.get_count(), 0);
    CHECK_EQ(b.get_free_size(), 8);
}

UT_TEST(ring_buffer, push_back_pop_front_is_fifo)
{
    ring_buffer<int, 8> b;
    for(int i = 0; i < 5; ++i) CHECK(b.push_back(i));
    CHECK_EQ(b.get_count(), 5);
    CHECK_EQ(b.get_free_size(), 3);
    for(int i = 0; i < 5; ++i) CHECK_EQ(b.pop_front(), i);
    CHECK_EQ(b.get_count(), 0);
}

UT_TEST(ring_buffer, push_and_pop_aliases)
{
    ring_buffer<int, 4> b;
    CHECK(b.push(1)); CHECK(b.push(2));
    CHECK_EQ(b.pop(), 1);
    CHECK_EQ(b.pop(), 2);
}

UT_TEST(ring_buffer, push_back_pop_back_is_lifo)
{
    ring_buffer<int, 8> b;
    for(int i = 0; i < 5; ++i) b.push_back(i);
    for(int i = 4; i >= 0; --i) CHECK_EQ(b.pop_back(), i);
}

UT_TEST(ring_buffer, push_front_pop_front_is_lifo)
{
    ring_buffer<int, 8> b;
    for(int i = 0; i < 5; ++i) CHECK(b.push_front(i));
    for(int i = 4; i >= 0; --i) CHECK_EQ(b.pop_front(), i);
}

UT_TEST(ring_buffer, push_front_pop_back_is_fifo)
{
    ring_buffer<int, 8> b;
    for(int i = 0; i < 5; ++i) b.push_front(i);
    for(int i = 0; i < 5; ++i) CHECK_EQ(b.pop_back(), i);
}

UT_TEST(ring_buffer, both_ends_mixed)
{
    ring_buffer<int, 6> b;
    b.push_back(2); b.push_front(1); b.push_back(3); b.push_front(0);   // 0 1 2 3
    CHECK_EQ(b[0], 0); CHECK_EQ(b[1], 1); CHECK_EQ(b[2], 2); CHECK_EQ(b[3], 3);
    CHECK_EQ(b.pop_back(), 3);
    CHECK_EQ(b.pop_front(), 0);
    CHECK_EQ(b.get_count(), 2);
    CHECK_EQ(b.pop_front(), 1);
    CHECK_EQ(b.pop_back(), 2);
}

UT_TEST(ring_buffer, full_rejects_push_and_keeps_data)
{
    ring_buffer<int, 3> b;
    CHECK(b.push_back(1)); CHECK(b.push_back(2)); CHECK(b.push_back(3));
    CHECK_EQ(b.get_free_size(), 0);
    CHECK(!b.push_back(4));
    CHECK(!b.push_front(0));
    CHECK_EQ(b.get_count(), 3);
    CHECK_EQ(b.pop_front(), 1); CHECK_EQ(b.pop_front(), 2); CHECK_EQ(b.pop_front(), 3);
}

UT_TEST(ring_buffer, pop_from_empty_keeps_count_zero)
{
    ring_buffer<int, 4> b;
    (void)b.pop_front();
    (void)b.pop_back();
    CHECK_EQ(b.get_count(), 0);
    CHECK_EQ(b.get_free_size(), 4);
    CHECK(b.push_back(7));
    CHECK_EQ(b.pop_front(), 7);
}

UT_TEST(ring_buffer, size_one)
{
    ring_buffer<int, 1> b;
    for(int i = 0; i < 10; ++i)
    {
        CHECK(b.push_back(i));
        CHECK(!b.push_back(99));
        CHECK(!b.push_front(99));
        CHECK_EQ(b[0], i);
        CHECK_EQ(i % 2 ? b.pop_back() : b.pop_front(), i);
    }
}

UT_TEST(ring_buffer, wraps_around_many_times)
{
    ring_buffer<int, 5> b;
    int next_in = 0, next_out = 0;
    for(int round = 0; round < 1000; ++round)
    {
        while(b.get_free_size() > 1) b.push_back(next_in++);
        CHECK_EQ(b.pop_front(), next_out++);
        CHECK_EQ(b.pop_front(), next_out++);
    }
    while(b.get_count()) CHECK_EQ(b.pop_front(), next_out++);
    CHECK_EQ(next_in, next_out);
}

UT_TEST(ring_buffer, push_front_wraps_from_index_zero)
{
    ring_buffer<int, 4> b;
    b.push_front(1);                      // First goes from 0 to Size-1
    b.push_front(0);
    b.push_back(2);
    CHECK_EQ(b[0], 0); CHECK_EQ(b[1], 1); CHECK_EQ(b[2], 2);
    CHECK_EQ(b.pop_back(), 2);
    CHECK_EQ(b.pop_back(), 1);
    CHECK_EQ(b.pop_back(), 0);
}

UT_TEST(ring_buffer, index_operator_is_writable)
{
    ring_buffer<int, 4> b;
    b.push_back(1); b.push_back(2);
    b[1] = 20;
    CHECK_EQ(b.pop_front(), 1);
    CHECK_EQ(b.pop_front(), 20);
}

UT_TEST(ring_buffer, flush_empties)
{
    ring_buffer<int, 4> b;
    b.push_back(1); b.push_back(2); b.pop_front(); b.push_back(3);
    b.flush();
    CHECK_EQ(b.get_count(), 0);
    CHECK_EQ(b.get_free_size(), 4);
    for(int i = 0; i < 4; ++i) CHECK(b.push_back(10 + i));
    for(int i = 0; i < 4; ++i) CHECK_EQ(b.pop_front(), 10 + i);
}

//==============================================================================
//
//    Bulk write / read
//
UT_TEST(ring_buffer, write_read_bulk)
{
    ring_buffer<int, 8> b;
    const int in[5] = { 1, 2, 3, 4, 5 };
    CHECK(b.write(in, 5));
    CHECK_EQ(b.get_count(), 5);
    int out[5] = {};
    b.read(out, 5);
    for(int i = 0; i < 5; ++i) CHECK_EQ(out[i], in[i]);
    CHECK_EQ(b.get_count(), 0);
}

UT_TEST(ring_buffer, write_too_much_is_rejected_whole)
{
    ring_buffer<int, 4> b;
    const int in[5] = { 1, 2, 3, 4, 5 };
    b.push_back(0);
    CHECK(!b.write(in, 4));               // 3 free
    CHECK_EQ(b.get_count(), 1);
    CHECK(b.write(in, 3));
    CHECK_EQ(b.get_free_size(), 0);
}

UT_TEST(ring_buffer, read_more_than_available_reads_what_is_there)
{
    ring_buffer<int, 8> b;
    b.push_back(1); b.push_back(2);
    int out[4] = { -1, -1, -1, -1 };
    b.read(out, 4);
    CHECK_EQ(out[0], 1); CHECK_EQ(out[1], 2);
    CHECK_EQ(out[2], -1); CHECK_EQ(out[3], -1);
    CHECK_EQ(b.get_count(), 0);
}

UT_TEST(ring_buffer, write_read_across_wrap)
{
    ring_buffer<int, 6> b;
    int in[6], out[6];
    for(int k = 0; k < 6; ++k) in[k] = k * 11;
    for(int shift = 0; shift < 12; ++shift)
    {
        b.push_back(-1);
        (void)b.pop_front();               // move First/Last by one
        CHECK(b.write(in, 6));
        b.read(out, 6);
        for(int k = 0; k < 6; ++k) CHECK_EQ(out[k], in[k]);
    }
}

UT_TEST(ring_buffer, zero_length_write_and_read)
{
    ring_buffer<int, 4> b;
    int x = 5;
    CHECK(b.write(&x, 0));
    b.read(&x, 0);
    CHECK_EQ(b.get_count(), 0);
    CHECK_EQ(x, 5);
}

//==============================================================================
//
//    Element types and index types
//
struct TItem
{
    uint32_t id;
    double   value;
    char     tag[5];
};

UT_TEST(ring_buffer, struct_items_are_copied)
{
    ring_buffer<TItem, 3> b;
    TItem a = { 7, 2.5, "abc" };
    b.push_back(a);
    a.id = 0;                              // the buffer keeps its own copy
    TItem r = b.pop_front();
    CHECK_EQ(r.id, 7);
    CHECK(r.value == 2.5);
    CHECK(r.tag[0] == 'a' && r.tag[2] == 'c' && r.tag[3] == 0);
}

UT_TEST(ring_buffer, wide_items)
{
    ring_buffer<uint64_t, 4> b;
    b.push_back(0xFFFFFFFFFFFFFFFFull);
    b.push_front(0x123456789ABCDEF0ull);
    CHECK(b.pop_back()  == 0xFFFFFFFFFFFFFFFFull);
    CHECK(b.pop_front() == 0x123456789ABCDEF0ull);
}

UT_TEST(ring_buffer, uint16_index_large_size)
{
    ring_buffer<uint16_t, 1000, uint16_t> b;
    CHECK_EQ(b.get_free_size(), 1000);
    for(int i = 0; i < 1000; ++i) CHECK(b.push_back(uint16_t(i)));
    CHECK(!b.push_back(0));
    for(int i = 0; i < 1000; ++i) CHECK_EQ(b[i], i);
    for(int i = 0; i < 1000; ++i) CHECK_EQ(b.pop_front(), i);
}

UT_TEST(ring_buffer, max_size_for_uint8_index)
{
    ring_buffer<int, 255> b;
    CHECK_EQ(b.get_free_size(), 255);
    for(int i = 0; i < 255; ++i) CHECK(b.push_back(i));
    CHECK(!b.push_back(0));
    for(int i = 0; i < 255; ++i) CHECK_EQ(b.pop_front(), i);
}

UT_TEST_DEFECT(ring_buffer, size_above_index_range, "4")
{
    // Size 300 with the default uint8_t index: Count/First/Last wrap at 256
#ifdef UT_RB_SIZE_CHECKED
    // the tree under test rejects ring_buffer<int, 300> with a static_assert,
    // so the defect cannot occur (and the type cannot even be declared here)
    CHECK(true);
#else
    ring_buffer<int, 300> b;
    CHECK_EQ(b.get_free_size(), 300);
    int n = 0;
    while(n < 1000 && b.push_back(n)) ++n;
    CHECK_EQ(n, 300);
#endif
}

//==============================================================================
//
//    Model-based test against std::deque
//
template<typename B, unsigned Size>
static void model_test(unsigned seed, unsigned steps)
{
    B b;
    std::deque<int> m;
    unsigned x = seed;
    int next = 0;
    for(unsigned step = 0; step < steps; ++step)
    {
        x = x * 1103515245u + 12345u;
        unsigned op = (x >> 16) % 9;
        bool ok = true;
        switch(op)
        {
            case 0: case 1:
            {
                bool r = b.push_back(next);
                ok = r == (m.size() < Size);
                if(r) m.push_back(next);
                ++next;
                break;
            }
            case 2:
            {
                bool r = b.push_front(next);
                ok = r == (m.size() < Size);
                if(r) m.push_front(next);
                ++next;
                break;
            }
            case 3: case 4:
                if(!m.empty()) { ok = b.pop_front() == m.front(); m.pop_front(); }
                break;
            case 5:
                if(!m.empty()) { ok = b.pop_back() == m.back(); m.pop_back(); }
                break;
            case 6:
            {
                int in[7];
                unsigned n = (x >> 8) % 8;
                for(unsigned k = 0; k < n && k < 7; ++k) in[k] = next++;
                if(n > 7) n = 7;
                bool r = b.write(in, n);
                ok = r == (m.size() + n <= Size);
                if(r) for(unsigned k = 0; k < n; ++k) m.push_back(in[k]);
                break;
            }
            case 7:
            {
                int out[7];
                unsigned n = (x >> 8) % 8;
                if(n > 7) n = 7;
                unsigned got = n < m.size() ? n : unsigned(m.size());
                b.read(out, n);
                for(unsigned k = 0; k < got && ok; ++k) { ok = out[k] == m.front(); m.pop_front(); }
                break;
            }
            case 8:
                if((x >> 4) % 64 == 0) { b.flush(); m.clear(); }
                break;
        }
        if(ok) ok = b.get_count() == m.size() && b.get_free_size() == Size - m.size();
        for(unsigned k = 0; ok && k < m.size(); ++k)
            ok = b[k] == m[k];
        if(!CHECK(ok))
        {
            printf("    model mismatch: Size %u, seed %u, step %u, op %u, count %u/%u\n",
                   Size, seed, step, op, unsigned(b.get_count()), unsigned(m.size()));
            return;
        }
    }
}

UT_TEST(ring_buffer, model_sizes_1_to_16)
{
    model_test<ring_buffer<int,  1>,  1>(1, 3000);
    model_test<ring_buffer<int,  2>,  2>(2, 3000);
    model_test<ring_buffer<int,  3>,  3>(3, 3000);
    model_test<ring_buffer<int,  7>,  7>(4, 5000);
    model_test<ring_buffer<int,  8>,  8>(5, 5000);
    model_test<ring_buffer<int, 16>, 16>(6, 5000);
}

UT_TEST(ring_buffer, model_sizes_up_to_128)
{
    model_test<ring_buffer<int,  31>,  31>(7, 20000);
    model_test<ring_buffer<int,  64>,  64>(8, 20000);
    model_test<ring_buffer<int, 128>, 128>(9, 20000);
}

UT_TEST(ring_buffer, model_uint16_index)
{
    model_test<ring_buffer<int, 200, uint16_t>, 200>(10, 50000);
    model_test<ring_buffer<int, 999, uint16_t>, 999>(11, 50000);
}

UT_TEST_DEFECT(ring_buffer, model_size_200_uint8_index, "5")
{
    // operator[] computes First + index in uint8_t: wrong element once it exceeds 255
    model_test<ring_buffer<int, 200>, 200>(12, 50000);
}

UT_TEST_DEFECT(ring_buffer, index_across_wrap_size_200, "5")
{
    ring_buffer<int, 200> b;
    for(int i = 0; i < 150; ++i) b.push_back(i);
    for(int i = 0; i < 150; ++i) b.pop_front();          // First = 150
    for(int i = 0; i < 170; ++i) b.push_back(1000 + i);
    CHECK_EQ(b[120], 1120);
}
