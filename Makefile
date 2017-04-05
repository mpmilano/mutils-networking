CPPFLAGS= -I$(PWD) -I$(PWD)/../  -I$(PWD)/../mutils-networking -I$(PWD)/../mutils -I$(PWD)/../mutils-containers -I$(PWD)/../mutils-serialization -I$(PWD)/../mutils-tasks -g  --std=c++1z -Wall -DMAX_THREADS=$(MAX_THREADS)  -Wall -Werror -Wextra -DNDEBUG
LDFLAGS=  --std=c++1z -lpq -lm -pthread
object_files=ServerSocket.o SerializationSupport.o utils.o Socket.o batched_connection_server.o batched_connection_client.o batched_connection_common.o  eventfd.o epoll.o dual_connection.o dual_connection_superstate.o GlobalPool.o simple_rpc.o

dc_test: $(object_files)
	clang++ -O3 test_dual_connection.cpp $(object_files) $(CPPFLAGS) $(LDFLAGS) -o dc_test

test_batched_connection: test_batched_connection_client test_batched_connection_server
	echo done

test_batched_connection_server: $(object_files)
	clang++ -o bc_server -O3 $(object_files) test_batched_connection_server.cpp $(CPPFLAGS) $(LDFLAGS)

test_batched_connection_client: $(object_files)
	clang++ -o bc_client -O3 $(object_files) test_batched_connection_client.cpp $(CPPFLAGS) $(LDFLAGS)

test_simple_rpc: test_simple_rpc_client test_simple_rpc_server
	echo done

test_simple_rpc_server: $(object_files)
	clang++ -o srpc_server -O3 $(object_files) test_simple_rpc_server.cpp $(CPPFLAGS) $(LDFLAGS)

test_simple_rpc_client: $(object_files)
	clang++ -o srpc_client -O3 $(object_files) test_simple_rpc_client.cpp $(CPPFLAGS) $(LDFLAGS)


batched_connection_common.o:
	clang++ -c -O3 ../*/batched_connection_common.cpp $(CPPFLAGS)

simple_rpc.o:
	clang++ -c -O3 ../*/simple_rpc.cpp $(CPPFLAGS)

batched_connection_client.o:
	clang++ -c -O3 ../*/batched_connection_client.cpp $(CPPFLAGS)

batched_connection_server.o:
	clang++ -c -O3 ../*/batched_connection_server.cpp $(CPPFLAGS)

utils.o:
	clang++ -c -O3  ../mutils/utils.cpp $(CPPFLAGS)

SerializationSupport.o:
	clang++ -c -O3  ../mutils-serialization/SerializationSupport.cpp $(CPPFLAGS)

epoll.o:
	clang++ -c -O3 epoll.cpp $(CPPFLAGS)

eventfd.o:
	clang++ -c -O3 ../mutils-tasks/eventfd.cpp $(CPPFLAGS)

ServerSocket.o:
	clang++ -c -O3 ../mutils-networking/ServerSocket.cpp $(CPPFLAGS)

Socket.o:
	clang++ -c -O3 ../mutils-networking/Socket.cpp $(CPPFLAGS)

GlobalPool.o:
	clang++ -c -O3  ../mutils-tasks/GlobalPool.cpp $(CPPFLAGS)

dual_connection.o:
	clang++ -c -O3  dual_connection.cpp $(CPPFLAGS)

dual_connection_superstate.o:
	clang++ -c -O3  dual_connection_superstate.cpp $(CPPFLAGS)

clean:
	rm *.o
