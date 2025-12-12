#include "WebServer/ParseHTTP.hpp"
#include "WebServer/Client.hpp"
#include <sys/stat.h>
#include <algorithm>

ParseHTTP::ParseHTTP()
{
	
}

ParseHTTP::~ParseHTTP()
{
	
}

void ParseHTTP::setClient(Client *client)
{
	this->client = client;
}

void ParseHTTP::setConfig(const ServerConfig *config)
{
	this->config = config;
}

std::string ParseHTTP::getResponse() const
{
	return (response);
}

std::string ParseHTTP::getMimeType(const std::string &path)
{
	size_t position = path.find_last_of('.');
	if (position == std::string::npos)
		return "application/octet-stream";
	
	std::string ext = path.substr(position + 1);
	for (size_t i = 0; i < ext.size(); i++)
		ext[i] = std::tolower(ext[i]);
	
	if (ext == "html" || ext == "htm") 
		return "text/html";
	if (ext == "css") 
		return "text/css";
	if (ext == "js") 
		return "application/javascript";
	if (ext == "json") 
		return "application/json";
	if (ext == "xml") 
		return "application/xml";
	if (ext == "txt") 
		return "text/plain";
	if (ext == "jpg" || ext == "jpeg") 
		return "image/jpeg";
	if (ext == "png") 
		return "image/png";
	if (ext == "gif") 
		return "image/gif";
	if (ext == "ico") 
		return "image/x-icon";
	if (ext == "webp") 
		return "image/webp";
	if (ext == "pdf")
		return "application/pdf";
	if (ext == "zip") 
		return "application/zip";
	if (ext == "mp4") 
		return "video/mp4";
	if (ext == "webm") 
		return "video/webm";	
	if (ext == "wav") 
		return "audio/wav";
	
	return ("application/octet-stream");
}

std::string ParseHTTP::urlConverter(const std::string &str)
{
	std::string decoded;
	size_t i = 0;
	
	while (i < str.length())
	{
		if (str[i] == '%' && i + 2 < str.length())
		{
			try
			{
				std::string hex = str.substr(i + 1, 2);
				char ch = static_cast<char>(std::stoi(hex, nullptr, 16));
				decoded = decoded + ch;
				i = i + 3;
			}
			catch (const std::exception& e)
			{
				send_error_response(400, "Bad Request");
			}
		}
		else
		{
			decoded = decoded + str[i];
			i++;
		}
	}
	return (decoded);
}

std::string ParseHTTP::sanitizePath(const std::string &path)
{
	std::string sanitized = urlConverter(path);
	
	size_t pos;
	while ((pos = sanitized.find("..")) != std::string::npos)
	{
		sanitized.erase(pos, 2);
	}
	
	while ((pos = sanitized.find("//")) != std::string::npos)
	{
		sanitized.erase(pos, 1);
	}
	
	if (sanitized.empty() || sanitized[0] != '/')
		sanitized = "/" + sanitized;
	
	return (sanitized);
}


const RouteConfig *ParseHTTP::findRoute(const std::string &path)
{
	if (!config)
		return nullptr;
	
	const RouteConfig *bestMatch = nullptr; 
	size_t longestMatch = 0;
	
	for (const RouteConfig &route : config->routes)
	{
		if (path.find(route.path) == 0)
		{
			size_t route_len = route.path.length();
			if (route_len > path.length())
				continue;
	
			if (path.length() == route_len || route.path[route_len - 1] == '/' || path[route_len] == '/')
			{
				if (route_len > longestMatch)
				{
					bestMatch = &route;
					longestMatch = route_len;
				}
			}
		}
	}
	return (bestMatch);
}

bool ParseHTTP::methodInConfig(const std::string &method, const RouteConfig *route)
{
	if (!route)
		return false;
	for (const std::string &allowed : route->methods)
	{
		if (allowed == method)
			return true;
	}
	return (false);
}

