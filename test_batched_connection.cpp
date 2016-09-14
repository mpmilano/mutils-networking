#include "batched_connection.hpp"
#include "GlobalPool.hpp"
#include <unistd.h>

using namespace mutils;
using namespace std;

int main(int, char* argv[]){

	{
		constexpr int port = 9845;
		int recv_port{0};
		ServerSocket ss{port};
		std::thread t{[&]{
				auto s = ss.receive();
				s.receive(recv_port);
				assert(recv_port == port);
				int recv{0};
				s.receive(recv);
				assert(recv == recv_port);
				recv = 0;
				s.receive(recv);
				assert(recv == recv_port);
			}};
		Socket s = Socket::connect(decode_ip("127.0.0.1"),port);
		s.send(port);
		s.send(port,port);
		t.join();
	}
	
	const auto portno = std::atoi(argv[1]);
	std::cout << "portno is: " << portno << std::endl;
	using action_t = typename batched_connection::receiver::action_t;
	std::thread receiver{[&]{
			batched_connection::receiver{portno,[]{
					return std::pair<action_t,std::vector<std::size_t> >{
						[](void** inbnd, connection& c) -> std::vector<std::size_t>{
							int rcv = *((int*)(inbnd[0]));
							c.send(rcv);
							return {sizeof(int)};
						}, {sizeof(int)}};
				}}.loop_until_false_set();
		}};
	sleep(1);
	batched_connection::batched_connections bc(decode_ip("127.0.0.1"),portno,100);
	using connection = batched_connection::locked_connection;

	std::function<void (const connection&) > looper;
	std::map<int, connection> connections;
	struct debug_info{
		int my_msg;
		int receipt;
	};
	try {
		for(int index = 0; index < MAX_THREADS/2; ++index){
			auto my_msg = index;
			if (my_msg %50 == 0) std::cout << "on message " << my_msg << std::endl;
			connections.emplace(my_msg,bc.spawn());
			auto &c = connections.at(my_msg);
			GlobalPool::inst.push([&c,my_msg](int){
					for(int i = 0; true; ++i){
						if (i % 100 == 0) std::cout << "connection " << my_msg << " on round " << i << std::endl;
						c->send(my_msg);
						int receipt{-1};
						c->receive(receipt);
						assert(receipt == my_msg);
						if (receipt != my_msg)
							throw debug_info{my_msg,receipt};
						
					}
				});
		}
	}
	catch (const debug_info& exn){
		std::cerr << "message error: " << exn.my_msg << " : " << exn.receipt << std::endl;
	}
	receiver.join();
	sleep(5000);
}
