build/udp_server 9001

build/network --listen-port 9000 --server-ip 127.0.0.1 --server-port 9001 --loss 0.2 --delay 0.8 --corrupt 0.33 --max-delay-ms 4000 --seed 42

build/udp_client 127.0.0.1 9000