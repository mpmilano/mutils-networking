#include "epoll.hpp"
#include <cassert>
#include <iostream>
#include <unistd.h>

namespace mutils{
	
	void EPoll::epoll_action::action() {
		_action(epoll_obj);
	}
	
	EPoll::epoll_action::epoll_action(epoll_action&& o)
		:epoll_obj(o.epoll_obj),
		 delete_epoll_obj(o.delete_epoll_obj),
		 _action(std::move(o._action)){
		o.epoll_obj = nullptr;
	}
	
	EPoll::epoll_action::~epoll_action(){
		if (epoll_obj) delete_epoll_obj(epoll_obj);
	}
	
	EPoll::EPoll(){
		assert(epoll_fd > 0);
	}

	void EPoll::wait(){
		using namespace std;
		auto n = epoll_wait(epoll_fd, returned_events.data(),max_events,-1);
		for (int i = 0; i < n; ++i){
			if ((returned_events.at(i).events & EPOLLERR) ||
				(returned_events.at(i).events & EPOLLHUP) ||
				(!(returned_events.at(i).events & EPOLLIN)))
			{
				/* An error has occured on this fd, or the socket is not
				   ready for reading (why were we notified then?) */
#ifndef NDEBUG
				std::cerr <<  "epoll error\n" << std::endl;
#endif
				//this should have the side effect of closing this resource
				fd_lookup.erase(returned_events.at(i).data.fd);
				continue;
			}
			else {
				try{
					fd_lookup.at(returned_events.at(i).data.fd).action();
				}
				catch(...){
					auto key = returned_events.at(i).data.fd;
					epoll_action returned = std::move(fd_lookup.at(key));
					fd_lookup.erase(key);
					throw epoll_removed_exception{
						std::current_exception(),
							std::move(returned)};
				}
			}
		}
	}

	EPoll::~EPoll(){
		close(epoll_fd);
	}
}
