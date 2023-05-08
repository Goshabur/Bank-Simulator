#include <boost/asio.hpp>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <thread>
#include "bank.h"

using boost::asio::ip::tcp;

namespace {
std::ostream &operator<<(std::ostream &in, const bank::transaction &tr) {
    return in << (tr.counterparty == nullptr ? "-" : tr.counterparty->name())
              << '\t' << tr.balance_delta_xts << '\t' << tr.comment;
}
}  // namespace

void handle_socket(tcp::socket s, bank::ledger &l) {
    tcp::iostream client(std::move(s));

    // initialize user
    client << "What is your name?\n";
    std::string name;
    if (!(client >> name)) {
        return;
    }
    bank::user &user = l.get_or_create_user(name);
    client << "Hi " << name << '\n';

    // helper function
    auto get_transactions = [&]() {
        // read number of displayed transactions
        int display_transactions = 0;
        if (!(client >> display_transactions)) {
            // if failed return empty
            return std::optional<bank::user_transactions_iterator>();
        }
        // setup transactions and balance for copying
        std::vector<bank::transaction> transactions_snapshot;
        int balance_snapshot = 0;
        // snapshot transactions and balance
        auto res_it = user.snapshot_transactions(
            [&](const auto &transactions, int balance_xts) {
                transactions_snapshot =
                    std::vector<bank::transaction>(transactions);
                balance_snapshot = balance_xts;
            });
        // display transactions
        display_transactions =
            std::min(display_transactions,
                     static_cast<int>(transactions_snapshot.size()));
        client << "CPTY\tBAL\tCOMM\n";
        for (auto it = transactions_snapshot.end() - display_transactions;
             it != transactions_snapshot.end(); it++) {
            client << *it << '\n';
        }
        client << "===== BALANCE: " << balance_snapshot << " XTS =====\n";
        return std::optional<bank::user_transactions_iterator>{res_it};
    };

    // register known commands
    std::map<std::string, std::function<bool()>> known_commands;
    known_commands.emplace("balance", [&]() {
        client << user.balance_xts() << '\n';
        return false;
    });
    known_commands.emplace("transfer", [&]() {
        std::string counterparty_name;
        int amount = 0;
        std::string comment;
        if (!(client >> counterparty_name >> amount)) {
            return true;
        }
        if (!std::getline(client, comment)) {
            return true;
        }
        comment.erase(0, 1);
        try {
            user.transfer(l.get_or_create_user(counterparty_name), amount,
                          comment);
            client << "OK\n";
        } catch (const bank::transfer_error &err) {
            client << err.what() << '\n';
        }
        return false;
    });
    known_commands.emplace("transactions",
                           [&]() { return !get_transactions().has_value(); });
    known_commands.emplace("monitor", [&]() {
        auto trs_it = get_transactions();
        if (!trs_it.has_value()) {
            return true;
        }
        while (client) {
            client << trs_it.value().wait_next_transaction() << '\n';
        }
        return false;
    });

    // interaction cycle
    while (client) {
        std::string command;
        if (!(client >> command)) {
            break;
        }
        if (known_commands.count(command) != 0) {
            bool is_stream_end = known_commands[command]();
            if (is_stream_end) {
                break;
            }
        } else {
            client << "Unknown command: '" << command << "'\n";
        }
    }
}

// NOLINTNEXTLINE(bugprone-exception-escape)
int main(int argc, char *argv[]) {
    if (argc != 3) {
        std::cout << "Expected usage: ./bank-server <port-num> <port-file>"
                  << std::endl;
        return 1;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    int port_num = std::stoi(argv[1]);

    boost::asio::io_context io_context;
    tcp::acceptor acceptor(
        io_context,
        tcp::endpoint(tcp::v4(), static_cast<unsigned short>(port_num)));
    int endpoint_port = acceptor.local_endpoint().port();
    {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        std::ofstream fout(argv[2]);
        if (!fout.is_open()) {
            // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
            std::cout << "Unable to store port to file " << argv[2]
                      << std::endl;
            return 1;
        }
        fout << endpoint_port;
    }

    bank::ledger l;
    auto process_socket = [&l](tcp::socket s) {
        auto remote = s.remote_endpoint();
        auto local = s.local_endpoint();
        std::cout << "Connected " << remote << " --> " << local << std::endl;
        handle_socket(std::move(s), l);
        std::cout << "Disconnected " << remote << " --> " << local << std::endl;
    };

    std::cout << "Listening at " << acceptor.local_endpoint() << std::endl;
    while (true) {
        tcp::socket s = acceptor.accept();
        std::thread(process_socket, std::move(s)).detach();
    }
}
