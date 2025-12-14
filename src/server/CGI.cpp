#include "WebServer/CGI.hpp"
#include <unistd.h>
#include <stdexcept>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <sys/wait.h>
#include "Config/Logger.hpp"

CGI::CGI(std::string cgi_path, std::string script_path, std::string request, std::string method, std::string query_string)
{
	_startTime = std::time(nullptr);
	_body = getBodyFromRequest(request);
	_headers = getHeadersFromRequest(request);
	if (pipe(_pipeIn) == -1 || pipe(_pipeOut) == -1)
	{
		throw std::runtime_error("Failed to create pipes");
	}
	
	_pid = fork();
	if (_pid == -1)
	{
		throw std::runtime_error("Failed to fork");
	}
	if (_pid == 0)
	{
		createEnvironmentMap(method, query_string);
		_env = createCGIEnv();
		close(_pipeIn[1]);
		close(_pipeOut[0]);
		dup2(_pipeIn[0], STDIN_FILENO);
		dup2(_pipeOut[1], STDOUT_FILENO);
		close(_pipeIn[0]);
		close(_pipeOut[1]);
		
		char **argv = new char*[3];
		argv[0] = new char[cgi_path.size() + 1];
		std::strcpy(argv[0], cgi_path.c_str());
		argv[1] = new char[script_path.size() + 1];
		std::strcpy(argv[1], script_path.c_str());
		argv[2] = NULL;
		
		execve(cgi_path.c_str(), argv, _env);
		exit(1);
	}
	else
	{
		close(_pipeIn[0]);
		close(_pipeOut[1]);

		pollfd writePfd;
		writePfd.fd = _pipeIn[1];
		writePfd.events = POLLOUT;
		writePfd.revents = 0;
		_pendingFDs.push(writePfd);
		
		pollfd readPfd;
		readPfd.fd = _pipeOut[0];
		readPfd.events = POLLIN;
		readPfd.revents = 0;
		_pendingFDs.push(readPfd);
	}
	_cgiActive = true;
}

CGI::CGI() : _cgiActive {false}
{
}

std::string CGI::getBodyFromRequest(const std::string& request)
{
	size_t pos = request.find("\r\n\r\n");
	if (pos != std::string::npos)
	{
		return request.substr(pos + 4);
	}
	return "";
}

std::string CGI::getHeadersFromRequest(const std::string& request)
{
	size_t pos = request.find("\r\n\r\n");
	if (pos != std::string::npos)
	{
		return request.substr(0, pos + 4);
	}
	return "";
}

void CGI::createEnvironmentMap(std::string method, std::string query_string)
{
	_envMap["REQUEST_METHOD"] = method;
	_envMap["QUERY_STRING"] = query_string;
	_envMap["SERVER_PROTOCOL"] = "HTTP/1.1";
	if (method == "POST")
		_envMap["CONTENT_LENGTH"] = std::to_string(_body.size());
	else
		_envMap["CONTENT_LENGTH"] = "0";
	
	std::unordered_map<std::string, std::string> headerMap = makeKeyValues(_headers);
	for (std::unordered_map<std::string, std::string>::iterator it = headerMap.begin(); it != headerMap.end(); ++it)
	{
		std::string key = it->first;
		if (key == "Content-Length")
			continue;
		else if (key == "Content-Type")
		{
			_envMap["CONTENT_TYPE"] = it->second;
			continue;
		}
		std::string value = it->second;
		std::transform(key.begin(), key.end(), key.begin(), ::toupper);
		std::replace(key.begin(), key.end(), '-', '_');
		_envMap["HTTP_" + key] = value;
	}
}

char** CGI::createCGIEnv()
{
	char** env = new char*[_envMap.size() + 1];
	size_t i = 0;
	for (std::unordered_map<std::string, std::string>::iterator it = _envMap.begin(); it != _envMap.end(); ++it)
	{
		std::string entry = it->first + "=" + it->second;
		env[i] = new char[entry.size() + 1];
		std::strcpy(env[i], entry.c_str());
		i++;
	}
	env[i] = NULL;
	return env;
}

