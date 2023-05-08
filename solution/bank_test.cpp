#include "bank.h"
#include <array>
#include <condition_variable>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>
#include "doctest.h"

#define TEST_USER_TRANSACTION_ITERATOR

namespace doctest {
template <>
struct StringMaker<bank::transaction> {
    static String convert(const bank::transaction &t) {
        std::stringstream str;
        str << "{"
            << (t.counterparty != nullptr ? t.counterparty->name() : "nullptr")
            << ", " << t.balance_delta_xts << ", \"" << t.comment << "\"}";
        return str.str().c_str();
    }
};

template <>
struct StringMaker<std::vector<bank::transaction>> {
    static String convert(const std::vector<bank::transaction> &v) {
        std::stringstream str;
        str << "{";
        bool first = true;
        for (const auto &t : v) {
            if (!first) {
                str << ", ";
            }
            first = false;
            // Inefficient, but good enough for logging.
            str << StringMaker<bank::transaction>::convert(t);
        }
        str << "}";
        return str.str().c_str();
    }
};
}  // namespace doctest

namespace bank {
// We should be able to look up operators via ADL.
bool operator==(const bank::transaction &a, const bank::transaction &b) {
    return a.counterparty == b.counterparty &&
           a.balance_delta_xts == b.balance_delta_xts && a.comment == b.comment;
}

bool operator!=(const bank::transaction &a, const bank::transaction &b) {
    return !(a == b);
}
}  // namespace bank

TEST_CASE("Create user") {
    bank::ledger l = {};
    bank::user &alice = l.get_or_create_user("Alice");
    const bank::user &bob = l.get_or_create_user("Bob");
    const bank::user &zero = l.get_or_create_user("0");

    CHECK(alice.name() == "Alice");
    CHECK(alice.balance_xts() == 100);

    CHECK(std::as_const(alice).name() == "Alice");
    CHECK(std::as_const(alice).balance_xts() == 100);

    CHECK(bob.name() == "Bob");
    CHECK(bob.balance_xts() == 100);

    CHECK(zero.name() == "0");
    CHECK(zero.balance_xts() == 100);
}

TEST_CASE("Create and get user") {
    bank::ledger l;
    bank::user &alice1 = l.get_or_create_user("Alice");
    l.get_or_create_user("Bob");
    bank::user &alice2 = l.get_or_create_user("Alice");
    CHECK(&alice1 == &alice2);
}

namespace {
template <typename T>
constexpr bool is_cvref_v =
    std::is_reference_v<T> &&std::is_const_v<std::remove_reference_t<T>>;
}

TEST_CASE("Snapshot initial transaction") {
    bank::ledger l;
    bank::user &alice = l.get_or_create_user("Alice");

    std::vector<bank::transaction> transactions_snapshot;
    SUBCASE("non-const") {
#ifdef TEST_USER_TRANSACTION_ITERATOR
        bank::user_transactions_iterator it =
#endif
            alice.snapshot_transactions(
                [&](auto &transactions, int balance_xts) {
                    CHECK(is_cvref_v<decltype(transactions)>);
                    transactions_snapshot =
                        std::vector(transactions.begin(), transactions.end());
                    CHECK(balance_xts == 100);
                });
#ifdef TEST_USER_TRANSACTION_ITERATOR
        static_cast<void>(it);
#endif
    }
    SUBCASE("const") {
#ifdef TEST_USER_TRANSACTION_ITERATOR
        bank::user_transactions_iterator it =
#endif
            std::as_const(alice).snapshot_transactions(
                [&](auto &transactions, int balance_xts) {
                    CHECK(is_cvref_v<decltype(transactions)>);
                    transactions_snapshot =
                        std::vector(transactions.begin(), transactions.end());
                    CHECK(balance_xts == 100);
                });
#ifdef TEST_USER_TRANSACTION_ITERATOR
        static_cast<void>(it);
#endif
    }
    CHECK(transactions_snapshot ==
          std::vector{
              bank::transaction{nullptr, 100, "Initial deposit for Alice"}});
}

