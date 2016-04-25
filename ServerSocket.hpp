#pragma once
#include "Socket.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <memory>

namespace mutils{

struct ServerSocket {
	struct Internals{
		const int sockID;
		struct sockaddr_in cli_addr;
		socklen_t clilen{sizeof(cli_addr)};
		Internals(int sockID);
	};
private:
	std::unique_ptr<Internals> i;
public:
	ServerSocket(int listen);
	ServerSocket(const ServerSocket&) = delete;
	Socket receive();
	~ServerSocket();
};

	struct AcceptConnectionLoop {
		std::shared_ptr<bool> alive{new bool{true}};
		const std::function<void (bool&, Socket)> onReceipt;
		
		AcceptConnectionLoop(std::function<void (bool&, Socket)> onReceipt);
		AcceptConnectionLoop(const AcceptConnectionLoop&) = default;
		
		void loop_until_dead(int listen, bool async = false);
	};

}