std::unordered_map<std::string, std::string> CGI::makeKeyValues(std::string headers)
{
	std::unordered_map<std::string, std::string> headerMap;
	size_t pos = 0;
	while (true)
	{
		size_t line_end = headers.find("\r\n", pos);
		if (line_end == std::string::npos)
			break;
		std::string line = headers.substr(pos, line_end - pos);
		size_t colon = line.find(": ");
		if (colon != std::string::npos)
		{
			std::string key = line.substr(0, colon);
			std::string value = line.substr(colon + 2);
			headerMap[key] = value;
		}
		pos = line_end + 2;
	}
	return headerMap;
}

std::queue<pollfd>& CGI::getPendingFDs()
{
	return _pendingFDs;
}

std::queue<int>& CGI::getRemoveFDs()
{
	return _removeFDs;
}

void CGI::writePipe()
{
	ssize_t written = write(_pipeIn[1], _body.c_str() + _bytesWritten, _body.size() - _bytesWritten);
	_bytesWritten += written;
	if (_bytesWritten >= _body.size())
	{
		close(_pipeIn[1]);
		_removeFDs.push(_pipeIn[1]);
		_pipeIn[1] = -1;
	}
	else if (written == 0)
	{
		close(_pipeIn[1]);
		_removeFDs.push(_pipeIn[1]);
		_pipeIn[1] = -1;
	}
	else if (written < 0)
	{
		Logger::log(LOG_ERROR, "Error writing to CGI pipe");
		throw std::runtime_error("Error writing to CGI pipe");
	}
}

void CGI::readPipe()
{
	char buffer[1024];
	size_t readBytes = read(_pipeOut[0], buffer, sizeof(buffer));
	if (readBytes > 0)
	{
		_response.append(buffer, readBytes);
		_bytesRead += readBytes;
		int status;
		pid_t result = waitpid(_pid, &status, 0);
		if (result == _pid)
		{
			close(_pipeOut[0]);
			validateResponse();
			_responseComplete = true;
			_removeFDs.push(_pipeOut[0]);
			_pipeOut[0] = -1;
		}
	}
	else if (readBytes == 0)
	{
		_responseComplete = true;
		close(_pipeOut[0]);
		_removeFDs.push(_pipeOut[0]);
		_pipeOut[0] = -1;
	}
	else
	{
		Logger::log(LOG_ERROR, "Error reading from CGI pipe");
		throw std::runtime_error("Error reading from CGI pipe");
	}
}

void CGI::validateResponse()
{
	// Ensure we have at least the required HTTP headers
	if (_response.find("Status:") == std::string::npos)
		_response = "HTTP/1.1 200 OK\r\n" + _response;

	if (_response.find("Content-Type:") == std::string::npos)
		_response = "Content-Type: text/html\r\n" + _response;


	if (_response.find("\n\n") != std::string::npos)
	{
		size_t pos = 0;
		while ((pos = _response.find("\n\n", pos)) != std::string::npos)
		{
			_response.replace(pos, 2, "\r\n\r\n");
			pos += 4;
		}
	}
	if (_response.find("\r\n\r\n") == std::string::npos)
	{
		_response += "\r\n\r\n";
	}

	if (_response.find("Content-Length:") == std::string::npos) {
		size_t header_end = _response.find("\r\n\r\n");
		if (header_end != std::string::npos)
		{
			std::string body = _response.substr(header_end + 4);
		}
	}
}

bool CGI::isResponseComplete() const
{
	return _responseComplete;
}

std::string CGI::getResponse() const
{
	return _response;
}

bool CGI::isCgiActive() const
{
	return _cgiActive;
}

void CGI::timedOut()
{
	Logger::log(LOG_INFO, "CGI: Process with PID " + std::to_string(_pid) + " terminated.");
	_cgiActive = false;
	if (_pipeIn[1] != -1)
	{
		_removeFDs.push(_pipeIn[1]);
		close(_pipeIn[1]);
		_pipeIn[1] = -1;
	}
	if (_pipeOut[0] != -1)
	{
		_removeFDs.push(_pipeOut[0]);
		close(_pipeOut[0]);
		_pipeOut[0] = -1;
	}
	kill(_pid, SIGKILL);	
}