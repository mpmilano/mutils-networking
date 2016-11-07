#pragma once
#include <sys/epoll.h>
#include <vector>
#include <unordered_map>
#include <iostream>

//purpose: wrap epoll logic in a class for easier use.
//the epoll logic contained herein is from https://banu.com/blog/2/how-to-use-epoll-a-complete-example-in-c/

namespace mutils{

struct EPoll{
	struct epoll_event event{0,epoll_data_t{.u64 = 0}};
	int max_events{0};
	std::vector<struct epoll_event> returned_events;
	int epoll_fd{epoll_create1(0)};

	struct epoll_action{
		template<typename FDType>
		static void delete_epoll_obj_specific(void *v){
			delete ((FDType*) v);
		}
		using delete_fp_t = void (*) (void*);
		
		void * epoll_obj;
		const delete_fp_t delete_epoll_obj;
		const std::function<void (void*)> _action;
		void action();

		template<typename FDType>
		epoll_action(std::unique_ptr<FDType> resource, std::function<void (FDType&)> action)
			:epoll_obj(resource.release()),
			 delete_epoll_obj(delete_epoll_obj_specific<FDType>),
			 _action([action](void* epoll_obj) {if (epoll_obj) return action(*((FDType*) epoll_obj));}) {}

		epoll_action(const epoll_action&) = delete;
		epoll_action(epoll_action&& o);
		
		~epoll_action();

		template<typename FDType>
		std::unique_ptr<FDType> release(){
			std::unique_ptr<FDType> ret{(FDType*)epoll_obj};
			epoll_obj = 0;
			return ret;
		}
	};
	
	std::unordered_map<int, epoll_action> fd_lookup;

	EPoll();

	void wait();

	template<typename FDType>
	FDType& add(std::unique_ptr<FDType> new_fd, std::function<void (FDType&)> action){
		using namespace std;
		auto infd = new_fd->underlying_fd();
		event.data.fd = infd;
		event.events = EPOLLIN;
		#ifndef NDEBUG
		auto retcode = 
		#endif
			epoll_ctl (epoll_fd, EPOLL_CTL_ADD, infd, &event);
		#ifndef NDEBUG
		if (retcode == -1){
			std::cerr << std::strerror(errno) << std::endl;
			std::cerr << infd << std::endl;
			std::cerr << typeid(*new_fd).name() << std::endl;
		}
		assert(retcode == 0);
		#endif
		++max_events;
		returned_events.emplace_back();
		return *( (FDType*) fd_lookup.emplace(infd,epoll_action{std::move(new_fd),action}).first->second.epoll_obj);
	}

	template<typename FDType>
	std::unique_ptr<FDType> remove(const FDType& fd){
		int raw_fd = fd.underlying_fd();
		event.data.fd = raw_fd;
		event.events = EPOLLIN;
		auto ret = std::move(fd_lookup.at(raw_fd));
		epoll_ctl (epoll_fd, EPOLL_CTL_DEL, raw_fd, &event);
		fd_lookup.erase(raw_fd);
		return std::unique_ptr<FDType>{ret.template release<FDType>()};
	}

	EPoll(const EPoll&) = delete;

	~EPoll();
	
};
}