void ParseHTTP::parse_http_request()
{
	if (!config)
	{
		send_error_response(500, "Internal Server Error");
		return;
	}
	std::string http_request = client->getRequest();

	size_t status_line_end = http_request.find("\r\n");
	if (status_line_end == std::string::npos)
	{
		send_error_response(400, "Bad Request");
		return;
	}
	
	std::string status_line = http_request.substr(0, status_line_end);
	std::istringstream iss(status_line);
	std::string method1, path1, version1;
	if (!(iss >> method1 >> path1 >> version1))
	{
		send_error_response(400, "Bad Request");
		return;
	}

	method = method1;  
	path = sanitizePath(path1);
	version = version1;   
	
	if (path.empty() || path[0] != '/')
	{
		send_error_response(400, "Bad Request");
		return ;
	}

	if (version != "HTTP/1.1")
	{
		send_error_response(505, "Version Not Supported");
		return ;
	}
	
	const RouteConfig *route = findRoute(path);
	
	if (!route)
	{
		send_error_response(404, "Not Found");
		return ;
	}
	
	if (!methodInConfig(method, route))
	{
		send_error_response(405, "Method Not Allowed");
		return ;
	}
	
	if (!route->redirectTo.empty())
	{
        response =
            "HTTP/1.1 301 Moved Permanently\r\n"
            "Location: " + route->redirectTo + "\r\n"
            "Content-Length: 0\r\n"
            "\r\n";
        return;
	}
	
	currentRoute = route;

	if (!currentRoute->cgiPath.empty())
	{
		handleCGI();
		return;
	}
	
	if (method == "GET")
	{
		handleGET();
	}
	
	else if (method == "POST")
	{
		handlePOST(http_request, status_line_end);
	}
	else if (method == "DELETE")
	{
		handleDELETE();
	}
	else
	{
		send_error_response(405, "Method Not Allowed");
		return ;
	}
}

void ParseHTTP::handleGET()
{	
	if (path == "/")
	{
		if (!config->index.empty())
			path = "/" + config->index;
		else
			path = "/index.html";
	}

	std::string file_path;
	
	if (!currentRoute->uploadPath.empty() && path.find(currentRoute->path) == 0)
	{
		std::string relative = path.substr(currentRoute->path.length());
		file_path = config->root + "/" + currentRoute->uploadPath + relative;
	}
	else
	{
		file_path = config->root + path;
	}
	
	struct stat path_stat;
	if (stat(file_path.c_str(), &path_stat) == 0 && S_ISDIR(path_stat.st_mode))
	{
		if (file_path.back() != '/')
			file_path = file_path + "/";
			
		std::string index_path = file_path + config->index;

		if (access(index_path.c_str(), F_OK) == 0) 
			file_path = index_path;
		else 
		{
			send_error_response(403, "Forbidden");
			return ;
		}
	}
	
	std::ifstream file(file_path, std::ios::binary);
	
	if (!file)
	{
		send_error_response(404, "Not Found");
		return;
	}
	
	std::stringstream get_content;
	get_content << file.rdbuf();
	std::string content = get_content.str();
	
	std::string mime_type = getMimeType(file_path);

	response =
		"HTTP/1.1 200 OK\r\n"
		"Content-Type: " + mime_type + "\r\n"
		"Content-Length: " + std::to_string(content.size()) + "\r\n"
		"\r\n" + 
		content;
}

void ParseHTTP::handlePOST(const std::string &http_request, size_t status_line_end)
{	
	size_t end_of_header = http_request.find("\r\n\r\n");
	if (end_of_header == std::string::npos)
	{
		send_error_response(400, "Bad Request");
		return;
	}
	
	std::string header = http_request.substr(status_line_end + 2, end_of_header - (status_line_end + 2));
	std::string boundary;
	int content_length = 0;
	bool plain_text = false;

	std::istringstream header_stream(header);
	std::string copy_header;
	while (std::getline(header_stream, copy_header))
	{
		if (copy_header.back() == '\r')
			copy_header.pop_back();
		if (copy_header.find("Content-Length") == 0)
		{
			try 
			{
				content_length = std::stoi(copy_header.substr(15));
				if (content_length > std::stoi(config->bodyLimit))
					{
						send_error_response(413, "Payload Too Large");
						return ;
					}
			}
			catch (const std::exception& e)
			{
				send_error_response(400, "Bad Request");
			}
		}
		else if (copy_header.find("Content-Type: multipart/form-data; boundary=") == 0)
		{
			size_t boundary_position = copy_header.find("boundary=");
			if (boundary_position != std::string::npos)
			{
				boundary = "--" + copy_header.substr(boundary_position + 9);
			}
		}
		else if (copy_header.find("Content-Type: text/plain") == 0)
			plain_text = true;
	}
	if (plain_text)
	{
		response = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n";
    	return ;
	}
	if (boundary.empty())
	{
		send_error_response(400, "Bad Request");
		return ;
	}
	
	if (currentRoute->uploadPath.empty())
	{
		send_error_response(500, "Internal Server Error");
		return ; 
	}
	
	std::string post_body = http_request.substr(end_of_header + 4);
		
	std::string uploaded_file = parseBlock(post_body, boundary, currentRoute->uploadPath);
	
	if (uploaded_file.empty())
	{
		send_error_response(400, "Bad Request");
		return ;
	}
	
	std::string body =
        "<html><head><title>Upload Success</title></head><body>"
        "<h1>Upload Successful</h1>"
        "<p>Uploaded file: " + uploaded_file + "</p>"
        "<a href=\"/file-upload.html\">Back to File Uploads</a>"
        "</body></html>";

    response =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html\r\n"
        "Content-Length: " + std::to_string(body.size()) + "\r\n"
        "\r\n" + 
		body;

}

