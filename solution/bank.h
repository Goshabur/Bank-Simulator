#ifndef BANK_H_
#define BANK_H_

#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace bank {
struct transfer_error : std::runtime_error {
    using std::runtime_error::runtime_error;
};
struct not_enough_funds_error : transfer_error {
    not_enough_funds_error(int available_xts, int requested_xts);
};
struct self_transfer_error : transfer_error {
    self_transfer_error();
};
struct nonpositive_amount_transfer_error : transfer_error {
    nonpositive_amount_transfer_error();
};
struct user;
struct user_transactions_iterator;
struct transaction {
    transaction(const user *counterparty,
                int balance_delta_xts,
                std::string comment);

    // NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes,misc-non-private-member-variables-in-classes)
    const user *const counterparty;
    // NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes,misc-non-private-member-variables-in-classes)
    const int balance_delta_xts;
    // NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes,misc-non-private-member-variables-in-classes)
    const std::string comment;
};
struct user {
    explicit user(std::string name);
    ~user() = default;
    user(const user &) = delete;
    user(user &&) = delete;
    user operator=(const user &) = delete;
    user operator=(user &&) = delete;

    [[nodiscard]] const std::string &name() const noexcept;
    [[nodiscard]] int balance_xts() const;
    user_transactions_iterator snapshot_transactions(
        const std::function<void(const std::vector<transaction> &, int)>
            &callback) const;

    void transfer(user &counterparty,
                  int amount_xts,
                  const std::string &comment);

    [[nodiscard]] user_transactions_iterator monitor() const;

    friend user_transactions_iterator;

private:
    void add_funds_unchecked(user &counterparty,
                             int balance_delta_xts,
                             const std::string &comment);

    const static int INITIAL_BALANCE = 100;
    const std::string m_name;
    int m_balance_xts = INITIAL_BALANCE;
    std::vector<transaction> m_transactions = {
        {nullptr, INITIAL_BALANCE, "Initial deposit for " + m_name}};
    /// main user mutex guarding balance and transactions
    mutable std::mutex m_mutex;
    /// conditional variable for transactions updates
    mutable std::condition_variable m_cond;
};

struct user_transactions_iterator {
    transaction wait_next_transaction();
    friend user;

private:
    explicit user_transactions_iterator(const user *user);
    const user *m_user;
    size_t pos;
};

struct ledger {
    user &get_or_create_user(const std::string &name) &;

private:
    std::unordered_map<std::string, std::unique_ptr<user>> users;
    /// ledger mutex guarding users map
    std::mutex m;
};
}  // namespace bank
#endif