#include "batched_connection.hpp"
#include "simple_rpc.hpp"
#include "GlobalPool.hpp"
#include "eventfd.hpp"
#include <unistd.h>
#include <set>

using namespace mutils;
using namespace std;
namespace conn_space = mutils::batched_connection;

int main(int argc, char* argv[]){
	const auto portno = (argc > 1 ? std::atoi(argv[1]) : 9876);
	std::cout << "portno is: " << portno << std::endl;
	using action_t = typename conn_space::receiver::action_t;
#ifndef NDEBUG
	std::set<connection*> used_connections;
#endif
	conn_space::receiver{portno,[&](whendebug(std::ostream&), connection& c){
				//std::cout << "receiver triggered" << std::endl;
				struct ReceiverFun : public conn_space::ReceiverFun {
					bool on_first_message{true};
					eventfd event_fd;
					connection& c;


					using second_t = std::unique_ptr<std::vector<unsigned char> >;
					using pair_t = std::pair<int, second_t>;
					
					pair_t recv{-1,second_t{nullptr}};
#ifndef NDEBUG
					std::set<connection*> &used_connections;
					std::mutex assert_single_threaded;
					ReceiverFun(connection& c, std::set<connection*> &used_connections)
						:c(c),used_connections(used_connections){}
#endif
					void deliver_new_event(std::size_t, const void* inbnd) {
#ifndef NDEBUG
						bool success = assert_single_threaded.try_lock();
						assert(success);
						AtScopeEnd ase{[&]{assert_single_threaded.unlock();}};
#endif
						//std::cout << "received message" << std::endl;
						if (on_first_message){
							//std::cout << "expected this message is size_t" << std::endl;
							recv.first = *((int*)(inbnd));
							on_first_message = false;
							event_fd.notify();
							//async_tick(c);
						}
						else{
							//std::cout << "expected this message is vector" << std::endl;
							recv.second = from_bytes<std::vector<unsigned char> >(nullptr,((char*) inbnd));
							{
								stringstream ss;
								ss << "received " << recv.second->size() << " element vector" << std::endl;
								//std::cout << ss.str();
							}
							on_first_message = true;
							event_fd.notify();
							//async_tick(c);
						}
					}

					void async_tick(){
						event_fd.wait();
						if (recv.first == -1 && recv.second){
							auto &sm_recv = recv.second;
							assert(sm_recv);
							auto random = better_rand();
							assert(random >= 0 && random <= 1);
							uint index = random * sm_recv->size();
							if (index == sm_recv->size()) index = 0;
							if (sm_recv->size() != 0){
								assert(index < sm_recv->size());
								(*sm_recv)[index]++;
							}
							{
								stringstream ss;
								ss << "sending " << c.send(*sm_recv) << " bytes of vector back" << std::endl;
								//std::cout << ss.str();
							}
						}
						else {
							c.send(recv.first);
							recv.first = -1;
						}
					}
					int underlying_fd(){ return event_fd.underlying_fd();}
				};
				return action_t{new ReceiverFun{c whendebug(,used_connections)}};
			}}.acceptor_fun();
}
