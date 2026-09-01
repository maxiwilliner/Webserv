#include "../include/webserv.hpp"

CGIHandler::CGIHandler()
{
}

CGIHandler::CGIHandler(const CGIHandler &original)
{
	this->env = original.env;
}

CGIHandler		&CGIHandler::operator=(const CGIHandler &original)
{
	if (this != &original)
	{
		this->env = original.env;
	}
	return (*this);
}

CGIHandler::~CGIHandler()
{
}

//Functions
void				CGIHandler::setupEnv(const HttpRequest &request, const LocationConfig &location)
{
	this->env.clear();
	this->env.push_back("REQUEST_METHOD=" + request.method);

	std::string uriWithoutQuery = request.uri;
	std::string queryString = "";
	size_t queryPos = request.uri.find('?');
	if (queryPos != std::string::npos)
	{
		uriWithoutQuery = request.uri.substr(0, queryPos);
		queryString = request.uri.substr(queryPos + 1);
	}

	std::string relUri = uriWithoutQuery;
	if (relUri.find(location.locationPath) == 0)
	{
		relUri = relUri.substr(location.locationPath.size());
	}
	while (!relUri.empty() && relUri[0] == '/')
	{
		relUri = relUri.substr(1);
	}

	std::string scriptFilename;
	if (!location.cgi_path.empty())
	{
		std::string basePath = location.cgi_path;
		if (!Utils::ends_with(basePath, "/"))
		{
			basePath += "/";
		}
		scriptFilename = basePath + relUri;
	}
	else
	{
		std::string basePath = location.root;
		if (!Utils::ends_with(basePath, "/"))
		{
			basePath += "/";
		}
		scriptFilename = basePath + relUri;
	}

	this->env.push_back("SCRIPT_NAME=" + uriWithoutQuery);
	this->env.push_back("SCRIPT_FILENAME=" + scriptFilename);
	this->env.push_back("QUERY_STRING=" + queryString);

	if (request.method == "POST")
	{
		std::ostringstream oss;
		oss << request.body.size();
		this->env.push_back("CONTENT_LENGTH=" + oss.str());
	}
	else
	{
		this->env.push_back("CONTENT_LENGTH=");
	}
	std::map<std::string, std::string>::const_iterator it = request.headers.find("Content-Type");
	if (it != request.headers.end()) {
		this->env.push_back("CONTENT_TYPE=" + it->second);
	} else {
		this->env.push_back("CONTENT_TYPE=");
	}
	
	this->env.push_back("SERVER_PROTOCOL=HTTP/1.1");
	this->env.push_back("GATEWAY_INTERFACE=CGI/1.1");

	std::map<std::string, std::string>::const_iterator it2 = request.headers.find("Host");
	if (it2 != request.headers.end()) {
		this->env.push_back("HTTP_HOST=" + it2->second);
	}

	std::map<std::string, std::string>::const_iterator it3 = request.headers.find("User-Agent");
	if (it3 != request.headers.end()) {
		this->env.push_back("HTTP_USER_AGENT=" + it3->second);
	} else {
		this->env.push_back("HTTP_USER_AGENT=");
	}
}

