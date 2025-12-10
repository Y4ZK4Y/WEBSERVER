#pragma once
#include <string>
#include <sstream>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <vector>
#include <cstdio>
#include <cerrno>
#include <cstring>
#include <unistd.h>
#include "Config/ServerConfig.hpp"

class Client;

class ParseHTTP
{

	public:
		ParseHTTP();
		~ParseHTTP();
	
		void setClient(Client *client);
		void setConfig(const ServerConfig *config);
		std::string getResponse() const;
		void parse_http_request();	
		void send_error_response(int status_code, const std::string &message);
		
	private:
		Client* client;
		const ServerConfig *config;
		const RouteConfig *currentRoute;
		std::string response;
		std::string method;
		std::string path;
		std::string version;

		std::string getMimeType(const std::string &path);
		std::string urlConverter(const std::string &str);
		std::string sanitizePath(const std::string &path);
		const RouteConfig* findRoute(const std::string &path);
		bool methodInConfig(const std::string &method, const RouteConfig *route);
		int isExecutable(const std::string& path);

		void handleCGI();
		void handleGET();
		void handlePOST(const std::string& http_request, size_t line_end);
		std::string parseBlock(const std::string &body, const std::string &boundary, const std::string &uploadPath);
		void handleDELETE();

};
