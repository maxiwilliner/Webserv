#include "../include/webserv.hpp"

//Constructors, operators and destructor

ServerManager::ServerManager() {
	this->acceptedConections.clear();
}

ServerManager::ServerManager(const ServerManager &original)
{
	this->serversList = original.serversList;
	this->listeningSockets = original.listeningSockets;
	this->clientConnections = original.clientConnections;
	this->pollfds = original.pollfds;
	this->acceptedConections = original.acceptedConections;
	this->pipeToClientMap = original.pipeToClientMap;
}

ServerManager	&ServerManager::operator=(const ServerManager &original)
{
	if (this != &original)
	{
		this->serversList = original.serversList;
		this->listeningSockets = original.listeningSockets;
		this->clientConnections = original.clientConnections;
		this->pollfds = original.pollfds;
		this->acceptedConections = original.acceptedConections;
		this->pipeToClientMap = original.pipeToClientMap;
	}
	return (*this);
}

ServerManager::~ServerManager()
{
	if (!this->clientConnections.empty())
	{
		// Free the memory allocated for each of the connections:
		std::map<int, ConnectionHandler*>::iterator		it;
		for (it = this->clientConnections.begin(); it != this->clientConnections.end(); ++it)
		{
			if (it->second)
			{
				if (it->second->cgiPid > 0)
				{
					kill(it->second->cgiPid, SIGKILL);
					waitpid(it->second->cgiPid, NULL, WNOHANG);
				}
				if (it->second->cgiPipeFd >= 0)
				{
					close(it->second->cgiPipeFd);
				}
				delete it->second;
			}
		}
		this->clientConnections.clear();
	}

	// Close each of the FDs in pollfds
	for (size_t i = 0; i < this->pollfds.size(); ++i)
	{
		if (this->pollfds[i].fd >= 0)
		{
			close(this->pollfds[i].fd);
			this->pollfds[i].fd = -1;
		}
	}
}

//Functions
void			ServerManager::loadConfigParsing(const std::string &filename)
{
	ConfigFileParser		configParser;
	this->serversList = configParser.parse(filename);
	std::cout << "[INFO] - CONFIGURATION FILE PARSED!" << std::endl;
}

void			ServerManager::initSockets()
{
	std::vector<std::pair<std::string, int> >	hostPortList;
	for (size_t i = 0; i < this->serversList.size(); i++)
	{
		std::pair<std::string, int>		hostPort;
		bool							pushToList = true;

		hostPort.first = this->serversList[i].ip;
		hostPort.second = this->serversList[i].port;
		for (size_t j = 0; j < hostPortList.size(); j++)
		{
			if (hostPort.first == hostPortList[j].first && hostPort.second == hostPortList[j].second)
			{
				pushToList = false;
				break;
			}
		}
		if (pushToList)
		{
			hostPortList.push_back(hostPort);

			int sockfd = socket(AF_INET, SOCK_STREAM, 0);
			if (sockfd < 0)
			{
				throw std::runtime_error("Failed to create socket");
			}

			int opt = 1;
			setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
			fcntl(sockfd, F_SETFL, O_NONBLOCK);

			sockaddr_in addr;
			memset(&addr, 0, sizeof(addr));
			addr.sin_family = AF_INET;
			addr.sin_port = htons(hostPort.second);
			inet_pton(AF_INET, hostPort.first.c_str(), &addr.sin_addr);

			if (bind(sockfd, (sockaddr*)&addr, sizeof(addr)) < 0)
			{
				close(sockfd);
				if (errno == EADDRINUSE)
				{
					throw std::runtime_error("Port already in use");
				}
				else 
				{
					throw std::runtime_error("Bind failed!");
				}
			}

			if (listen(sockfd, SOMAXCONN) < 0)
			{
				close(sockfd);
				throw std::runtime_error("Listen failed!");
			}

			this->listeningSockets[sockfd] = this->serversList[i];
			std::cout << "[INFO] - INITIALIZING SOCKET ON " << hostPort.first << ":" << hostPort.second << std::endl;

			pollfd pfd;
			pfd.fd = sockfd;
			pfd.events = POLLIN;
			pfd.revents = 0;
			this->pollfds.push_back(pfd);
		}
	}
	std::cout << "[INFO] - SERVER IS READY TO RUN!" << std::endl;
}

