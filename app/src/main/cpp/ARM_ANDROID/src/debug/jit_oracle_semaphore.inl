// SPDX-License-Identifier: GPL-3.0+
namespace Threading
{
struct UserspaceSemaphoreOracle
{
    template <typename Predicate>
    static bool Until(Predicate predicate)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (!predicate())
        {
            if (std::chrono::steady_clock::now() >= deadline)
                return false;
            std::this_thread::yield();
        }
        return true;
    }

    static void Run(void (*check)(bool, const char*))
    {
        for (u32 spinning = 0; spinning < 2; ++spinning)
        for (const auto& scenario : std::array<std::array<int, 2>, 7>{{{0, 0}, {0, 8}, {1, 32}, {2, 2}, {4, 8}, {4, 2}, {4, 0}}})
        {
            const int waiters = scenario[0], permits = scenario[1];
            UserspaceSemaphore sem;
            std::atomic<int> completed{0};
            std::vector<std::thread> threads;
            for (int waiter = 0; waiter < waiters; ++waiter)
                threads.emplace_back([&] {
                    if (spinning) sem.WaitWithSpin(); else sem.Wait();
                    completed.fetch_add(1, std::memory_order_release);
                });
            const bool registered = Until([&] { return sem.m_counter.load(std::memory_order_acquire) == -waiters; });
            check(registered, "semaphore fixture registers every blocking waiter before posting");
            sem.Post(permits);
            const int expected_first = std::min(waiters, permits);
            bool first_wake;
            if (permits)
                first_wake = Until([&] { return completed.load(std::memory_order_acquire) >= expected_first; });
            else
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                first_wake = true;
            }
            first_wake &= completed.load(std::memory_order_acquire) == expected_first;
            check(first_wake, "semaphore batch wakes exactly the waiters covered by its permits");
            const int remainder = std::max(0, waiters - permits);
            if (remainder)
                sem.Post(remainder);
            const bool all_done = Until([&] { return completed.load(std::memory_order_acquire) == waiters; });
            check(all_done, "semaphore later batches release every remaining waiter without losing wakes");
            std::printf("SEMAPHORE spin=%u waiters=%d permits=%d completed=%d\n", spinning, waiters, permits,
                completed.load(std::memory_order_acquire));
            if (!all_done)
            {
                // A failed wake test must finish without leaving detached or
                // blocked threads. All waits already decremented the counter.
                sem.m_counter.store(0, std::memory_order_release);
                for (int waiter = 0; waiter < waiters; ++waiter)
                    sem.m_sema.Post();
            }
            for (auto& thread : threads)
                thread.join();
            if (all_done)
            {
                int surplus = 0;
                while (sem.TryWait()) ++surplus;
                check(surplus == std::max(0, permits - waiters) && !sem.m_sema.TryWait(),
                    "semaphore surplus permits stay in userspace without extra kernel wake tokens");
            }
        }

        // Several producers can publish into one batch semaphore concurrently.
        // Repeated rounds exercise consumers already blocked and posts in flight.
        for (u32 round = 0; round < 32; ++round)
        {
            UserspaceSemaphore sem;
            std::atomic<int> completed{0};
            std::vector<std::thread> consumers, producers;
            for (u32 consumer = 0; consumer < 4; ++consumer)
                consumers.emplace_back([&] { sem.Wait(); completed.fetch_add(1, std::memory_order_release); });
            const bool registered = Until([&] { return sem.m_counter.load(std::memory_order_acquire) == -4; });
            for (u32 producer = 0; producer < 2; ++producer)
                producers.emplace_back([&, producer] {
                    if ((round & 1) && producer) { sem.Post(); sem.Post(); }
                    else sem.Post(2);
                });
            for (auto& producer : producers) producer.join();
            const bool done = Until([&] { return completed.load(std::memory_order_acquire) == 4; });
            if (!done)
            {
                sem.m_counter.store(0, std::memory_order_release);
                for (u32 waiter = 0; waiter < 4; ++waiter) sem.m_sema.Post();
            }
            for (auto& consumer : consumers) consumer.join();
            check(registered && done && sem.m_counter.load(std::memory_order_acquire) == 0,
                "semaphore concurrent batch producers release all registered consumers");
        }
    }
};
}
