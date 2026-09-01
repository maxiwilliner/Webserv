#pragma once

#include "webserv.hpp"

class ServerManager 
{
	private:

		std::vector<ServerConfig>				serversList; 			//Vector containing the information of each server
		std::map<int, ServerConfig>				listeningSockets;		//We have a socket FD for each server with unique (Host, Port)
		std::map<int, ConnectionHandler*>		clientConnections;		//We have a conection for each client socket FD
		std::vector<pollfd>						pollfds;				//Vector containing a list of pollfd structs. Each one represents a socket FD that poll() monitors for events
		std::map<int, ServerConfig>				clientToServerMap;
		std::map<int, int>						pipeToClientMap;		//Maps cgi_pipe_fd -> client_fd
		std::vector<int>						acceptedConections;

		void		acceptNewClient(int server_fd, std::vector<pollfd> &fds_to_add);
		void		handleClientRequest(int client_fd, std::vector<pollfd> &fds_to_add, std::vector<int> &fds_to_close);
		void		handleCGIPipe(int pipe_fd, std::vector<int> &fds_to_close);
		void		checkCGITimeouts();
		void		closeClient(int client_fd);
		void		removePollFd(int fd);
		void		setPollFdEvent(int fd, short events);

	public: 

		ServerManager();
		ServerManager(const ServerManager &original);
		ServerManager	&operator=(const ServerManager &original);
		~ServerManager();

		void		loadConfigParsing(const std::string &filename);		//Starts the parsing of the config file
		void		initSockets();										//Initializes each of the sockets
		void		run();												//Main loop for poll()

};
