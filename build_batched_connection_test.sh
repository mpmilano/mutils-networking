#!/bin/bash

rm bc_client;
rm bc_server;
#CC=g++-6.2.0
CC="clang++ -DNDEBUG"
STDLIB=--stdlib=libc++
$CC -I ../mutils-containers/ -I ../mutils-tasks/ -I ../mutils-serialization/ -I ../mutils batched_connection_common.cpp  batched_connection_client.cpp ../mutils/utils.cpp ../mutils-serialization/SerializationSupport.cpp ../mutils-networking/ServerSocket.cpp ../mutils-networking/Socket.cpp ../mutils-tasks/GlobalPool.cpp test_batched_connection_client.cpp --std=c++1z -DMAX_THREADS=$1 -pthread -g -O3 -Wall -Werror -Wextra -o bc_client $STDLIB &
$CC -I ../mutils-containers/ -I ../mutils-tasks/ -I ../mutils-serialization/ -I ../mutils batched_connection_common.cpp  batched_connection_server.cpp ../mutils/utils.cpp ../mutils-serialization/SerializationSupport.cpp ../mutils-networking/ServerSocket.cpp ../mutils-networking/Socket.cpp test_batched_connection_server.cpp --std=c++1z -DMAX_THREADS=$1 -pthread -g -O3 -Wall -Werror -Wextra -o bc_server $STDLIB &
wait
killall bc_server
killall bc_client
./bc_client &
ID=$!
./bc_server
kill $ID
