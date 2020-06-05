#include <gtest/gtest.h>

#include "forward_call.h"

#include "logging.h"

TEST(ForwardCall, Basics) {
    asio::io_context this_context;
    auto this_id = std::this_thread::get_id();

    asio::io_context other_context;
    std::thread other_thread([&] { other_context.run(); });
    auto other_guard = asio::make_work_guard(other_context);
    auto other_id = other_thread.get_id();

    auto coroutine = [&]() -> asio::awaitable<void> {
        EXPECT_EQ(std::this_thread::get_id(), this_id);
        for (const int expected_result : std::vector<int>{844, 42, 100, -100000}) {
            const int result = co_await traft::forward_call(other_context, [=] {
                EXPECT_EQ(std::this_thread::get_id(), other_id);
                return expected_result;
            });
            EXPECT_EQ(std::this_thread::get_id(), this_id);
            EXPECT_EQ(result, expected_result);
        }
    };

    boost::asio::co_spawn(this_context, coroutine, boost::asio::detached);
    this_context.run();

    other_guard.reset();
    other_thread.join();
}