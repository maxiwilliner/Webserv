#include "../include/webserv.hpp"

RequestParser::RequestParser()
{
	this->phase = REQUEST_LINE;
	this->isChunked = false;
	this->chunkPhase = CHUNK_SIZE_PHASE;
	this->expectedChunkSize = 0;
	this->currentChunkBytes = 0;
}

RequestParser::RequestParser(const RequestParser &original)
{
	this->phase = original.phase;
	this->request = original.request;
	this->isChunked = original.isChunked;
	this->chunkPhase = original.chunkPhase;
	this->expectedChunkSize = original.expectedChunkSize;
	this->currentChunkBytes = original.currentChunkBytes;
}

RequestParser::~RequestParser()
{
}

RequestParser	&RequestParser::operator=(const RequestParser &original)
{
	if (this != &original)
	{
		this->phase = original.phase;
		this->request = original.request;
		this->isChunked = original.isChunked;
		this->chunkPhase = original.chunkPhase;
		this->expectedChunkSize = original.expectedChunkSize;
		this->currentChunkBytes = original.currentChunkBytes;
	}
	return (*this);
}

//Functions
bool			RequestParser::isRequestComplete() const
{
	return (this->phase == COMPLETE);
}

HttpRequest		RequestParser::getHttpRequest() const 
{
	return (this->request);
}

