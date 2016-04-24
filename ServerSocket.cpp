#include "ServerSocket.hpp"
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <netdb.h>
#include <thread>

namespace mutils{

	struct ServerSocket::Internals {
		const int sockID;
		struct sockaddr_in cli_addr;
		socklen_t clilen{sizeof(cli_addr)};
	};

	ServerSocket::ServerSocket(int portno){
		int sockfd;
		bool complete = false;
		struct sockaddr_in serv_addr;
		sockfd = socket(AF_INET, SOCK_STREAM, 0);
		//linger lingerStruct{0,0};
		//setsockopt(sockfd, SOL_SOCKET, SO_LINGER, (void *)&lingerStruct, sizeof(lingerStruct));
		if (sockfd < 0){
			std::cerr << "ERROR opening socket" << std::endl;
			return;
		}
		bzero((char *) &serv_addr, sizeof(serv_addr));
		serv_addr.sin_family = AF_INET;
		serv_addr.sin_addr.s_addr = INADDR_ANY;
		serv_addr.sin_port = htons(portno);
		if (bind(sockfd, (struct sockaddr *) &serv_addr,
				 sizeof(serv_addr)) < 0){
			std::cerr << "ERROR on binding" << std::endl;
			return;
		}
		AtScopeEnd ase{[&](){if (!complete) close(sockfd);}};
		{
			bool success = listen(sockfd,50) == 0;
			assert(success);
		}
		
		complete = true;
		this->i = new Internals{sockfd};
	}

	ServerSocket::~ServerSocket(){
		close(i->sockID); delete i;
	}

	Socket ServerSocket::receive(){
		int newsockfd = accept(i->sockID,
							   (struct sockaddr *) &i->cli_addr,
							   &i->clilen);
		if (newsockfd < 0){
			std::cerr << "ERROR on accept: "
					  << std::strerror(errno)
					  << std::endl;
		}
		return Socket{newsockfd};
	}
}