TEST_CASE("Qualifiers") {
    SUBCASE("transaction") {
        SUBCASE("constructor is implicit") {
            bank::ledger l;
            bank::user &alice = l.get_or_create_user("Alice");
            bank::transaction t = {&alice, 10, std::string("comment")};
            static_cast<void>(t);
        }
        SUBCASE(
            "Immutable, non-assignable, copyable for easier "
            "multithreading") {
            CHECK(!std::is_move_assignable_v<bank::transaction>);
            CHECK(!std::is_copy_assignable_v<bank::transaction>);
            CHECK(std::is_move_constructible_v<
                  bank::transaction>);  // Does not really move, for
                                        // compatibility.
            CHECK(std::is_copy_constructible_v<bank::transaction>);

            CHECK(std::is_const_v<decltype(bank::transaction::counterparty)>);
            CHECK(std::is_const_v<
                  decltype(bank::transaction::balance_delta_xts)>);
            CHECK(std::is_const_v<decltype(bank::transaction::comment)>);
        }
    }
#ifdef TEST_USER_TRANSACTION_ITERATOR
    SUBCASE("user_transaction_iterator") {
        SUBCASE("Non-publicly-constructible from user") {
            CHECK(!std::is_constructible_v<bank::user_transactions_iterator,
                                           bank::user>);
        }
    }
#endif
    SUBCASE("user") {
        SUBCASE("Explicitly constructible from string") {
            CHECK(!std::is_convertible_v<std::string, bank::user>);
            CHECK(std::is_constructible_v<bank::user, std::string>);
        }
        SUBCASE("name() is const noexcept") {
            CHECK(noexcept(std::declval<const bank::user>().name()));
        }
        SUBCASE("balance_xts() is const") {
            CHECK(!noexcept(std::declval<const bank::user>().balance_xts()));
        }
        SUBCASE("Non-movable, non-copyable for easier multithreading") {
            CHECK(!std::is_move_assignable_v<bank::user>);
            CHECK(!std::is_copy_assignable_v<bank::user>);
            CHECK(!std::is_move_constructible_v<bank::user>);
            CHECK(!std::is_copy_constructible_v<bank::user>);
        }
    }
}

TEST_CASE("Simple transfer") {
    bank::ledger l;
    bank::user &alice = l.get_or_create_user("Alice");
    bank::user &bob = l.get_or_create_user("Bob");

    alice.transfer(bob, 40, "Test transfer");
    CHECK(alice.name() == "Alice");
    CHECK(bob.name() == "Bob");
    CHECK(alice.balance_xts() == 60);
    CHECK(bob.balance_xts() == 140);

    alice.snapshot_transactions([&](auto &transactions, int balance_xts) {
        CHECK(std::vector(transactions.begin(), transactions.end()) ==
              std::vector{
                  bank::transaction{nullptr, 100, "Initial deposit for Alice"},
                  bank::transaction{&bob, -40, "Test transfer"}});
        CHECK(balance_xts == 60);
    });
    bob.snapshot_transactions([&](auto &transactions, int balance_xts) {
        CHECK(std::vector(transactions.begin(), transactions.end()) ==
              std::vector{
                  bank::transaction{nullptr, 100, "Initial deposit for Bob"},
                  bank::transaction{&alice, 40, "Test transfer"}});
        CHECK(balance_xts == 140);
    });
}

#ifdef TEST_USER_TRANSACTION_ITERATOR
TEST_CASE("Iterators are copy/move assignable/constructible") {
    CHECK(std::is_copy_constructible_v<bank::user_transactions_iterator>);
    CHECK(std::is_copy_assignable_v<bank::user_transactions_iterator>);
    CHECK(std::is_move_constructible_v<bank::user_transactions_iterator>);
    CHECK(std::is_move_assignable_v<bank::user_transactions_iterator>);
}

