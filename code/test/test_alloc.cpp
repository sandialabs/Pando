#include "test.hpp"

#include "big_space.hpp"

#include <string>
#include <vector>
#include <cstdint>

using namespace std;
using namespace pando;

static bool aligned16(const void* p) {
    return (reinterpret_cast<uintptr_t>(p) % 16) == 0;
}

// True if [a, a+na) and [b, b+nb) do not overlap
static bool disjoint(const char* a, size_t na, const char* b, size_t nb) {
    return a+na <= b || b+nb <= a;
}

TEST(bigspace) {

    BigSpace sp;

    char* a1 = (char*)sp.allocate(12);
    char* a2 = (char*)sp.allocate(2);

    NOPRINT_NOTEQ(a1, a2);
    EQ(disjoint(a1, 12, a2, 2), true);
    EQ(aligned16(a1), true);
    EQ(aligned16(a2), true);

    TEST_PASS
}

TEST(oom) {
    BigSpace sp { 64 };

    char* a1 = (char*)sp.allocate(1);
    char* a2 = (char*)sp.allocate(1);
    NOPRINT_NOTEQ(a1, a2);
    EQ(disjoint(a1, 1, a2, 1), true);

    try {
        // 32 bytes used, 32 left: a 33-byte request rounds to 48
        char* a3 = (char*)sp.allocate(33);
        (void)a3;
        TEST_FAIL
    } catch(const bad_alloc &e) { }

    // The failed request must not have consumed any space
    char* a4 = (char*)sp.allocate(32);
    EQ(disjoint(a4, 32, a1, 16), true);
    EQ(disjoint(a4, 32, a2, 16), true);

    TEST_PASS
}

TEST(alloc_raw) {
    // Each request is rounded up to a 16-byte size class: 2 ints -> 16, 3 ints -> 16
    auto sp = std::make_shared<BigSpace>(32);
    BigSpaceAllocator<int> bsa1{sp};
    int* int_sp1 = bsa1.allocate(2);
    BigSpaceAllocator<int> bsa2{sp};
    int* int_sp2 = bsa2.allocate(3);

    int_sp1[0] = 0;
    int_sp1[1] = 1;

    int_sp2[0] = 0;
    int_sp2[1] = 1;
    int_sp2[2] = 2;

    // fixme finish writing this...

    TEST_PASS
}

TEST(alloc_str) {
    auto sp = std::make_shared<BigSpace>(1ull<<10);
    char* first_byte = (char*)sp->allocate(1);
    BigSpaceAllocator<char> alloc{sp};
    basic_string<char, char_traits<char>, BigSpaceAllocator<char>> str {alloc};
    str.assign("0000000000000000"); // NOTE on gcc 11.2, short string optimization kicks in with string lengths 15 or less
    EQ(disjoint(first_byte, 16, str.c_str(), str.capacity()+1), true);
    EQ(aligned16(str.c_str()), true);
    EQ(string(str.c_str()), string("0000000000000000"));

    TEST_PASS
}

TEST(str_ops) {
    using Str = basic_string<char, char_traits<char>, BigSpaceAllocator<char>>;

    auto sp = std::make_shared<BigSpace>(1ull<<10);
    BigSpaceAllocator<char> alloc{sp};
    Str str {alloc};
    str.assign("0000000000000000asdf");

    auto next_str = str;

    NOPRINT_NOTEQ(str.c_str(), next_str.c_str());
    EQ(disjoint(str.c_str(), str.capacity()+1, next_str.c_str(), next_str.capacity()+1), true);
    EQ(string(next_str.c_str()), string("0000000000000000asdf"));

    const char* next_str_ptr = next_str.c_str();

    Str mv_str = move(next_str);

    NOPRINT_EQ(next_str_ptr, mv_str.c_str());

    TEST_PASS
}

TEST(copy_alloc) {
    auto sp = std::make_shared<BigSpace>(64);
    BigSpaceAllocator<char> a1{sp};
    auto a2 = a1;
    EQ(a1 == a2, true);

    char* s1 = a1.allocate(1);
    char* s2 = a2.allocate(1);

    NOPRINT_NOTEQ(s1, s2);
    EQ(disjoint(s1, 16, s2, 16), true);

    // Memory from one copy can be freed through the other
    a2.deallocate(s1, 1);
    char* s3 = a1.allocate(1);
    NOPRINT_EQ(s3, s1);

    TEST_PASS
}