std::string ParseHTTP::parseBlock(const std::string &body, const std::string &boundary, const std::string &uploadPath)
{
   	size_t first_boundary_in_body = body.find("\r\n\r\n");
    if (first_boundary_in_body == std::string::npos)
	{
        return "";
	}

	size_t filename_position = body.find("filename=\"");
    if (filename_position == std::string::npos)
    {
		return ""; 
	}
    filename_position = filename_position + 10;
    size_t end_of_filename = body.find("\"", filename_position);
    if (end_of_filename == std::string::npos)
        return "";

    std::string filename = body.substr(filename_position, end_of_filename - filename_position);

    size_t last_slash = filename.find_last_of("/\\");
    if (last_slash != std::string::npos)
        filename = filename.substr(last_slash + 1);

    if (filename.empty())
        return "";

    size_t content_start = first_boundary_in_body + 4;

    size_t next_boundary = body.find("\r\n" + boundary, content_start);
    if (next_boundary == std::string::npos)
        next_boundary = body.length();

    std::string file_content = body.substr(content_start, next_boundary - content_start);

	std::string add_root;
	add_root = config->root + "/" + uploadPath;
	
    struct stat st;
	 if (stat(add_root.c_str(), &st) != 0)
        mkdir(add_root.c_str(), 0755);
		
    std::string full_path = add_root + "/" + filename;

    std::ofstream out_file(full_path, std::ios::binary);
    if (!out_file)
        return "";

    out_file.write(file_content.c_str(), file_content.size());
    out_file.close();

    return (filename); 
}

void ParseHTTP::handleDELETE()
{
	std::string relative = path.substr(currentRoute->path.length());
	std::string file_path = config->root + "/" + currentRoute->uploadPath + relative;
	
	struct stat file_check;
	if (stat(file_path.c_str(), &file_check) != 0)
	{
		send_error_response(404, "Not Found");
		return ;
	}

	if (S_ISDIR(file_check.st_mode))
	{
		send_error_response(403, "Forbidden");
		return ;
	}
	
	if (std::remove(file_path.c_str()) == 0)
	{
		std::string body = "<html><body><h1>Deleted " + path + "</h1><a href=\"/\">Back to home</a></body></html>";
		response =
			"HTTP/1.1 200 OK\r\n"
			"Content-Type: text/html\r\n"
			"Content-Length: " + std::to_string(body.size()) + "\r\n"
			"\r\n" +
			body;
	}
	else
	{
		send_error_response(500, "Internal Server Error");
	}
}


int ParseHTTP::isExecutable(const std::string& path)
{
	struct stat st;
	
	if (stat(path.c_str(), &st) != 0)
		return 404;	
	if (access(path.c_str(), X_OK) != 0)
		return 403;
	return 200;
}

void ParseHTTP::handleCGI()
{
	std::string cgi_path = currentRoute->cgiPath;
	std::string query_string;
	size_t query_pos = path.find('?');
	std::string script_path = config->root + path;
	if (query_pos != std::string::npos)
		query_string = path.substr(query_pos + 1);
	else
		query_string = "";
	if (path == "/cgi-bin" || path == "/cgi-bin/")
	{
		send_error_response(403, "Forbidden");
		return ;
	}
	int status = isExecutable(script_path);
	if (status != 200)
	{
		if (status == 404)
			send_error_response(404, "CGI script not found");
		else if (status == 403)
			send_error_response(403, "CGI script not executable");
		return ;
	}
	client->createCgiProcess(currentRoute->cgiPath, script_path, method, query_string);
}


void ParseHTTP::send_error_response(int status_code, const std::string &message)
{

	std::string body;
	if (config && config->errorPages.count(status_code) > 0)
	{
		std::string error_page = config->root + "/" + config->errorPages.at(status_code);
		std::ifstream error_file(error_page, std::ios::binary);
		if (error_file)
		{
			std::stringstream ss;
			ss << error_file.rdbuf();
			body = ss.str();
		}
	}
	response =
		"HTTP/1.1 " + std::to_string(status_code) + " " + message + "\r\n"
		"Content-Type: text/html\r\n"
		"Content-Length: " + std::to_string(body.size()) + "\r\n"
		"\r\n" + 
		body;
}