TEST_CASE("Transfer works with iterators, iterators are copyable") {
    bank::ledger l;
    bank::transaction alice_initial{nullptr, 100, "Initial deposit for Alice"};
    bank::transaction bob_initial{nullptr, 100, "Initial deposit for Bob"};

    bank::user &alice = l.get_or_create_user("Alice");
    bank::user_transactions_iterator alice_it1 = alice.monitor();
    bank::user_transactions_iterator alice_it2 =
        alice.snapshot_transactions([&](auto &ts, int balance_xts) {
            CHECK(balance_xts == 100);
            CHECK(std::vector(ts.begin(), ts.end()) ==
                  std::vector{alice_initial});
        });

    bank::user &bob = l.get_or_create_user("Bob");
    bank::user_transactions_iterator bob_it1 = bob.monitor();
    bank::user_transactions_iterator bob_it2 =
        bob.snapshot_transactions([&](auto &ts, int balance_xts) {
            CHECK(balance_xts == 100);
            CHECK(std::vector(ts.begin(), ts.end()) ==
                  std::vector{bob_initial});
        });

    alice.transfer(bob, 40, "Test transfer from Alice");
    bank::transaction alice_transfer1{&bob, -40, "Test transfer from Alice"};
    bank::transaction bob_transfer1{&alice, 40, "Test transfer from Alice"};

    bank::user_transactions_iterator alice_it3 = alice_it1;
    bank::user_transactions_iterator bob_it3 = bob_it1;

    bank::user_transactions_iterator alice_it4 =
        alice.snapshot_transactions([&](auto &ts, int balance_xts) {
            CHECK(balance_xts == 60);
            CHECK(std::vector(ts.begin(), ts.end()) ==
                  std::vector{alice_initial, alice_transfer1});
        });

    bank::user_transactions_iterator bob_it4 =
        bob.snapshot_transactions([&](auto &ts, int balance_xts) {
            CHECK(balance_xts == 140);
            CHECK(std::vector(ts.begin(), ts.end()) ==
                  std::vector{bob_initial, bob_transfer1});
        });

    bob.transfer(alice, 20, "Test transfer from Bob");
    bank::transaction alice_transfer2{&bob, 20, "Test transfer from Bob"};
    bank::transaction bob_transfer2{&alice, -20, "Test transfer from Bob"};

    CHECK(alice_it1.wait_next_transaction() == alice_transfer1);
    CHECK(bob_it1.wait_next_transaction() == bob_transfer1);
    CHECK(alice_it1.wait_next_transaction() == alice_transfer2);
    CHECK(bob_it1.wait_next_transaction() == bob_transfer2);

    CHECK(alice_it2.wait_next_transaction() == alice_transfer1);
    CHECK(bob_it2.wait_next_transaction() == bob_transfer1);
    CHECK(alice_it2.wait_next_transaction() == alice_transfer2);
    CHECK(bob_it2.wait_next_transaction() == bob_transfer2);

    CHECK(alice_it3.wait_next_transaction() == alice_transfer1);
    CHECK(bob_it3.wait_next_transaction() == bob_transfer1);
    CHECK(alice_it3.wait_next_transaction() == alice_transfer2);
    CHECK(bob_it3.wait_next_transaction() == bob_transfer2);

    CHECK(alice_it4.wait_next_transaction() == alice_transfer2);
    CHECK(bob_it4.wait_next_transaction() == bob_transfer2);
}
#endif

TEST_CASE("Not enough funds error") {
    bank::ledger l;
    bank::user &alice = l.get_or_create_user("Alice");
    bank::user &bob = l.get_or_create_user("Bob");

    CHECK_THROWS_AS_MESSAGE(
        alice.transfer(bob, 101, "Test transfer"), bank::not_enough_funds_error,
        "Not enough funds: 100 XTS available, 101 XTS requested");

    CHECK(std::is_convertible_v<bank::not_enough_funds_error &,
                                bank::transfer_error &>);

    CHECK(alice.balance_xts() == 100);
    CHECK(bob.balance_xts() == 100);

    alice.snapshot_transactions([](const auto &ts, int balance_xts) {
        CHECK(ts.size() == 1);
        CHECK(balance_xts == 100);
    });
    bob.snapshot_transactions([](const auto &ts, int balance_xts) {
        CHECK(ts.size() == 1);
        CHECK(balance_xts == 100);
    });
}

namespace {
class latch {
    std::mutex m;
    int counter;
    std::condition_variable counter_changed;

public:
    explicit latch(int counter_) : counter(counter_) {
    }
    void arrive_and_wait() {
        std::unique_lock l(m);
        counter--;
        counter_changed.notify_all();
        counter_changed.wait(l, [&]() { return counter <= 0; });
    }
};
}  // namespace

// Multithreaded tests has to work under MinGW as well, including debugging
// mode. Unfortunately, MinGW's support for thread_local is currently broken and
// attaching debugger makes it immediately apparent by crash.
// See https://github.com/onqtam/doctest/issues/501
// Hence, we avoid calling any doctest routines in other threads.