void			ServerManager::setPollFdEvent(int fd, short events)
{
	for (size_t i = 0; i < this->pollfds.size(); i++)
	{
		if (this->pollfds[i].fd == fd)
		{
			this->pollfds[i].events = events;
			this->pollfds[i].revents = 0;
			return;
		}
	}
}

void			ServerManager::removePollFd(int fd)
{
	for (size_t i = 0; i < this->pollfds.size(); i++)
	{
		if (this->pollfds[i].fd == fd)
		{
			this->pollfds.erase(this->pollfds.begin() + i);
			return;
		}
	}
}

void			ServerManager::checkCGITimeouts()
{
	std::time_t now = std::time(NULL);
	for (std::map<int, ConnectionHandler*>::iterator it = this->clientConnections.begin(); it != this->clientConnections.end(); ++it)
	{
		if (it->second && it->second->isWaitingCGI())
		{
			if (now - it->second->cgiStartTime > 5) // 5 seconds CGI timeout
			{
				std::cout << "[INFO] - CGI timed out for client fd " << it->first << std::endl;
				if (it->second->cgiPid > 0)
				{
					kill(it->second->cgiPid, SIGKILL);
					waitpid(it->second->cgiPid, NULL, WNOHANG);
					it->second->cgiPid = -1;
				}
				if (it->second->cgiPipeFd >= 0)
				{
					close(it->second->cgiPipeFd);
					removePollFd(it->second->cgiPipeFd);
					this->pipeToClientMap.erase(it->second->cgiPipeFd);
					it->second->cgiPipeFd = -1;
				}
				it->second->response = ErrorHandler::generateHttpResponse(504, it->second->config.first);
				it->second->setState(ConnectionHandler::WRITING);
				setPollFdEvent(it->first, POLLOUT);
			}
		}
	}
}

void			ServerManager::handleCGIPipe(int pipe_fd, std::vector<int> &fds_to_close)
{
	if (this->pipeToClientMap.find(pipe_fd) == this->pipeToClientMap.end())
	{
		fds_to_close.push_back(pipe_fd);
		return;
	}

	int client_fd = this->pipeToClientMap[pipe_fd];
	if (this->clientConnections.find(client_fd) == this->clientConnections.end())
	{
		fds_to_close.push_back(pipe_fd);
		return;
	}

	ConnectionHandler *client = this->clientConnections[client_fd];
	char buffer[BUFFER_SIZE];
	ssize_t n = read(pipe_fd, buffer, sizeof(buffer));

	if (n > 0)
	{
		client->cgiBuffer.append(buffer, n);
	}
	else if (n == 0) // EOF: CGI script finished output
	{
		fds_to_close.push_back(pipe_fd);
		this->pipeToClientMap.erase(pipe_fd);
		client->cgiPipeFd = -1;

		int status = 0;
		waitpid(client->cgiPid, &status, WNOHANG);
		client->cgiPid = -1;

		if (client->cgiBuffer.empty() && WIFEXITED(status) && WEXITSTATUS(status) != 0)
		{
			client->response = ErrorHandler::generateHttpResponse(500, client->config.first);
		}
		else
		{
			client->response = CGIHandler::parseOutput(client->cgiBuffer, client->request, client->config.first);
		}
		client->setState(ConnectionHandler::WRITING);
		setPollFdEvent(client_fd, POLLOUT);
	}
	else // Read error
	{
		if (client->cgiPid > 0)
		{
			kill(client->cgiPid, SIGKILL);
			waitpid(client->cgiPid, NULL, WNOHANG);
			client->cgiPid = -1;
		}
		fds_to_close.push_back(pipe_fd);
		this->pipeToClientMap.erase(pipe_fd);
		client->cgiPipeFd = -1;

		client->response = ErrorHandler::generateHttpResponse(500, client->config.first);
		client->setState(ConnectionHandler::WRITING);
		setPollFdEvent(client_fd, POLLOUT);
	}
}

