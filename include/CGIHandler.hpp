#pragma once

#include "webserv.hpp"

class CGIHandler
{
	private:
		std::vector<std::string>				env;

		void				setupEnv(const HttpRequest &request, const LocationConfig &location);

	public:
		CGIHandler();
		CGIHandler(const CGIHandler &original);
		CGIHandler	&operator=(const CGIHandler &original);
		~CGIHandler();

		int					startCGI(const HttpRequest &request, const LocationConfig &location, const ServerConfig &server, pid_t &outPid, int &outReadPipeFd);
		static HttpResponse	parseOutput(const std::string &cgiOutput, const HttpRequest &request, const ServerConfig &server);
};
