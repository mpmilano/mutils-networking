#pragma once
#include "Socket.hpp"

namespace mutils{

struct ServerSocket {

private:
	struct Internals;
	Internals *i;
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
