#include "../include/webserv.hpp"

ResponseBuilder::ResponseBuilder()
{

}

ResponseBuilder::ResponseBuilder(const ResponseBuilder &original)
{

	*this = original;
}

ResponseBuilder		&ResponseBuilder::operator=(const ResponseBuilder &original)
{
	if (this != &original)
	{

	}
	return (*this);

}

ResponseBuilder::~ResponseBuilder()
{

}


//Functions
std::string			ResponseBuilder::getMimeType(const std::string &path)
{
	if (Utils::ends_with(path, ".html"))
	{
		return ("text/html");
	}
	if (Utils::ends_with(path, ".css"))
	{
		return ("text/css");
	}
	if (Utils::ends_with(path, ".js"))
	{
		return ("application/javascript");
	}
	if (Utils::ends_with(path, ".png"))
	{
		return ("image/png");
	}
	if (Utils::ends_with(path, ".jpg") || Utils::ends_with(path, ".jpeg"))
	{
		return ("image/jpeg");
	}
	if (Utils::ends_with(path, ".txt"))
	{
		return ("text/plain");
	}
	return ("application/octet-stream");
}


std::string			ResponseBuilder::getExtensionType(const std::string &mime)
{
	if (mime == "text/html")
	{
		return (".html");
	}
	if (mime == "text/css")
	{
		return (".css");
	}
	if (mime == "application/javascript")
	{
		return (".js");
	}
	if (mime == "image/png")
	{
		return (".png");
	}
	if (mime == "image/jpeg")
	{
		return (".jpeg");
	}
	if (mime == "text/plain")
	{
		return (".txt");
	}
	return ("");
}


std::string			ResponseBuilder::getFileContent(const std::string &path)
{
	std::ifstream	file(path.c_str());
	std::string		buffer;
	std::string		line;

	if (!file.is_open())
	{
		throw InvalidPathExcept();
	}
	while (std::getline(file, line))
	{
		buffer.append(line);
	}
	file.close();
	return(buffer);
}


std::string 		ResponseBuilder::generateAutoIndexHtml(const std::string& dirPath)
{
    std::string html = "<html><head><title>Index of " + dirPath + "</title></head><body>";
    html += "<h1>Index of " + dirPath + "</h1><ul>";

    DIR* dir = opendir(dirPath.c_str());
    if (!dir)
	{
        return ("");
	}

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL)
    {
        std::string name = entry->d_name;
        if (name == ".")
		{
			continue;
		}

        std::string href = name;
        if (entry->d_type == DT_DIR)
		{
            href += "/";
		}
        html += "<li><a href=\"" + href + "\">" + name + "</a></li>";
    }
    closedir(dir);

    html += "</ul></body></html>";
    return (html);
}


std::string			ResponseBuilder::getPath(const bool exactMatch, const std::string method, const std::string serverRoot, const std::string root, const std::string locPrefix, const std::string uri, const std::vector<std::string> index, const bool autoindex)
{
	std::string		path;
	std::string		uriMinusLocPrefix;

	//removed matched location prefix from uri
	if (exactMatch)
	{
		std::string newRoot = root;
		if (root[root.size() - 1] == '/')
		{
			newRoot = root.substr(0, root.size() - 1);
		}
		std::string newUri = uri;
		if (uri[0] != '/')
		{
			newUri = "/" + uri;
		}
		// std::cout << "root: " << newRoot << " + uri: " << newUri << " = path: " << path << std::endl;
		path = newRoot + newUri;
	}
	else 
	{
		if (serverRoot == root)
		{
			std::string newRoot = root;
			if (root[root.size() - 1] == '/')
			{
				newRoot = root.substr(0, root.size() - 1);
			}
			std::string newUri = uri;
			if (uri[0] != '/')
			{
				newUri = "/" + uri;
			}
			// std::cout << "server root: " << newRoot << " + uri: " << newUri << " = path: " << path << std::endl;
			path = newRoot + newUri;
		}
		else 
		{
			std::string newRoot = root;
			if (root[root.size() - 1] == '/')
			{
				newRoot = root.substr(0, root.size() - 1);
			}
			std::string	newUri = uri.substr(locPrefix.size());
			if (newUri[0] != '/')
			{
				newUri = "/" + newUri;
			}
			// std::cout << "location root: " << newRoot << " + uri - location path: " << newUri << " = path: " << path << std::endl;
			path = newRoot + newUri;
		}
	}

	// std::cout << path << std::endl;
	
	//If the path is a directory, we match it with the indexes provided and we create the path
	if (Utils::isDirectory(path))
	{
		size_t i;
		if (!index.empty())
		{
			for (i = 0; i < index.size(); i++)
			{
				std::string filePath = path + "/" + index[i];
				if (Utils::fileExists(filePath))
				{
					return (filePath);
				}
			}
			//Later do the autoindex part
			if (autoindex)
			{
				std::string autoIn = generateAutoIndexHtml(path);
				if (autoIn == "")
				{
					return ("403");
				}
				return (autoIn);
			}
			else 
			{
				return ("403");
			}
		}
		else 
		{
			if (autoindex)
			{
				std::string autoIn = generateAutoIndexHtml(path);
				if (autoIn == "")
				{
					return ("403");
				}
				return (autoIn);
			}
			else
			{
				return ("403");
			}
		}
	}
	if (Utils::fileExists(path))
	{
		if (method == "GET")
		{
			if (access(path.c_str(), R_OK) != 0)
			{
				return ("403");
			}
		}
		else if (method == "POST")
		{
			if (access(path.c_str(), W_OK) != 0 || access(path.c_str(), F_OK) != 0)
			{
				return ("403");
			}
		}
		else if (method == "DELETE")
		{
			if (access(path.c_str(), W_OK) != 0)
			{
				return ("403");
			}
		}
		return (path);
	}
	else 
	{
		return ("404");
	}
}


