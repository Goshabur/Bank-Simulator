#!/usr/bin/env python3
import atexit
import pathlib
import socket
import subprocess
import sys
import tempfile
import time


def check_eq(expected, actual):
    if expected != actual:
        print(f'Check failed: expected {repr(expected)}, got {repr(actual)}')
        sys.exit(1)


def main():
    _, *server_cmd = sys.argv
    assert server_cmd, 'Expected usage: ./run-test-server.py <command-to-run>'

    pfile = tempfile.NamedTemporaryFile(delete=False)
    pfile.close()

    print(f'Starting the server and waiting for the port file {pfile.name}...', flush=True)
    server = subprocess.Popen(args=[*server_cmd, '0', pfile.name])
    def kill_server():
        try:
            server.wait(timeout=0.1)
        except subprocess.TimeoutExpired:
            server.kill()
    atexit.register(kill_server)

    while not (port_str := pathlib.Path(pfile.name).read_text()):
        time.sleep(0.1)
        assert not server.poll(), 'Server has unexpectedly terminated'
    port = int(port_str)
    print(f'    ok, port is {port}')
    print('Trying to open two connections...', flush=True)

    with socket.socket() as sock_alice, socket.socket() as sock_bob:
        sock_alice.connect(('localhost', port))
        sock_bob.connect(('localhost', port))

        f_alice = sock_alice.makefile(mode='rw')
        f_bob = sock_bob.makefile(mode='rw')

        check_eq('What is your name?\n', f_alice.readline())
        check_eq('What is your name?\n', f_bob.readline())

        print('    ok')

        print(f'Authenticating as Alice at {sock_alice.getsockname()} and Bob at {sock_bob.getsockname()}...', flush=True)
        f_alice.write('Alice\n')
        f_alice.flush()
        f_bob.write('Bob\n')
        f_bob.flush()
        check_eq('Hi Alice\n', f_alice.readline())
        check_eq('Hi Bob\n', f_bob.readline())
        print('    ok')

        print('Transfer from Alice to Bob...', flush=True)
        f_alice.write('transfer Bob 90 This is a comment\n')
        f_alice.flush()
        check_eq('OK\n', f_alice.readline())
        print('    ok')

        print('Invalid transfer from Alice to Bob...', flush=True)
        f_alice.write('transfer Bob 90 This is an another comment\n')
        f_alice.flush()
        check_eq('Not enough funds: 10 XTS available, 90 XTS requested\n', f_alice.readline())
        print('    ok')

        print('Checking balance in several packets...', flush=True)
        f_alice.write('balance\n')
        f_alice.flush()
        f_bob.write('bala')
        f_bob.flush()
        f_bob.write('nce\n')
        f_bob.flush()
        check_eq('10\n', f_alice.readline())
        check_eq('190\n', f_bob.readline())
        print('    ok')

        print('Checking last transactions...', flush=True)
        f_alice.write('transactions 10\n')
        f_alice.flush()
        check_eq('CPTY\tBAL\tCOMM\n', f_alice.readline())
        check_eq('-\t100\tInitial deposit for Alice\n', f_alice.readline())
        check_eq('Bob\t-90\tThis is a comment\n', f_alice.readline())
        check_eq('===== BALANCE: 10 XTS =====\n', f_alice.readline())
        print('    ok')

        print('Checking some of last transactions...', flush=True)
        f_alice.write('transactions 1\n')
        f_alice.flush()
        check_eq('CPTY\tBAL\tCOMM\n', f_alice.readline())
        check_eq('Bob\t-90\tThis is a comment\n', f_alice.readline())
        check_eq('===== BALANCE: 10 XTS =====\n', f_alice.readline())
        print('    ok')

        print('Starting monitoring...', flush=True)
        f_alice.write('monitor 1\n')
        f_alice.flush()
        check_eq('CPTY\tBAL\tCOMM\n', f_alice.readline())
        check_eq('Bob\t-90\tThis is a comment\n', f_alice.readline())
        check_eq('===== BALANCE: 10 XTS =====\n', f_alice.readline())
        print('    ok')

        print('Doing another transfer and waiting for monitoring to display it...', flush=True)
        f_bob.write('transfer Alice 50  Another comment\n')
        f_bob.flush()
        check_eq('OK\n', f_bob.readline())
        check_eq('Bob\t50\t Another comment\n', f_alice.readline())
        print('    ok')

        print('Issuing invalid command in several packets...', flush=True)
        f_bob.write('wt')
        f_bob.flush()
        f_bob.write('f\n')
        f_bob.flush()
        check_eq("Unknown command: 'wtf'\n", f_bob.readline())
        print('    ok')
    print('All ok.')


if __name__ == '__main__':
    main()
