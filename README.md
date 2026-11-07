build/udp_server 9001

build/network --listen-port 9000 --server-ip 127.0.0.1 --server-port 9001 --loss 0.00 --delay 0.00 --corrupt 0.00 --max-delay-ms 500 --seed 42

build/udp_client 127.0.0.1 9000