TEST_CASE("Lots of users") {
#if !defined(SMALL_TESTS) && !defined(LESS_USERS_TEST)
    const int STEPS = 10;
    const int OPERATIONS_PER_STEP = 10'000;
#else
    const int STEPS = 3;
    const int OPERATIONS_PER_STEP = 1'000;
#endif
    for (int step = 0; step < STEPS; step++) {
        INFO("Step " << (step + 1) << "/" << STEPS);
        bank::ledger l;
        latch latch(3);
        std::set<int> t1a_balances_xts;
        std::thread t1a([&]() {
            latch.arrive_and_wait();
            for (int op = 0; op < OPERATIONS_PER_STEP; op++) {
                const bank::user &u =
                    l.get_or_create_user(std::to_string(op) + "-t1");
                t1a_balances_xts.insert(u.balance_xts());
            }
        });
        std::set<int> t1b_balances_xts;
        std::thread t1b([&]() {
            latch.arrive_and_wait();
            for (int op = 0; op < OPERATIONS_PER_STEP; op++) {
                const bank::user &u =
                    l.get_or_create_user(std::to_string(op) + "-t1");
                t1b_balances_xts.insert(u.balance_xts());
            }
        });

        latch.arrive_and_wait();
        for (int op = 0; op < OPERATIONS_PER_STEP; op++) {
            const bank::user &u =
                l.get_or_create_user(std::to_string(op) + "-t2");
            REQUIRE(u.balance_xts() == 100);
        }

        t1b.join();
        CHECK(t1b_balances_xts == std::set{100});
        t1a.join();
        CHECK(t1a_balances_xts == std::set{100});
    };
}

TEST_CASE("Single producer, single consumer") {
#ifndef SMALL_TESTS
    const int STEPS = 10;
    const int OPERATIONS_PER_STEP = 10'000;
#else
    const int STEPS = 3;
    const int OPERATIONS_PER_STEP = 1'000;
#endif
    for (int step = 0; step < STEPS; step++) {
        INFO("Step " << (step + 1) << "/" << STEPS);
        bank::ledger l;
        latch latch(2);
        std::thread producer([&]() {
            bank::user &alice = l.get_or_create_user("Alice");
            bank::user &bob = l.get_or_create_user("Bob");
            latch.arrive_and_wait();
            for (int op = 0; op < OPERATIONS_PER_STEP; op++) {
                alice.transfer(bob, 10, "A2B");
                bob.transfer(alice, 10, "B2A");
            }
        });

        const bank::user &alice = l.get_or_create_user("Alice");
        const bank::user &bob = l.get_or_create_user("Bob");
#ifdef TEST_USER_TRANSACTION_ITERATOR
        std::array its{
            std::pair(alice.monitor(), bob.monitor()),
            std::pair(std::as_const(alice).monitor(),
                      std::as_const(bob).monitor()),
        };
#endif
        latch.arrive_and_wait();
        for (int op = 0; op < OPERATIONS_PER_STEP; op++) {
            INFO("Operation " << (op + 1) << "/" << OPERATIONS_PER_STEP);
            int alice_balance_xts = alice.balance_xts();
            if (!(alice_balance_xts == 90 || alice_balance_xts == 100)) {
                FAIL("Invalid Alice's balance: " << alice_balance_xts);
            }

            int bob_balance_xts = bob.balance_xts();
            if (!(bob_balance_xts == 100 || bob_balance_xts == 110)) {
                FAIL("Invalid Bob's balance: " << alice_balance_xts);
            }

#ifdef TEST_USER_TRANSACTION_ITERATOR
            for (auto &[alice_it, bob_it] : its) {
                REQUIRE(alice_it.wait_next_transaction() ==
                        bank::transaction{&bob, -10, "A2B"});
                REQUIRE(bob_it.wait_next_transaction() ==
                        bank::transaction{&alice, 10, "A2B"});

                // Try in a different order.
                REQUIRE(bob_it.wait_next_transaction() ==
                        bank::transaction{&alice, -10, "B2A"});
                REQUIRE(alice_it.wait_next_transaction() ==
                        bank::transaction{&bob, 10, "B2A"});
            }
#endif
        }
        producer.join();

        CHECK(alice.balance_xts() == 100);
        CHECK(bob.balance_xts() == 100);
        alice.snapshot_transactions([&](const auto &ts, int balance_xts) {
            CHECK(ts.size() == 1 + 2 * OPERATIONS_PER_STEP);
            CHECK(balance_xts == 100);
        });
        bob.snapshot_transactions([&](const auto &ts, int balance_xts) {
            CHECK(ts.size() == 1 + 2 * OPERATIONS_PER_STEP);
            CHECK(balance_xts == 100);
        });
    };
}

