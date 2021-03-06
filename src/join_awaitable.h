#pragma once

#include <variant>

#include "asio_with_aliases.h"

namespace traft {

template<typename... AwaitableTypes>
asio::awaitable<std::variant<AwaitableTypes...>> fastest(asio::awaitable<AwaitableTypes>... awaitables) {
    // To avoid race on completion handler among awaitables threads.
    // Probably can be done in a more efficient way, but doesn't matter now.
    auto strand = asio::make_strand(co_await asio::this_coro::executor);

    // The lambda right below is called after the coroutine suspension.
    // 'handler' is the cororutine handler, so it should be called to resume coroutine.
    auto implementation = [strand, ...awaitables = std::move(awaitables)]<std::size_t... Indexes>(
        auto handler, std::index_sequence<Indexes...>) mutable {
        // `HandlerType` is movable-only, so store is mandatory here
        auto handler_store = std::make_shared<std::optional<decltype(handler)>>();
        handler_store->emplace(std::move(handler));
        (boost::asio::co_spawn(
            strand,
            // Executor is captured by value, so it won't be removed early.
            [strand, awaitables = std::move(awaitables), handler_store]() mutable -> asio::awaitable<void> {
                auto result = co_await std::move(awaitables);
                // It's possible to make here separate dispatch on strand, so the actual work on the line above
                // can be done simultaneously.
                if (!handler_store->has_value()) {
                    // We're too late - other awaiter was faster.
                    co_return;
                }
                asio::post(strand, [result = std::move(result), handler = std::move(handler_store->value())]() mutable {
                    // TODO: exceptions
                    handler(
                        errc::make_error_code(errc::success),
                        std::variant<AwaitableTypes...>{std::in_place_index_t<Indexes>{}, std::move(result)});
                });
                handler_store->reset();
            }, boost::asio::detached), ...);
    };

    auto proxy = [implementation = std::move(implementation)](auto handler) mutable {
        implementation(std::move(handler), std::make_index_sequence<sizeof...(AwaitableTypes)>{});
    };

    // Using built-in asio free function in order to properly implement async call
    using HandlerFunction = void(error_code, std::variant<AwaitableTypes...>);
    co_return co_await async_initiate<const asio::use_awaitable_t<>, HandlerFunction>(
        std::move(proxy), asio::use_awaitable);
}

} // namespace traft