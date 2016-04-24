#include "ServerSocket.hpp"
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <netdb.h>
#include <thread>

namespace mutils{

	ServerSocket::ServerSocket(int portno){
		int sockfd;
		bool complete = false;
		struct sockaddr_in serv_addr;
		sockfd = socket(AF_INET, SOCK_STREAM, 0);
		//linger lingerStruct{0,0};
		//setsockopt(sockfd, SOL_SOCKET, SO_LINGER, (void *)&lingerStruct, sizeof(lingerStruct));
		if (sockfd < 0){
			std::cerr << "ERROR opening socket" << std::endl;
			throw SocketException{};
		}
		bzero((char *) &serv_addr, sizeof(serv_addr));
		serv_addr.sin_family = AF_INET;
		serv_addr.sin_addr.s_addr = INADDR_ANY;
		serv_addr.sin_port = htons(portno);
		if (bind(sockfd, (struct sockaddr *) &serv_addr,
				 sizeof(serv_addr)) < 0){
			std::cerr << "ERROR on binding" << std::endl;
			throw SocketException{};
		}
		AtScopeEnd ase{[&](){if (!complete) close(sockfd);}};
		{
			bool success = listen(sockfd,50) == 0;
			assert(success);
		}
		
		complete = true;
		this->i.reset(new Internals{sockfd});
	}

	ServerSocket::Internals::Internals(int s):sockID(s){}

	namespace {
		Socket receive_impl(ServerSocket::Internals& i){
			int newsockfd = accept(i.sockID,
								   (struct sockaddr *) &i.cli_addr,
								   &i.clilen);
			if (newsockfd < 0){
				std::cerr << "ERROR on accept: "
						  << std::strerror(errno)
						  << std::endl;
			}
			return Socket{newsockfd};
		}
	}

	ServerSocket::ServerSocket(int listen,
							   std::function<void (Socket)> onReceipt,
							   bool async):ServerSocket(listen){
		std::thread([async,onReceipt,i = this->i](){
				while (i->alive) {
					if (async){
						std::thread([onReceipt,sock = receive_impl(*i)] () {
								onReceipt(sock);
							}).detach();
					}
					else {
						onReceipt(receive_impl(*i));
					}
				}
			}).detach();
	}

	ServerSocket::~ServerSocket(){
		close(i->sockID);
		i->alive = false;
	}

	Socket ServerSocket::receive(){
		return receive_impl(*i);
	}
}
