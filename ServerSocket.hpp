#pragma once
#include "Socket.hpp"

namespace mutils{

struct ServerSocket {

private:
	struct Internals;
	Internals *i;
public:
	ServerSocket(int listen);
	Socket receive();
	~ServerSocket();
};

}
