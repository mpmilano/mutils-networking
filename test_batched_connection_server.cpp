#include "batched_connection.hpp"
#include "simple_rpc.hpp"
#include "GlobalPool.hpp"
#include <unistd.h>

using namespace mutils;
using namespace std;
namespace conn_space = mutils::batched_connection;

int main(int argc, char* argv[]){
	const auto portno = (argc > 1 ? std::atoi(argv[1]) : 9876);
	std::cout << "portno is: " << portno << std::endl;
	using action_t = typename conn_space::receiver::action_t;
	conn_space::receiver{portno,[](whendebug(std::ofstream&)){
				//std::cout << "receiver triggered" << std::endl;
				struct ReceiverFun : public conn_space::ReceiverFun {
					bool on_first_message{true};
#ifndef NDEBUG
					std::mutex assert_single_threaded;
#endif
					void operator()(const void* inbnd, connection& c) {
#ifndef NDEBUG
						bool success = assert_single_threaded.try_lock();
						assert(success);
						AtScopeEnd ase{[&]{assert_single_threaded.unlock();}};
#endif
						//std::cout << "received message" << std::endl;
						if (on_first_message){
							//std::cout << "expected this message is size_t" << std::endl;
							int rcv = *((int*)(inbnd));
							c.send(rcv);
							on_first_message = false;
						}
						else{
							//std::cout << "expected this message is vector" << std::endl;
							auto received = from_bytes<std::vector<unsigned char> >(nullptr,((char*) inbnd));
							{
								stringstream ss;
								ss << "received " << received->size() << " element vector" << std::endl;
								//std::cout << ss.str();
							}
							auto random = better_rand();
							assert(random >= 0 && random <= 1);
							uint index = random * received->size();
							if (index == received->size()) index = 0;
							if (received->size() != 0){
								assert(index < received->size());
								(*received)[index]++;
							}
							{
								stringstream ss;
								ss << "sending " << c.send(*received) << " bytes of vector back" << std::endl;
								//std::cout << ss.str();
							}
							on_first_message = true;
						}
					}
				};
				return action_t{new ReceiverFun{}};
			}}.loop_until_false_set();
}