TEST_CASE("Multiple producers, no consumers") {
#ifndef SMALL_TESTS
    const int STEPS = 10;
    const int OPERATIONS_PER_STEP = 10'000;
#else
    const int STEPS = 3;
    const int OPERATIONS_PER_STEP = 1'000;
#endif
    for (int step = 0; step < STEPS; step++) {
        INFO("Step " << (step + 1) << "/" << STEPS);
        bank::ledger l;
        latch latch(2);
        std::thread t1([&]() {
            latch.arrive_and_wait();
            bank::user &alice = l.get_or_create_user("Alice");
            bank::user &bob = l.get_or_create_user("Bob");
            for (int op = 0; op < OPERATIONS_PER_STEP; op++) {
                alice.transfer(bob, 10, "A2B-1");
                bob.transfer(alice, 10, "B2A-1");
            }
        });

        latch.arrive_and_wait();
        {
            bank::user &alice = l.get_or_create_user("Alice");
            bank::user &bob = l.get_or_create_user("Bob");
            for (int op = 0; op < OPERATIONS_PER_STEP; op++) {
                bob.transfer(alice, 10, "B2A-2");
                alice.transfer(bob, 10, "A2B-2");
            }
        }
        t1.join();

        const bank::user &alice = l.get_or_create_user("Alice");
        const bank::user &bob = l.get_or_create_user("Bob");
        CHECK(alice.balance_xts() == 100);
        CHECK(bob.balance_xts() == 100);
        alice.snapshot_transactions([&](const auto &ts, int balance_xts) {
            CHECK(ts.size() == 1 + 4 * OPERATIONS_PER_STEP);
            CHECK(balance_xts == 100);
        });
        bob.snapshot_transactions([&](const auto &ts, int balance_xts) {
            CHECK(ts.size() == 1 + 4 * OPERATIONS_PER_STEP);
            CHECK(balance_xts == 100);
        });
    };
}

TEST_CASE("Single producer, snapshot_transaction is atomic") {
#ifndef SMALL_TESTS
    const int STEPS = 3;
    const int OPERATIONS_PER_STEP = 1000;
#else
    const int STEPS = 3;
    const int OPERATIONS_PER_STEP = 100;
#endif
    for (int step = 0; step < STEPS; step++) {
        INFO("Step " << (step + 1) << "/" << STEPS);
        bank::ledger l;
        latch latch(2);
        std::thread producer([&]() {
            latch.arrive_and_wait();
            bank::user &alice = l.get_or_create_user("Alice");
            bank::user &bob = l.get_or_create_user("Bob");
            for (int op = 0; op < OPERATIONS_PER_STEP; op++) {
                alice.transfer(bob, 10, "A2B");
                bob.transfer(alice, 10, "B2A");
            }
        });

        latch.arrive_and_wait();
        const bank::user &alice = l.get_or_create_user("Alice");
        const bank::user &bob = l.get_or_create_user("Bob");
        for (int op = 0; op < OPERATIONS_PER_STEP; op++) {
            INFO("Operation " << (op + 1) << "/" << OPERATIONS_PER_STEP);
            int alice_balance_xts = alice.balance_xts();
            if (!(alice_balance_xts == 90 || alice_balance_xts == 100)) {
                FAIL("Invalid Alice's balance: " << alice_balance_xts);
            }

            alice.snapshot_transactions([&](auto &ts, int balance_xts) {
                std::vector v1(ts.begin(), ts.end());
                std::vector v2(ts.begin(), ts.end());
                REQUIRE(v1 == v2);
                int real_balance_xts = 0;
                for (const auto &t : v1) {
                    real_balance_xts += t.balance_delta_xts;
                    REQUIRE(real_balance_xts >= 0);
                }
                REQUIRE(balance_xts == real_balance_xts);
            });

            int bob_balance_xts = bob.balance_xts();
            if (!(bob_balance_xts == 100 || bob_balance_xts == 110)) {
                FAIL("Invalid Bob's balance: " << alice_balance_xts);
            }

            bob.snapshot_transactions([&](auto &ts, int balance_xts) {
                std::vector v1(ts.begin(), ts.end());
                std::vector v2(ts.begin(), ts.end());
                REQUIRE(v1 == v2);
                int real_balance_xts = 0;
                for (const auto &t : v1) {
                    real_balance_xts += t.balance_delta_xts;
                    REQUIRE(real_balance_xts >= 0);
                }
                REQUIRE(balance_xts == real_balance_xts);
            });
        }
        producer.join();

        CHECK(alice.balance_xts() == 100);
        CHECK(bob.balance_xts() == 100);
        alice.snapshot_transactions([&](const auto &ts, int balance_xts) {
            CHECK(ts.size() == 1 + 2 * OPERATIONS_PER_STEP);
            CHECK(balance_xts == 100);
        });
        bob.snapshot_transactions([&](const auto &ts, int balance_xts) {
            CHECK(ts.size() == 1 + 2 * OPERATIONS_PER_STEP);
            CHECK(balance_xts == 100);
        });
    };
}
