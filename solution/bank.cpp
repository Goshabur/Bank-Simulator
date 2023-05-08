#include "bank.h"

bank::transaction::transaction(const bank::user *counterparty,
                               int balance_delta_xts,
                               std::string comment)
    : counterparty(counterparty),
      balance_delta_xts(balance_delta_xts),
      comment(std::move(comment)) {
}

bank::user::user(std::string name) : m_name(std::move(name)) {
}

const std::string &bank::user::name() const noexcept {
    return m_name;
}

int bank::user::balance_xts() const {
    std::unique_lock l{m_mutex};
    return m_balance_xts;
}

bank::user_transactions_iterator bank::user::snapshot_transactions(
    const std::function<void(const std::vector<transaction> &, int)> &callback)
    const {
    std::unique_lock l{m_mutex};
    callback(m_transactions, m_balance_xts);
    return user_transactions_iterator{this};
}

bank::user_transactions_iterator bank::user::monitor() const {
    std::unique_lock l{m_mutex};
    return user_transactions_iterator{this};
}

void bank::user::transfer(bank::user &counterparty,
                          int amount_xts,
                          const std::string &comment) {
    // ban self-transfer
    if (this == &counterparty) {
        throw self_transfer_error();
    }
    // must send some funds
    if (amount_xts <= 0) {
        throw nonpositive_amount_transfer_error();
    }
    // lock self and counterparty
    std::scoped_lock l{m_mutex, counterparty.m_mutex};
    // must have enough funds
    if (m_balance_xts < amount_xts) {
        throw not_enough_funds_error(m_balance_xts, amount_xts);
    }
    // perform transfer
    add_funds_unchecked(counterparty, -amount_xts, comment);
    counterparty.add_funds_unchecked(*this, amount_xts, comment);
}

/// Adds funds for given transfer to current user.
/// Performs no checks, must be called with `m_mutex` locked.
void bank::user::add_funds_unchecked(bank::user &counterparty,
                                     int balance_delta_xts,
                                     const std::string &comment) {
    m_balance_xts += balance_delta_xts;
    // add transaction
    m_transactions.emplace_back(&counterparty, balance_delta_xts, comment);
    // notify iterators
    m_cond.notify_all();
}

bank::transaction bank::user_transactions_iterator::wait_next_transaction() {
    std::unique_lock l{m_user->m_mutex};
    // wait until new element is available
    while (pos >= m_user->m_transactions.size()) {
        m_user->m_cond.wait(l);
    }
    // return new element
    return m_user->m_transactions[pos++];
}

/// This private constructor must be called with `user->m_mutex` locked
bank::user_transactions_iterator::user_transactions_iterator(
    const bank::user *user)
    : m_user(user), pos(user->m_transactions.size()) {
}

bank::user &bank::ledger::get_or_create_user(const std::string &name) & {
    std::unique_lock l{m};
    // find given user
    auto it = users.find(name);
    if (it == users.end()) {
        // if doesn't exist, initialize
        auto [new_it, _] = users.emplace(name, std::make_unique<user>(name));
        return *new_it->second;
    }
    return *it->second;
}

bank::not_enough_funds_error::not_enough_funds_error(int available_xts,
                                                     int requested_xts)
    : transfer_error("Not enough funds: " + std::to_string(available_xts) +
                     " XTS available, " + std::to_string(requested_xts) +
                     " XTS requested") {
}

bank::self_transfer_error::self_transfer_error()
    : transfer_error("Transfer to yourself") {
}

bank::nonpositive_amount_transfer_error::nonpositive_amount_transfer_error()
    : transfer_error("Transfer of non-positive amount") {
}
