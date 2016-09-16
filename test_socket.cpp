#include "Socket.hpp"
#include "ServerSocket.hpp"
#include <thread>

using namespace mutils;

int main(){
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
				{
					long recv{0};
					static_assert(sizeof(long) == 2*sizeof(int),"");
					int new_recv{0};
					s.receive(recv);
					s.receive(new_recv);
					assert(new_recv == 235);
				}
			}};
		Socket s = Socket::connect(decode_ip("127.0.0.1"),port);
		s.send(port);
		s.send(port,port);
		const int new_send = 235;
		s.send(port,port,new_send);
		t.join();
	}
}
