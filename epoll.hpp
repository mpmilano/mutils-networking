#pragma once
#include <sys/epoll.h>
#include <vector>
#include <unordered_map>

//purpose: wrap epoll logic in a class for easier use.
//the epoll logic contained herein is from https://banu.com/blog/2/how-to-use-epoll-a-complete-example-in-c/

template<typename FDType>
struct EPoll{
	struct epoll_event event;
	int max_events{0};
	std::vector<struct epoll_event> returned_events;
	int epoll_fd{epoll_create1(0)};
	std::unordered_map<int, FDType> fd_lookup;

	EPoll(){
		assert(epoll_fd > 0);
	}

	//wait() will give you ownership over the file
	//descriptors that EPoll covers.
	//YOU MUST EITHER CLOSE OR RETURN THESE.
	auto wait(){
		using namespace std;
		std::list<FDType> ret;
		auto n = epoll_wait(epoll_fd, returned_events.data(),max_events,-1);
		for (int i = 0; i < n; ++i){
			if ((returned_events.at(i).events & EPOLLERR) ||
				(returned_events.at(i).events & EPOLLHUP) ||
				(!(returned_events.at(i).events & EPOLLIN)))
			{
				/* An error has occured on this fd, or the socket is not
				   ready for reading (why were we notified then?) */
				std::cerr <<  "epoll error\n" << std::endl;
				//this should have the side effect of closing this resource
				fd_lookup.erase(returned_events.at(i).data.fd);
				continue;
			}
			else {
				ret.emplace_back(
					std::move(
						fd_lookup.at(returned_events.at(i).data.fd)
						)
					);
			}
		}
		return ret;
	}

	FDType& add(FDType new_fd){
		using namespace std;
		auto infd = new_fd.underlying_fd();
		event.data.fd = infd;
		event.events = EPOLLIN;
		#ifndef NDEBUG
		auto retcode = 
		#endif
			epoll_ctl (epoll_fd, EPOLL_CTL_ADD, infd, &event);
		assert(retcode == 0);
		++max_events;
		returned_events.emplace_back();
		return fd_lookup.emplace(infd,std::move(new_fd)).first->second;
	}

	/*
	  if you got an FDType from me, please return it here.
	 */
	FDType& return_ownership(FDType returned_fd){
		return fd_lookup.insert_or_assign(returned_fd.underlying_fd(),
								 std::move(returned_fd)).first->second;
	}

	EPoll(const EPoll&) = delete;

	~EPoll(){
		close(epoll_fd);
	}
	
};