void			ServerManager::run()
{
	std::cout << "[INFO] - SERVER IS RUNNING!" << std::endl;
	while (true)
	{
		checkCGITimeouts();

		int ready = poll(this->pollfds.data(), this->pollfds.size(), 1000);
		if (ready <= 0)
		{
			std::time_t now = std::time(NULL);
			if (ready == 0)
			{
				std::vector<int> idle_to_close;
				for (std::map<int, ConnectionHandler *>::iterator it = this->clientConnections.begin(); it != this->clientConnections.end(); ++it)
				{
					if (!it->second->isWaitingCGI() && now - it->second->lastActivity > TIMEOUT)
					{
						std::cout << "[INFO] - Closing idle connection (fd = " << it->first << ")" << std::endl;
						idle_to_close.push_back(it->first);
					}
				}
				for (size_t k = 0; k < idle_to_close.size(); ++k)
				{
					closeClient(idle_to_close[k]);
				}
			}
			continue;
		}

		std::vector<pollfd> fds_to_add;
		std::vector<int> fds_to_close;

		for (size_t i = 0; i < this->pollfds.size(); i++)
		{
			if (this->pollfds[i].revents == 0)
			{
				continue;
			}

			int fd = this->pollfds[i].fd;

			if (this->pollfds[i].revents & (POLLERR | POLLHUP | POLLNVAL))
			{
				if (this->pipeToClientMap.count(fd))
				{
					handleCGIPipe(fd, fds_to_close);
				}
				else if (!this->listeningSockets.count(fd))
				{
					fds_to_close.push_back(fd);
				}
				continue;
			}

			if (this->pollfds[i].revents & POLLIN)
			{
				if (this->listeningSockets.count(fd))
				{
					acceptNewClient(fd, fds_to_add);
				}
				else if (this->pipeToClientMap.count(fd))
				{
					handleCGIPipe(fd, fds_to_close);
				}
				else if (this->clientConnections.count(fd))
				{
					handleClientRequest(fd, fds_to_add, fds_to_close);
				}
			}
			else if (this->pollfds[i].revents & POLLOUT)
			{
				if (this->clientConnections.count(fd))
				{
					handleClientRequest(fd, fds_to_add, fds_to_close);
				}
			}
		}

		// Apply deferred closures
		for (size_t k = 0; k < fds_to_close.size(); ++k)
		{
			int close_fd = fds_to_close[k];
			if (this->pipeToClientMap.count(close_fd))
			{
				close(close_fd);
				removePollFd(close_fd);
				this->pipeToClientMap.erase(close_fd);
			}
			else if (this->clientConnections.count(close_fd))
			{
				closeClient(close_fd);
			}
		}

		// Apply deferred additions
		for (size_t k = 0; k < fds_to_add.size(); ++k)
		{
			this->pollfds.push_back(fds_to_add[k]);
		}
	}
}

void			ServerManager::acceptNewClient(int server_fd, std::vector<pollfd> &fds_to_add)
{
	while (true)
	{
		int client_fd = accept(server_fd, NULL, NULL);
		if (client_fd < 0)
		{
			break;
		}

		fcntl(client_fd, F_SETFL, O_NONBLOCK);
		this->clientConnections[client_fd] = new ConnectionHandler(client_fd);
		this->clientToServerMap[client_fd] = this->listeningSockets[server_fd];

		pollfd fds;
		fds.fd = client_fd;
		fds.events = POLLIN;
		fds.revents = 0;
		fds_to_add.push_back(fds);
	}
}