std::string			ResponseBuilder::generateFilename(const std::string &mime) {
    std::time_t t = std::time(NULL);
    std::tm* tm = std::localtime(&t);
	std::string		extension = getExtensionType(mime);
    
	//upload_ + date + extension
    std::ostringstream oss;
    char buffer[64];
	std::strftime(buffer, sizeof(buffer), "%Y%m%d_%H%M%S", tm);
	oss << "upload_" << buffer << extension;
	// std::cout << oss.str() << std::endl;
    return oss.str();
}


HttpResponse		ResponseBuilder::generateHttpResponse(const HttpRequest &request, const std::pair<ServerConfig, LocationConfig>	 &config)
{
	HttpResponse	response;
	ServerConfig	server = config.first;
	LocationConfig	location = config.second;

	//1. We check if the method is allowed. If not -> error 405
	size_t i;
	for (i = 0; i < location.allowed_methods.size(); i++)
	{
		if (location.allowed_methods[i] == request.method)
		{
			break;
		}
	}
	if (i >= location.allowed_methods.size())
	{
		return (ErrorHandler::generateHttpResponse(405, server));
	}
	if (!location.returnDirective.empty())
	{
		std::map<int, std::string>::iterator it = location.returnDirective.begin();
		response.status_code = it->first;
		if (it->first == 301)
		{
			response.status_text = "Moved Permanently";
			response.headers["Location"] = it->second;
			response.headers["Content-Length"] = "0";
		}
		else if (it->first == 302)
		{
			response.status_text = "Found";
			response.headers["Location"] = it->second;
			response.headers["Content-Length"] = "0";
		}
		else if (it->first == 307)
		{
			response.status_text = "Temporary Redirect";
			response.headers["Location"] = it->second;
			response.headers["Content-Length"] = "0";
		}
		else if (it->first == 308)
		{
			response.status_text = "Permanent Redirect";
			response.headers["Location"] = it->second;
			response.headers["Content-Length"] = "0";
		}
		else if (it->first == 400 || it->first == 403 || it->first == 404 || it->first == 500)
		{
			return (ErrorHandler::generateHttpResponse(it->first, server));
		}
		else if (it->first == 200)
		{
			response.status_text = "OK";
		}
		else 
		{
			return (ErrorHandler::generateHttpResponse(501, server));
		}
		if (request.headers.count("Connection"))
		{
			if (request.headers.find("Connection")->second == "close")
			{
				response.headers["Connection"] = "close";
			}
			else 
			{
				response.headers["Connection"] = "keep-alive";
			}
		}
		else 
		{
			response.headers["Connection"] = "keep-alive";
		}
		return (response);
	}

	//2. method GET
	if (request.method == "GET")
	{
		std::string newUri = request.uri;
		if (newUri.find("?") != std::string::npos)
		{
			newUri = newUri.substr(0, newUri.find("?"));
		}

		// First we get the path of the file.
		std::string path = getPath(location.exactMatch, request.method, server.root, location.root, location.locationPath, newUri, location.index, location.autoindex);
			if (path == "404")
			{
				return (ErrorHandler::generateHttpResponse(404, server));
			}
			else if (path == "403")
			{
				return (ErrorHandler::generateHttpResponse(403, server));
			}
			else if (path.substr(0, 6) == "<html>")
			{
				response.status_code = 200;
				response.status_text = "OK";
				response.headers["Content-Type"] = "text/html";
				std::stringstream ss1;
				ss1 << path.size();
				response.headers["Content-Length"] = ss1.str();
				if (request.headers.count("Connection"))
				{
					if (request.headers.find("Connection")->second == "close")
					{
						response.headers["Connection"] = "close";
					}
					else 
					{
						response.headers["Connection"] = "keep-alive";
					}
				}
				else 
				{
					response.headers["Connection"] = "keep-alive";
				}
				response.body = path;
				return (response);
			}
			//Once we obtain the path of the file: we open it and we get all the content
			std::string		fileContent = getFileContent(path);
			//We setup status code and text
			response.status_code = 200;
			response.status_text = "OK";
			//We declare Content-Length header
			std::stringstream ss;
			ss << fileContent.size();
			response.headers["Content-Length"] = ss.str();
			//We declare Content-Type (MIME-type)
			response.headers["Content-Type"] = getMimeType(path);
			//We check Connection header from request
			if (request.headers.count("Connection"))
			{
				if (request.headers.find("Connection")->second == "close")
				{
					response.headers["Connection"] = "close";
				}
				else 
				{
					response.headers["Connection"] = "keep-alive";
				}
			}
			else 
			{
				response.headers["Connection"] = "keep-alive";
			}
			//Now we save the fileContent as the body and we return the request
		response.body = fileContent;
		return (response);
	}

	//3. method POST
	else if (request.method == "POST")
	{
		std::string newUri = request.uri;
		if (newUri.find("?") != std::string::npos)
		{
			newUri = newUri.substr(0, newUri.find("?"));
		}
		
		if (!location.upload_enable)
		{
			return (ErrorHandler::generateHttpResponse(403, server));
		}
		if (!request.headers.count("Content-Type"))
		{
			return (ErrorHandler::generateHttpResponse(400, server));
		}
		if (request.body.size() > (size_t)location.client_max_body_size)
		{
			return (ErrorHandler::generateHttpResponse(413, server));
		}

		std::string uploadDir = (!location.upload_store.empty()) ? location.upload_store : location.root;
		if (!Utils::ends_with(uploadDir, "/"))
		{
			uploadDir += "/";
		}

		std::string relUri = newUri;
		if (relUri.find(location.locationPath) == 0)
		{
			relUri = relUri.substr(location.locationPath.size());
		}
		while (!relUri.empty() && relUri[0] == '/')
		{
			relUri = relUri.substr(1);
		}

		std::string uploadPath;
		if (relUri.empty())
		{
			std::string mime = request.headers.find("Content-Type")->second;
			uploadPath = uploadDir + generateFilename(mime);
		}
		else
		{
			uploadPath = uploadDir + relUri;
		}

		// Now we create the file and write all the request body in it
		if (!Utils::fileExists(uploadPath))
		{
			std::ofstream	file(uploadPath.c_str(), std::ios::binary);
			if (!file.is_open())
			{
				return (ErrorHandler::generateHttpResponse(500, server));
			}
			file.write(request.body.c_str(), request.body.size());
			file.close();
			response.status_code = 201;
			response.status_text = "Created";
		}
		else 
		{
			if (access(uploadPath.c_str(), W_OK) != 0)
			{
				return (ErrorHandler::generateHttpResponse(403, server));
			}
			std::ofstream file;
			file.open(uploadPath.c_str(), std::ios::out | std::ios::trunc | std::ios::binary);
			if (!file.is_open())
			{
				return (ErrorHandler::generateHttpResponse(500, server));
			}
			file.write(request.body.c_str(), request.body.size());
			file.close();
			response.status_code = 200;
			response.status_text = "OK";
		}

		// Set response headers
		response.headers["Content-Type"] = "text/plain";
		response.body = "File uploaded successfully\n";
		std::ostringstream oss;
		oss << response.body.size();
		response.headers["Content-Length"] = oss.str();
		if (request.headers.count("Connection"))
		{
			if (request.headers.find("Connection")->second == "close")
			{
				response.headers["Connection"] = "close";
			}
			else 
			{
				response.headers["Connection"] = "keep-alive";
			}
		}
		else 
		{
			response.headers["Connection"] = "keep-alive";
		}
		return (response);
	}
	
	//4. method DELETE
	else 
	{
		std::string newUri = request.uri;
		if (newUri.find("?") != std::string::npos)
		{
			newUri = newUri.substr(0, newUri.find("?"));
		}

		std::string deleteDir = (!location.upload_store.empty()) ? location.upload_store : location.root;
		if (!Utils::ends_with(deleteDir, "/"))
		{
			deleteDir += "/";
		}
		std::string relUri = newUri;
		if (relUri.find(location.locationPath) == 0)
		{
			relUri = relUri.substr(location.locationPath.size());
		}
		while (!relUri.empty() && relUri[0] == '/')
		{
			relUri = relUri.substr(1);
		}
		std::string path;
		if (!location.upload_store.empty() && !relUri.empty() && Utils::fileExists(deleteDir + relUri))
		{
			path = deleteDir + relUri;
		}
		else
		{
			path = getPath(location.exactMatch, request.method, server.root, location.root, location.locationPath, newUri, location.index, location.autoindex);
		}

		if (path == "404" || !Utils::fileExists(path))
		{
			return (ErrorHandler::generateHttpResponse(404, server));
		}
		if (Utils::isDirectory(path))
		{
			return (ErrorHandler::generateHttpResponse(400, server));
		}
		if (access(path.c_str(), W_OK) != 0)
		{
			return (ErrorHandler::generateHttpResponse(403, server));
		}
		if (std::remove(path.c_str()) == 0)
		{
			response.status_code = 204;
			response.status_text = "No Content";
			if (request.headers.count("Connection"))
			{
				if (request.headers.at("Connection") == "close")
				{
					response.headers["Connection"] = "close";
				}
				else 
				{
					response.headers["Connection"] = "keep-alive";
				}
			}
			else 
			{
				response.headers["Connection"] = "keep-alive";
			}
			return (response);
		}
		else 
		{
			return (ErrorHandler::generateHttpResponse(500, server));
		}
	}
	return (response);

}