TEST(move_alloc) {
    auto sp = std::make_shared<BigSpace>(64);
    BigSpaceAllocator<char> a1{sp};
    char* s1 = a1.allocate(1);

    auto a2 = std::move(a1);

    char* s2 = a2.allocate(1);

    NOPRINT_NOTEQ(s1, s2);
    EQ(disjoint(s1, 16, s2, 16), true);

    TEST_PASS
}

TEST(str_plus_equals) {
    using Str = basic_string<char, char_traits<char>, BigSpaceAllocator<char>>;
    
    auto sp = std::make_shared<BigSpace>(1ull<<10);
    BigSpaceAllocator<char> alloc{sp};
    Str str {alloc};
    str.assign("0000000000000000asdf");

    const char* str_ptr = str.c_str();

    str += "testing";

    NOPRINT_EQ(str.c_str(), str_ptr); 

    str += "1111111111111111";

    NOPRINT_NOTEQ(str.c_str(), str_ptr);
    EQ(aligned16(str.c_str()), true);
    EQ(string(str.c_str()), string("0000000000000000asdftesting1111111111111111"));

    TEST_PASS
}

TEST(str_plus_equals_two) {
    using Str = basic_string<char, char_traits<char>, BigSpaceAllocator<char>>;
    
    auto sp = std::make_shared<BigSpace>(1ull<<10);
    BigSpaceAllocator<char> alloc{sp};
    Str str1 {alloc};
    Str str2 {alloc};

    str1 = "0000000000000000ABC";
    str2 = "1111111111111111111";

    str1 += "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";
    str2 += "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB";

    const char* p1 = str1.c_str();
    Str str3 = move(str1);
    const char* p2 = str3.c_str();
    NOPRINT_EQ(p1, p2);

    cout << str2 << endl << str3 << endl;

    TEST_PASS
}

TEST(str_move) {
    using Str = basic_string<char, char_traits<char>, BigSpaceAllocator<char>>;
    
    auto sp = std::make_shared<BigSpace>(1ull<<10);
    BigSpaceAllocator<char> alloc{sp};
    Str str {alloc};
    str.assign("0000000000000000asdf");

    Str str2 {alloc};
    str2 = move(str);
    
    TEST_PASS
}

TEST(reuse) {
    BigSpace sp { 1ull<<10 };
    void* a = sp.allocate(32);
    sp.deallocate(a, 32);
    void* b = sp.allocate(32);
    NOPRINT_EQ(a, b);

    TEST_PASS
}

TEST(class_sharing) {
    BigSpace sp { 1ull<<10 };
    void* a = sp.allocate(20);
    sp.deallocate(a, 20);
    void* b = sp.allocate(30); // 20 and 30 both round to 32
    NOPRINT_EQ(a, b);

    TEST_PASS
}

TEST(no_cross_class_reuse) {
    BigSpace sp { 1ull<<10 };
    void* a = sp.allocate(32);
    sp.deallocate(a, 32);
    void* b = sp.allocate(64);
    NOPRINT_NOTEQ(a, b);
    EQ(disjoint((char*)a, 32, (char*)b, 64), true);

    TEST_PASS
}

TEST(lifo_order) {
    BigSpace sp { 1ull<<10 };
    void* a = sp.allocate(48);
    void* b = sp.allocate(48);
    sp.deallocate(a, 48);
    sp.deallocate(b, 48);
    NOPRINT_EQ(sp.allocate(48), b);
    NOPRINT_EQ(sp.allocate(48), a);

    TEST_PASS
}

