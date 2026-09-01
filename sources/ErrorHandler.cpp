#include "../include/webserv.hpp"

ErrorHandler::ErrorHandler()
{

}

ErrorHandler::ErrorHandler(const ErrorHandler &original)
{
	*this = original;
}

ErrorHandler		&ErrorHandler::operator=(const ErrorHandler &original)
{
	if (this != &original)
	{

	}
	return (*this);

}

ErrorHandler::~ErrorHandler()
{

}


//Functions
HttpResponse	ErrorHandler::generateHttpResponse(int status_code, const ServerConfig &config)
{
	HttpResponse	response;

	response.status_code = status_code;
	response.status_text = Utils::statusText(status_code);

	// Check if there is an error page configured for this error in this server
	if (config.error_page.count(status_code))
	{
		std::string rootError = config.root;
		std::string errorPagePath = config.error_page.at(status_code);
		if (Utils::ends_with(rootError, "/"))
		{
			rootError = rootError.substr(0, rootError.length() - 1);
		}
		if (!errorPagePath.empty() && errorPagePath[0] == '/')
		{
			errorPagePath = errorPagePath.substr(1);
		}
		std::string path = rootError + "/" + errorPagePath;
		std::ifstream file(path.c_str());
		
		if (file.is_open())
		{
			std::stringstream buffer;
			buffer << file.rdbuf();
			response.body = buffer.str();
			response.headers["Content-Type"] = "text/html";
		}
		else 
		{
			std::ostringstream ss;
			ss << "<html><body><h1>" << status_code << " " << response.status_text << "</h1></body></html>";
			response.body = ss.str();
			response.headers["Content-Type"] = "text/html";
		}
	}
	else
	{
		std::ostringstream ss;
		ss << "<html><body><h1>" << status_code << " " << response.status_text << "</h1></body></html>";
		response.body = ss.str();
		response.headers["Content-Type"] = "text/html";
	}

	response.headers["Connection"] = "close";
	std::ostringstream oss;
	oss << response.body.size();
	response.headers["Content-Length"] = oss.str();

	return (response);
}