int				CGIHandler::startCGI(const HttpRequest &request, const LocationConfig &location, const ServerConfig &server, pid_t &outPid, int &outReadPipeFd)
{
	(void)server;
	setupEnv(request, location);

	std::string scriptFilename;
	for (size_t i = 0; i < this->env.size(); ++i)
	{
		if (this->env[i].find("SCRIPT_FILENAME=") == 0)
		{
			scriptFilename = this->env[i].substr(strlen("SCRIPT_FILENAME="));
			break;
		}
	}

	if (scriptFilename.empty() || !Utils::fileExists(scriptFilename))
	{
		return (404);
	}
	if (access(scriptFilename.c_str(), X_OK) < 0)
	{
		return (403);
	}
	if (request.headers.find("Host") == request.headers.end())
	{
		return (400);
	}

	int	pipeToChild[2];
	int	pipeFromChild[2];
	
	if (pipe(pipeToChild) < 0)
	{
		return (500);
	}
	if (pipe(pipeFromChild) < 0)
	{
		close(pipeToChild[0]);
		close(pipeToChild[1]);
		return (500);
	}

	fcntl(pipeFromChild[0], F_SETFL, O_NONBLOCK);

	pid_t	pid = fork();
	if (pid < 0)
	{
		close(pipeToChild[0]);
		close(pipeToChild[1]);
		close(pipeFromChild[0]);
		close(pipeFromChild[1]);
		return (500);
	}

	// In the child:
	if (pid == 0)
	{
		close(pipeToChild[1]);
		close(pipeFromChild[0]);

		dup2(pipeToChild[0], STDIN_FILENO);
		close(pipeToChild[0]);

		dup2(pipeFromChild[1], STDOUT_FILENO);
		close(pipeFromChild[1]);

		// chdir to script directory (Subject requirement)
		size_t slashPos = scriptFilename.rfind('/');
		std::string scriptFileOnly = scriptFilename;
		if (slashPos != std::string::npos)
		{
			std::string scriptDir = scriptFilename.substr(0, slashPos);
			scriptFileOnly = scriptFilename.substr(slashPos + 1);
			chdir(scriptDir.c_str());
		}

		std::string execPath = "./" + scriptFileOnly;
		char *argv[] = {const_cast<char*>(execPath.c_str()), NULL};

		std::vector<char*> envp;
		for (size_t i = 0; i < this->env.size(); i++)
		{
			envp.push_back(const_cast<char*>(this->env[i].c_str()));
		}
		envp.push_back(NULL);

		execve(execPath.c_str(), argv, envp.data());
		exit(1);
	}

	// In the parent:
	close(pipeToChild[0]);
	close(pipeFromChild[1]);

	// Write request body to child's stdin
	if (!request.body.empty())
	{
		write(pipeToChild[1], request.body.c_str(), request.body.size());
	}
	close(pipeToChild[1]); // Close to send EOF to child

	outPid = pid;
	outReadPipeFd = pipeFromChild[0];
	return (0);
}

HttpResponse	CGIHandler::parseOutput(const std::string &cgiOutput, const HttpRequest &request, const ServerConfig &server)
{
	HttpResponse	response;
	response.status_code = 200;
	response.status_text = "OK";

	std::string buffer = cgiOutput;
	std::string line;
	size_t pos = 0;

	if (buffer.find("\n\n") == std::string::npos && buffer.find("\r\n\r\n") == std::string::npos && !buffer.empty())
	{
		return (ErrorHandler::generateHttpResponse(500, server));
	}

	int k = 0;
	while (Utils::getNextLine(buffer, pos, line) && line != "\r\n" && line != "\n" && line != "\r" && !line.empty())
	{
		k++;
		// Strip trailing \r or \n
		while (!line.empty() && (line[line.size() - 1] == '\r' || line[line.size() - 1] == '\n'))
		{
			line.erase(line.size() - 1);
		}
		if (line.empty())
		{
			break;
		}

		size_t colonPos = line.find(':');
		if (colonPos == std::string::npos)
		{
			return (ErrorHandler::generateHttpResponse(500, server));
		}

		std::string header = line.substr(0, colonPos);
		std::string value = line.substr(colonPos + 1);
		value = Utils::ft_trim(value);

		if (header == "Status")
		{
			std::vector<std::string> statusParts = Utils::splitBySpaces(value);
			if (statusParts.size() >= 1)
			{
				response.status_code = std::atoi(statusParts[0].c_str());
				if (statusParts.size() >= 2)
				{
					response.status_text = value.substr(statusParts[0].size() + 1);
				}
				else
				{
					response.status_text = Utils::statusText(response.status_code);
				}
			}
		}
		else if (header == "Content-Type" || header == "Location" || header == "Content-Length")
		{
			response.headers[header] = value;
		}
		else
		{
			response.headers[header] = value;
		}
	}

	if (request.headers.find("Connection") == request.headers.end() || request.headers.find("Connection")->second == "keep-alive")
	{
		response.headers["Connection"] = "keep-alive";
	}
	else
	{
		response.headers["Connection"] = "close";
	}

	if (response.headers.find("Location") != response.headers.end() && response.status_code == 200)
	{
		response.status_code = 302;
		response.status_text = "Found";
	}

	if (response.headers.find("Content-Type") == response.headers.end() && !cgiOutput.empty() && request.method != "GET" && response.headers.find("Location") == response.headers.end())
	{
		return (ErrorHandler::generateHttpResponse(500, server));
	}
	else if (response.headers.find("Content-Type") == response.headers.end())
	{
		response.headers["Content-Type"] = "text/plain";
	}

	// Parse remaining as body
	if (pos < buffer.size())
	{
		response.body = buffer.substr(pos);
	}

	std::ostringstream oss;
	oss << response.body.size();
	response.headers["Content-Length"] = oss.str();

	return (response);
}