TEST(steady_state) {
    using Str = basic_string<char, char_traits<char>, BigSpaceAllocator<char>>;

    // Without reuse this workload needs >12MB. With size-class free lists the
    // footprint is bounded (each class holds at most a few blocks, since
    // blocks are never split or merged), and fits in 64KiB.
    auto sp = std::make_shared<BigSpace>(1ull<<16);
    BigSpaceAllocator<char> alloc{sp};

    try {
        Str str {alloc};
        for (size_t iter = 0; iter < 1000; ++iter) {
            // Grow
            for (size_t len = 16; len <= 2048; len *= 2) {
                str.assign(len, 'a' + (iter % 26));
                EQ(str.size(), len);
            }
            // Shrink (assigning a fresh, smaller string forces reallocation)
            for (size_t len = 2048; len >= 16; len /= 2) {
                str = Str(len, 'z', alloc);
                EQ(str.size(), len);
            }
        }

        // Many overwrites of a fixed set of values
        vector<Str> vals;
        for (size_t i = 0; i < 8; ++i)
            vals.emplace_back(alloc);
        for (size_t iter = 0; iter < 10000; ++iter) {
            vals[iter % vals.size()] = Str(100 + (iter % 200), 'x', alloc);
        }
    } catch (const bad_alloc&) {
        TEST_FAIL
    }

    TEST_PASS
}

TEST(exact_fill) {
    BigSpace sp { 64 };
    void* a = sp.allocate(64);
    NOPRINT_NOTEQ(a, nullptr);
    try {
        void* b = sp.allocate(1);
        (void)b;
        TEST_FAIL
    } catch (const bad_alloc&) { }

    // Once freed, the full block can be handed out again
    sp.deallocate(a, 64);
    NOPRINT_EQ(sp.allocate(64), a);

    TEST_PASS
}

TEST(class_boundaries) {
    EQ(BigSpace::class_size(BigSpace::size_class_index(1)), 16ull);
    EQ(BigSpace::class_size(BigSpace::size_class_index(16)), 16ull);
    EQ(BigSpace::class_size(BigSpace::size_class_index(17)), 32ull);
    EQ(BigSpace::class_size(BigSpace::size_class_index(1024)), 1024ull);
    EQ(BigSpace::class_size(BigSpace::size_class_index(1025)), 1280ull);
    EQ(BigSpace::class_size(BigSpace::size_class_index(1280)), 1280ull);
    EQ(BigSpace::class_size(BigSpace::size_class_index(1281)), 1536ull);
    EQ(BigSpace::class_size(BigSpace::size_class_index(1ull<<20)), 1ull<<20);

    EQ(BigSpace::size_class_index(1024), BigSpace::NUM_SMALL_CLASSES-1);
    EQ(BigSpace::size_class_index(1025), BigSpace::NUM_SMALL_CLASSES);

    size_t prev_idx = 0;
    for (size_t n = 1; n <= (1ull<<22); n += (n < 4096 ? 1 : 97)) {
        size_t idx = BigSpace::size_class_index(n);
        size_t cs = BigSpace::class_size(idx);
        EQ(idx < BigSpace::NUM_CLASSES, true);
        EQ(cs >= n, true);
        EQ(cs % 16, 0ull);
        EQ(idx >= prev_idx, true); // monotonic
        if (n > 1024) {
            EQ(cs * 4 <= n * 5 + 4, true); // at most ~25% waste
        }
        // The class must be the smallest one that fits
        if (idx > 0) {
            EQ(BigSpace::class_size(idx-1) < n, true);
        }
        prev_idx = idx;
    }

    // Largest class index must still be in range
    EQ(BigSpace::size_class_index(~0ull >> 1) < BigSpace::NUM_CLASSES, true);

    TEST_PASS
}

TESTS_BEGIN
    RUN_TEST(bigspace)
    RUN_TEST(oom)
    RUN_TEST(alloc_raw)
    RUN_TEST(alloc_str)
    RUN_TEST(str_ops)
    RUN_TEST(copy_alloc)
    RUN_TEST(move_alloc)
    RUN_TEST(str_plus_equals)
    RUN_TEST(str_plus_equals_two)
    RUN_TEST(str_move)
    RUN_TEST(reuse)
    RUN_TEST(class_sharing)
    RUN_TEST(no_cross_class_reuse)
    RUN_TEST(lifo_order)
    RUN_TEST(steady_state)
    RUN_TEST(exact_fill)
    RUN_TEST(class_boundaries)
TESTS_END