void			ServerManager::handleClientRequest(int client_fd, std::vector<pollfd> &fds_to_add, std::vector<int> &fds_to_close)
{
	if (this->clientConnections.find(client_fd) == this->clientConnections.end())
	{
		return;
	}

	ConnectionHandler *client = this->clientConnections[client_fd];

	if (!client->isRequestComplete())
	{
		if (!client->readRequest())
		{
			fds_to_close.push_back(client_fd);
			return;
		}

		if (client->isRequestComplete())
		{
			Router router;
			std::string ip = this->clientToServerMap[client_fd].ip;
			int port = this->clientToServerMap[client_fd].port;
			std::vector<ServerConfig> matchServers;
			
			for (size_t i = 0; i < this->serversList.size(); i++)
			{
				if (this->serversList[i].port == port && this->serversList[i].ip == ip)
				{
					matchServers.push_back(this->serversList[i]);
				}
			}

			client->config = router.route(client->request, matchServers);

			// Check if CGI should handle this request
			std::string newUri = client->request.uri;
			if (newUri.find("?") != std::string::npos)
			{
				newUri = newUri.substr(0, newUri.find("?"));
			}

			if ((client->config.second.cgi_extension != "" || client->config.second.cgi_path != "") && Utils::ends_with(newUri, client->config.second.cgi_extension))
			{
				CGIHandler cgi;
				pid_t pid = -1;
				int readPipeFd = -1;
				int res = cgi.startCGI(client->request, client->config.second, client->config.first, pid, readPipeFd);

				if (res != 0)
				{
					client->response = ErrorHandler::generateHttpResponse(res, client->config.first);
					client->setState(ConnectionHandler::WRITING);
					setPollFdEvent(client_fd, POLLOUT);
				}
				else
				{
					client->cgiPid = pid;
					client->cgiPipeFd = readPipeFd;
					client->cgiStartTime = std::time(NULL);
					client->cgiBuffer.clear();
					client->setState(ConnectionHandler::WAITING_CGI);

					this->pipeToClientMap[readPipeFd] = client_fd;

					pollfd pfd;
					pfd.fd = readPipeFd;
					pfd.events = POLLIN;
					pfd.revents = 0;
					fds_to_add.push_back(pfd);

					setPollFdEvent(client_fd, 0); // Disable socket events while CGI runs
				}
			}
			else
			{
				setPollFdEvent(client_fd, POLLOUT);
			}
		}
	}

	if (client->isReadyToWrite())
	{
		if (!client->writeResponse(client->config.first))
		{
			fds_to_close.push_back(client_fd);
			return;
		}
		if (client->isResponseComplete())
		{
			if (client->response.headers.find("Connection") != client->response.headers.end() && client->response.headers.find("Connection")->second == "close")
			{
				fds_to_close.push_back(client_fd);
			}
			else 
			{
				setPollFdEvent(client_fd, POLLIN);
				client->cleanConnection();
			}
		}
	}
}

void			ServerManager::closeClient(int client_fd)
{
	removePollFd(client_fd);

	if (this->clientConnections.find(client_fd) != this->clientConnections.end())
	{
		ConnectionHandler *client = this->clientConnections[client_fd];
		if (client)
		{
			if (client->cgiPid > 0)
			{
				kill(client->cgiPid, SIGKILL);
				waitpid(client->cgiPid, NULL, WNOHANG);
				client->cgiPid = -1;
			}
			if (client->cgiPipeFd >= 0)
			{
				close(client->cgiPipeFd);
				removePollFd(client->cgiPipeFd);
				this->pipeToClientMap.erase(client->cgiPipeFd);
				client->cgiPipeFd = -1;
			}
			delete client;
		}
		this->clientConnections.erase(client_fd);
	}

	this->clientToServerMap.erase(client_fd);

	if (client_fd >= 0)
	{
		close(client_fd);
	}
}

