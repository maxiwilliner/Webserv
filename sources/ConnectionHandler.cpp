#include "../include/webserv.hpp"

ConnectionHandler::ConnectionHandler()
{
	this->fd = -1;
	this->buffer = "";
	this->state = READING;
	this->lastActivity = std::time(NULL);
	this->sendBuffer = "";
	this->sendOffset = 0;
	this->cgiPid = -1;
	this->cgiPipeFd = -1;
	this->cgiStartTime = 0;
	this->cgiBuffer = "";
}

ConnectionHandler::ConnectionHandler(int fd)
{
	this->fd = fd;
	this->buffer = "";
	this->state = READING;
	this->lastActivity = std::time(NULL);
	this->sendBuffer = "";
	this->sendOffset = 0;
	this->cgiPid = -1;
	this->cgiPipeFd = -1;
	this->cgiStartTime = 0;
	this->cgiBuffer = "";
}

ConnectionHandler::ConnectionHandler(const ConnectionHandler &original)
{
	this->fd = original.fd;
	this->buffer = original.buffer;
	this->state = original.state;
	this->lastActivity = original.lastActivity;
	this->sendBuffer = original.sendBuffer;
	this->sendOffset = original.sendOffset;
	this->request = original.request;
	this->response = original.response;
	this->config = original.config;
	this->cgiPid = original.cgiPid;
	this->cgiPipeFd = original.cgiPipeFd;
	this->cgiStartTime = original.cgiStartTime;
	this->cgiBuffer = original.cgiBuffer;
}

ConnectionHandler	&ConnectionHandler::operator=(const ConnectionHandler &original)
{
	if (this != &original)
	{
		this->fd = original.fd;
		this->buffer = original.buffer;
		this->state = original.state;
		this->lastActivity = original.lastActivity;
		this->sendBuffer = original.sendBuffer;
		this->sendOffset = original.sendOffset;
		this->request = original.request;
		this->response = original.response;
		this->config = original.config;
		this->cgiPid = original.cgiPid;
		this->cgiPipeFd = original.cgiPipeFd;
		this->cgiStartTime = original.cgiStartTime;
		this->cgiBuffer = original.cgiBuffer;
	}
	return (*this);
}

ConnectionHandler::~ConnectionHandler()
{
	if (this->fd >= 0)
	{
		close(this->fd);
		this->fd = -1;
	}
}

//Functions
bool				ConnectionHandler::writeResponse(const ServerConfig &server)
{
	if (this->state != WRITING)
	{
		return (false);
	}
	// If response is not yet built (status_code == -1), build it
	if (this->sendBuffer.empty())
	{
		if (this->response.status_code <= 0)
		{
			ResponseBuilder builder;
			if (this->request.error > 0)
			{
				this->response = ErrorHandler::generateHttpResponse(this->request.error, server);
			}
			else if (this->config.second.client_max_body_size >= 0 && int(this->request.body.size()) > this->config.second.client_max_body_size)
			{
				this->response = ErrorHandler::generateHttpResponse(413, server);
			}
			else 
			{
				this->response = builder.generateHttpResponse(this->request, this->config);
			}
		}
		this->sendBuffer = this->response.build();
		this->sendOffset = 0;
	}

	ssize_t sent = send(this->fd, this->sendBuffer.c_str() + this->sendOffset, this->sendBuffer.size() - this->sendOffset, 0);
	if (sent <= 0)
	{
		return (false);
	}
	this->sendOffset += sent;
	if (this->sendOffset >= this->sendBuffer.size())
	{
		this->state = READY;
	}
	return (true);
}

bool				ConnectionHandler::isReadyToWrite()
{
	return (this->state == WRITING);
}

bool				ConnectionHandler::isRequestComplete()
{
	return (this->state != READING);
}

bool				ConnectionHandler::isWaitingCGI()
{
	return (this->state == WAITING_CGI);
}

void				ConnectionHandler::setState(State newState)
{
	this->state = newState;
}

ConnectionHandler::State	ConnectionHandler::getState() const
{
	return (this->state);
}

bool				ConnectionHandler::isResponseComplete()
{
	return (this->state == READY);
}

void 				ConnectionHandler::cleanConnection()
{
	this->state = READING;
	this->buffer.clear();
	this->sendBuffer.clear();
	this->sendOffset = 0;
	this->cgiPid = -1;
	this->cgiPipeFd = -1;
	this->cgiStartTime = 0;
	this->cgiBuffer.clear();
	this->request.body.clear();
	this->request.headers.clear();
	this->request.error = -1;
	this->request.method.clear();
	this->request.uri.clear();
	this->request.version.clear();
	this->response.body.clear();
	this->response.headers.clear();
	this->response.status_code = -1;
	this->response.status_text.clear();
}

bool				ConnectionHandler::parseRequest()
{
	// We search for the end of headers; if not found yet, wait for more data
	size_t endOfHeaders = this->buffer.find("\r\n\r\n");
	if (endOfHeaders == std::string::npos)
	{
		return (true);
	}

	RequestParser		parser;
	std::string			line;
	size_t				pos = 0;

	while (Utils::getNextLine(buffer, pos, line))
	{
		parser.parseLine(line);
	}

	if (parser.isRequestComplete())
	{
		this->state = WRITING;
		this->request = parser.getHttpRequest();
		std::cout << "[TRACE] - " << this->request.method << " " << this->request.uri << " " << this->request.version << std::endl;
		return (true);
	}
	return (true);
}

bool				ConnectionHandler::readRequest()
{
	if (this->state != READING)
	{
		return (true);
	}
	char		tmpBuffer[BUFFER_SIZE];
	ssize_t bytes = recv(this->fd, tmpBuffer, sizeof(tmpBuffer), 0);

	if (bytes <= 0)
	{
		// 0 means client closed connection, < 0 means error
		return (false);
	}

	this->buffer.append(tmpBuffer, bytes);
	this->lastActivity = std::time(NULL);
	return (parseRequest());
}

