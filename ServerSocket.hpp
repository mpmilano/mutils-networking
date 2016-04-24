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
		volatile bool alive{true};
		Internals(int sockID);
	};
private:
	std::shared_ptr<Internals> i;
public:
	ServerSocket(int listen);
	ServerSocket(int listen,
				 std::function<void (Socket)> onReceipt,
				 bool async = false);
	ServerSocket(const ServerSocket&) = delete;
	Socket receive();
	~ServerSocket();
};

}
