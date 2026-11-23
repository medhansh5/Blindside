#include "blindside/ring_buffer.hpp"
#include <cassert>
#include <iostream>
#include <thread>

void test_capacity_and_overflow() {
    blindside::RingBuffer<int, 3> buffer;
    assert(buffer.size() == 0);

    buffer.push(10);
    buffer.push(20);
    buffer.push(30);
    assert(buffer.size() == 3);

    // Push 4th element (overflows, drops oldest element '10')
    buffer.push(40);
    assert(buffer.size() == 3);

    auto item1 = buffer.try_pop();
    assert(item1.has_value() && item1.value() == 20);
    (void)item1;

    auto item2 = buffer.try_pop();
    assert(item2.has_value() && item2.value() == 30);
    (void)item2;

    auto item3 = buffer.try_pop();
    assert(item3.has_value() && item3.value() == 40);
    (void)item3;

    assert(buffer.size() == 0);
    std::cout << "[TEST PASSED] RingBuffer capacity and overflow test." << std::endl;
}

void test_concurrent_push_pop() {
    blindside::RingBuffer<int, 256> buffer;
    constexpr int total_items = 200;

    std::thread producer([&]() {
        for (int i = 0; i < total_items; ++i) {
            buffer.push(i);
            std::this_thread::sleep_for(std::chrono::microseconds(10));
        }
        buffer.stop();
    });

    int popped_count = 0;
    std::thread consumer([&]() {
        while (!buffer.is_stopped() || buffer.size() > 0) {
            auto opt = buffer.wait_pop_for(std::chrono::milliseconds(50));
            if (opt.has_value()) {
                popped_count++;
            }
        }
    });

    producer.join();
    consumer.join();

    assert(popped_count == total_items);
    std::cout << "[TEST PASSED] RingBuffer concurrent producer-consumer lossless test." << std::endl;
}

void test_concurrent_overflow_with_stop() {
    blindside::RingBuffer<int, 16> buffer;
    constexpr int total_items = 200;

    std::thread producer([&]() {
        for (int i = 0; i < total_items; ++i) {
            buffer.push(i);
            std::this_thread::sleep_for(std::chrono::microseconds(5));
        }
        buffer.stop();
    });

    int popped_count = 0;
    std::thread consumer([&]() {
        while (!buffer.is_stopped() || buffer.size() > 0) {
            auto opt = buffer.wait_pop_for(std::chrono::milliseconds(20));
            if (opt.has_value()) {
                popped_count++;
            }
        }
    });

    producer.join();
    consumer.join();

    assert(buffer.is_stopped());
    assert(popped_count > 0 && popped_count <= total_items);
    std::cout << "[TEST PASSED] RingBuffer concurrent overflow with clean stop test." << std::endl;
}

void test_stop_unblocks_waiting_consumer() {
    blindside::RingBuffer<int, 8> buffer;
    std::atomic<bool> unblocked{false};
    auto start = std::chrono::steady_clock::now();

    std::thread consumer([&]() {
        auto opt = buffer.wait_pop_for(std::chrono::seconds(5));
        assert(!opt.has_value());
        unblocked = true;
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    buffer.stop();
    consumer.join();

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start
    ).count();

    assert(unblocked.load());
    assert(elapsed < 2000); // Must unblock immediately on stop, well before 5s timeout
    std::cout << "[TEST PASSED] RingBuffer stop unblocks waiting consumer test." << std::endl;
}

int main() {
    test_capacity_and_overflow();
    test_concurrent_push_pop();
    test_concurrent_overflow_with_stop();
    test_stop_unblocks_waiting_consumer();
    std::cout << "All RingBuffer unit tests passed successfully!\n";
    return 0;
}