void			RequestParser::parseLine(const std::string &line)
{
	if (this->phase == REQUEST_LINE)
	{
		// Skip empty lines before request line
		if (line.empty() || line == "\r" || line == "\n" || line == "\r\n")
		{
			return;
		}

		std::vector<std::string>	firstLine = Utils::splitBySpaces(line);
		if (firstLine.size() != 3)
		{
			this->phase = COMPLETE;
			this->request.error = 400;
			return;
		}

		// Check method
		if (firstLine[0] != "GET" && firstLine[0] != "POST" && firstLine[0] != "DELETE")
		{
			this->phase = COMPLETE;
			this->request.error = 405;
			return;
		}
		this->request.method = firstLine[0];
		this->request.uri = firstLine[1];

		// Check URI
		if (this->request.uri.empty() || this->request.uri[0] != '/')
		{
			this->phase = COMPLETE;
			this->request.error = 400;
			return;
		}
		if (this->request.uri.find("/../") != std::string::npos || Utils::ends_with(this->request.uri, "/..") || (this->request.uri.size() >= 2 && this->request.uri[0] == '.' && this->request.uri[1] == '.'))
		{
			this->phase = COMPLETE;
			this->request.error = 400;
			return;
		}

		// Check HTTP version
		std::string version = firstLine[2];
		while (!version.empty() && (version[version.size() - 1] == '\r' || version[version.size() - 1] == '\n'))
		{
			version.erase(version.size() - 1);
		}
		if (version != "HTTP/1.1" && version != "HTTP/1.0")
		{
			this->phase = COMPLETE;
			this->request.error = 400;
			return;
		}
		this->request.version = version;
		this->phase = HEADERS;
	}
	else if (this->phase == HEADERS)
	{
		// End of headers
		if (line == "\r" || line == "\n" || line == "\r\n" || line.empty())
		{
			// Host header is mandatory in HTTP/1.1
			if (!this->request.headers.count("Host"))
			{
				this->phase = COMPLETE;
				this->request.error = 400;
				return;
			}
			// Strip port from Host if present
			size_t colonHost = this->request.headers["Host"].find(':');
			if (colonHost != std::string::npos)
			{
				this->request.headers["Host"] = this->request.headers["Host"].substr(0, colonHost);
			}

			if (!this->request.headers.count("Connection"))
			{
				this->request.headers["Connection"] = "keep-alive";
			}

			// Check for Transfer-Encoding: chunked
			if (this->request.headers.count("Transfer-Encoding"))
			{
				std::string te = Utils::toLowerAlphaOnly(this->request.headers["Transfer-Encoding"]);
				if (te.find("chunked") != std::string::npos)
				{
					this->isChunked = true;
					this->chunkPhase = CHUNK_SIZE_PHASE;
					this->phase = BODY;
					this->request.body.clear();
					return;
				}
			}

			// Check Content-Length / Body
			if (this->request.headers.count("Content-Length"))
			{
				std::string clStr = this->request.headers["Content-Length"];
				for (size_t j = 0; j < clStr.size(); j++)
				{
					if (!std::isdigit(clStr[j]))
					{
						this->phase = COMPLETE;
						this->request.error = 400;
						return;
					}
				}
				this->request.body.clear();
				if (std::atoi(clStr.c_str()) == 0)
				{
					this->phase = COMPLETE;
				}
				else
				{
					this->phase = BODY;
				}
				return;
			}
			else if (this->request.method == "POST")
			{
				// POST without Content-Length and not chunked
				this->phase = COMPLETE;
				this->request.error = 400;
				return;
			}
			else
			{
				// No body expected for GET/DELETE without Content-Length
				this->phase = COMPLETE;
				return;
			}
		}

		// Parse single header line (Key: Value)
		std::string cleanLine = line;
		while (!cleanLine.empty() && (cleanLine[cleanLine.size() - 1] == '\r' || cleanLine[cleanLine.size() - 1] == '\n'))
		{
			cleanLine.erase(cleanLine.size() - 1);
		}

		size_t colonPos = cleanLine.find(':');
		if (colonPos == std::string::npos || colonPos == 0)
		{
			this->phase = COMPLETE;
			this->request.error = 400;
			return;
		}

		std::string headerName = cleanLine.substr(0, colonPos);
		std::string headerVal = cleanLine.substr(colonPos + 1);
		headerName = Utils::ft_trim(headerName);
		headerVal = Utils::ft_trim(headerVal);

		std::string lowerKey = Utils::toLowerAlphaOnly(headerName);
		if (lowerKey == "host")
		{
			this->request.headers["Host"] = headerVal;
		}
		else if (lowerKey == "user-agent")
		{
			this->request.headers["User-Agent"] = headerVal;
		}
		else if (lowerKey == "accept")
		{
			this->request.headers["Accept"] = headerVal;
		}
		else if (lowerKey == "content-length")
		{
			this->request.headers["Content-Length"] = headerVal;
		}
		else if (lowerKey == "content-type")
		{
			this->request.headers["Content-Type"] = headerVal;
		}
		else if (lowerKey == "transfer-encoding")
		{
			this->request.headers["Transfer-Encoding"] = headerVal;
		}
		else if (lowerKey == "connection")
		{
			this->request.headers["Connection"] = headerVal;
			if (headerVal != "keep-alive" && headerVal != "close")
			{
				this->phase = COMPLETE;
				this->request.error = 400;
				return;
			}
		}
		else if (lowerKey == "accept-encoding")
		{
			this->request.headers["Accept-Encoding"] = headerVal;
		}
		else
		{
			this->request.headers[headerName] = headerVal;
		}
	}
	else if (this->phase == BODY)
	{
		if (!this->isChunked)
		{
			int contentLength = std::atoi(this->request.headers["Content-Length"].c_str());
			this->request.body.append(line);
			if (int(this->request.body.size()) >= contentLength)
			{
				this->request.body = this->request.body.substr(0, contentLength);
				this->phase = COMPLETE;
			}
		}
		else
		{
			// Chunked decoding
			if (this->chunkPhase == CHUNK_SIZE_PHASE)
			{
				std::string cleanSize = line;
				while (!cleanSize.empty() && (cleanSize[cleanSize.size() - 1] == '\r' || cleanSize[cleanSize.size() - 1] == '\n'))
				{
					cleanSize.erase(cleanSize.size() - 1);
				}
				if (cleanSize.empty())
				{
					return;
				}
				char *endPtr;
				long sz = std::strtol(cleanSize.c_str(), &endPtr, 16);
				if (sz < 0)
				{
					this->phase = COMPLETE;
					this->request.error = 400;
					return;
				}
				if (sz == 0)
				{
					this->chunkPhase = CHUNK_TRAILER_PHASE;
				}
				else
				{
					this->expectedChunkSize = sz;
					this->currentChunkBytes = 0;
					this->chunkPhase = CHUNK_DATA_PHASE;
				}
			}
			else if (this->chunkPhase == CHUNK_DATA_PHASE)
			{
				this->request.body.append(line);
				this->currentChunkBytes += line.size();
				if (this->currentChunkBytes >= this->expectedChunkSize)
				{
					// Adjust length if extra trailing \r\n was appended
					if (this->currentChunkBytes > this->expectedChunkSize)
					{
						size_t excess = this->currentChunkBytes - this->expectedChunkSize;
						if (this->request.body.size() >= excess)
						{
							this->request.body.erase(this->request.body.size() - excess);
						}
					}
					this->chunkPhase = CHUNK_SIZE_PHASE;
				}
			}
			else if (this->chunkPhase == CHUNK_TRAILER_PHASE)
			{
				if (line == "\r" || line == "\n" || line == "\r\n" || line.empty())
				{
					this->phase = COMPLETE;
					std::ostringstream oss;
					oss << this->request.body.size();
					this->request.headers["Content-Length"] = oss.str();
				}
			}
		}
	}
}

