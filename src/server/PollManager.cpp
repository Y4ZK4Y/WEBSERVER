#include "WebServer/PollManager.hpp"
#include <iostream>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <bitset>
#include <signal.h>
#include "Config/Logger.hpp"

void PollManager::addListeningSocket(ListeningSocket* socket)
{
	int fd = socket->getFd();
	_sockets[fd] = socket;
	pollfd pfd;
	pfd.fd = fd;
	pfd.events = POLLIN;
	pfd.revents = 0;
	_pollfds.push_back(pfd);
}

void PollManager::addClient(const Client& client)
{
	int fd = client.getFd();
	_clients[fd] = std::make_shared<Client>(client);
	pollfd pfd;
	pfd.fd = fd;
	pfd.events = POLLIN;
	pfd.revents = 0;
	_pollfds.push_back(pfd);
}

void PollManager::acceptConnection(int fd)
{
	int clientFd = accept(fd, nullptr, nullptr);
	if (clientFd < 0)
	{
		return;
	}
	Logger::log(LOG_INFO, "Accepted new connection, fd: " + std::to_string(clientFd));
	addClient(Client(clientFd, _sockets[fd]->getConfig()));
}

void PollManager::readClient(int fd)
{
	auto it = _clients.find(fd);
	if (it != _clients.end())
	{
		Client& client = *(it->second);
		client.setLastActivityTime();
		if (client.getFd() == fd)
		{
			if (!client.getRequestComplete())
				client.buildRequest();
			if (client.getRequestComplete())
			{
				unregisterForRead(fd);
				client.buildResponse();
				if (client.getResponseBuilt())
				{
					registerForWrite(fd);
				}
			}
		}
		else {
			client.cgiRead();
			if (client.getCgiProcess().isResponseComplete())
			{
				client.setResponse(client.getCgiProcess().getResponse());
				registerForWrite(client.getFd());
				unregisterForRead(fd);
			}
		}
	}
}

void PollManager::unregisterForRead(int fd)
{
	for (size_t i = 0; i < _pollfds.size(); ++i)
	{
		if (_pollfds[i].fd == fd)
		{
			_pollfds[i].events &= ~POLLIN;
			return;
		}
	}
}

void PollManager::registerForRead(int fd)
{
	for (size_t i = 0; i < _pollfds.size(); ++i)
	{
		if (_pollfds[i].fd == fd)
		{
			_pollfds[i].events |= POLLIN;
			return;
		}
	}
}

void PollManager::registerForWrite(int fd)
{
	for (size_t i = 0; i < _pollfds.size(); ++i)
	{
		if (_pollfds[i].fd == fd)
		{
			_pollfds[i].events |= POLLOUT;
			return;
		}
	}
}

void PollManager::unregisterForWrite(int fd)
{
	for (size_t i = 0; i < _pollfds.size(); ++i)
	{
		if (_pollfds[i].fd == fd)
		{
			_pollfds[i].events &= ~POLLOUT;
			return;
		}
	}
}

void PollManager::removeClient(int fd)
{
	_clients.erase(fd);

	for (auto it = _pollfds.begin(); it != _pollfds.end(); ++it)
	{
		if (it->fd == fd)
		{
			_pollfds.erase(it);
			break;
		}
	}
	close(fd);
}

void PollManager::handleAddQueue()
{
	std::vector<std::pair<int, std::shared_ptr<Client>>> newFds;

	for (auto it = _clients.begin(); it != _clients.end(); ++it)
	{
		if (it->second->getCgiProcess().isCgiActive())
		{
			std::queue<pollfd>& queue = it->second->getAddQueue();
			while (!queue.empty())
			{
				newFds.push_back({queue.front().fd, it->second});
				_pollfds.push_back(queue.front());
				queue.pop();
			}
		}
	}

	for (size_t i = 0; i < newFds.size(); ++i)
	{
		_clients[newFds[i].first] = newFds[i].second;
	}
}

void PollManager::handleRemoveQueue()
{
	std::vector<int> fdsToRemove;

	for (auto it = _clients.begin(); it != _clients.end(); ++it)
	{
		if (it->second->getCgiProcess().isCgiActive() == false)
			continue;
		std::queue<int>& removeQueue = it->second->getRemoveQueue();
		while (!removeQueue.empty())
		{
			fdsToRemove.push_back(removeQueue.front());
			removeQueue.pop();
		}
	}

	for (size_t i = 0; i < fdsToRemove.size(); ++i)
	{
		removeClient(fdsToRemove[i]);
	}
}

void PollManager::printPollFDs()
{
	std::cout << "Current pollfds:" << std::endl;
	for (size_t i = 0; i < _pollfds.size(); ++i)
	{
		std::cout << "fd: " << _pollfds[i].fd << std::endl;
	}
}

void PollManager::handleTimeout()
{
	for (auto it = _clients.begin(); it != _clients.end(); ++it)
	{
		Client& client = *(it->second);
		std::time_t currentTime = std::time(nullptr);
		double elapsed;
		if (client.getCgiProcess().isCgiActive() == false)
		{	
			elapsed = std::difftime(currentTime, client.getLastActivityTime());
			if (elapsed > 10)
			{
				client.reset();
				removeClient(client.getFd());
			}
			continue;
		}
		elapsed = std::difftime(currentTime, client.getCgiProcess().getStartTime());
		if (elapsed > 5) // 5 seconds timeout
		{
			client.getCgiProcess().timedOut();
			removeCgiFds(client.getCgiProcess());
			ParseHTTP parser;
			parser.setConfig(client.getConfig());
			parser.send_error_response(504, "Gateway Timeout");
			client.setResponse(parser.getResponse());
			registerForWrite(client.getFd());
		}
	}
}

void PollManager::removeCgiFds(CGI& cgi)
{
	std::queue<int>& removeQueue = cgi.getRemoveFDs();
	while (!removeQueue.empty())
	{
		for (int i = 0; i < (int)_pollfds.size(); ++i)
		{
			if (_pollfds[i].fd == removeQueue.front())
			{
				_pollfds.erase(_pollfds.begin() + i);
				break;
			}
		}
		_clients.erase(removeQueue.front());
		removeQueue.pop();
	}
}

void PollManager::handlePollin(int fd)
{
	if (_sockets.count(fd))
	{
		acceptConnection(fd);
	}
	else if (_clients.count(fd))
	{
		readClient(fd);
	}
}

void PollManager::handlePollout(int fd)
{
	if (_clients.count(fd))
	{
		Client& client = *_clients[fd];
		client.setLastActivityTime();
		if (fd == client.getFd())
		{
			client.sendResponse();
			if (client.getResponseComplete())
			{
				unregisterForWrite(fd);
				client.reset();
				removeClient(fd);
			}
		}
		else
		{
			client.cgiWrite();
		}
	}
}

void PollManager::run()
{
	while (true)
	{
		int events = poll(&_pollfds[0], _pollfds.size(), 5000);
		if (events > 0)
		{
			for (size_t i = 0; i < _pollfds.size(); ++i)
			{
				try
				{
					if (_pollfds[i].revents & POLLIN)
					{
						handlePollin(_pollfds[i].fd);
					}
					if (_pollfds[i].revents & POLLOUT)
					{
						handlePollout(_pollfds[i].fd);
					}
				}
				catch(const std::exception& e)
				{
					removeClient(_pollfds[i].fd);
				}
			}
		}
		else
		{
			handleTimeout();
		}
		handleAddQueue();
		handleRemoveQueue();
	}
}