#define CPPHTTPLIB_OPENSSL_SUPPORT
#define _CRT_SECURE_NO_WARNINGS
#include <iostream>
#include <fstream>
#include <chrono>
#include <format>
#include <string>
#include <string_view>
#include <algorithm>
#include <cstdlib>
#include <nlohmann/json.hpp>
#include "httplib.h"

using json = nlohmann::json;


std::optional<std::string> getApiKey()
{
	const char* apiKey{ std::getenv("GEMINI_API_KEY") };
	if (!apiKey)
	{
		std::cout << "API key could not be found";
		return std::nullopt;
	}

	return std::string{ apiKey };
}

std::optional<json> getTasks()
{
	std::ifstream file("C:/C++ Projects/Lynx/tasks.json");
	if (!file.is_open())
	{
		return std::nullopt;
	}
	return json::parse(file);
}

std::vector<json> getSortedTasks(json tasksJson)
{
	std::vector<json> tasks{};
	for (const auto& e : tasksJson)
	{
		tasks.push_back(e);
	}

	std::sort(tasks.begin(), tasks.end(), [](const json& task1, const json& task2)
		{
			bool t1IsExam{ task1.contains("type") && task1["type"] == "exam" };
			bool t2IsExam{ task2.contains("type") && task2["type"] == "exam" };

			if (t1IsExam && !t2IsExam)
			{
				return false;
			}
			if (!t1IsExam && t2IsExam)
			{
				return true;
			}

			return task1["deadline"].get<std::string>() < task2["deadline"].get<std::string>();
		}
	);
	return tasks;
}

std::string getListString(std::vector<json> tasks)
{
	std::string listString{};
	for (const auto& e : tasks)
	{
		if (e.contains("status") && e["status"] == "done")
		{
			continue;
		}
		std::string status{ e.contains("status") ? e["status"].get<std::string>() : "N/A" };
		listString += "Task: " + e["task"].get<std::string>() +
			" | Category: " + e["category"].get<std::string>() +
			" | Deadline: " + e["deadline"].get<std::string>() +
			" | Status: " + status +
			" | Type: " + e["type"].get<std::string>() + '\n';
	}
	return listString;
}

int main()
{
	auto apiKey{ getApiKey() };
	if (!apiKey)
	{
		return 1;
	}

	auto tasksOpt{ getTasks() };
	if (!tasksOpt)
	{
		std::cout << "Failed to get tasks.\n";
		return 1;
	}

	json tasks{ *tasksOpt };
	std::vector<json> sortedTasks{ getSortedTasks(tasks) };
	std::string tasklist{ getListString(sortedTasks) };

	httplib::Client cli("https://generativelanguage.googleapis.com");
	httplib::Headers headers = {
		{ "x-goog-api-key", *apiKey }
	};

	json parameters = {
		{"type", "object"},
		{"properties", json::object()},
		{"required", json::array()}
	};

	json taskDeclaration = {
		{"type", "function"},
		{"name", "get_tasks"},
		{"description", "gets user's tasklist with status and deadlines"},
		{"parameters", parameters}
	};
	
	json body = {
		{"model", "gemini-3.8-flash"},
		{"input", "Give me a morning briefing on my current tasks"},
		{"tools", json::array({taskDeclaration})}
	};

	auto res = cli.Post(
		"/v1beta/interactions",
		headers,
		body.dump(),
		"application/json"
	);

	if (!res)
	{
		std::cout << "Request failed.\n";
		return 1;
	}

	std::cout << "RESPONSE\n";
	if (res->status != 200)
	{
		std::cout << "API error " << res->status << ": " << res->body << '\n';
		return 1;
	}
	json responseData = json::parse(res->body);
	std::cout << responseData.dump(2) << '\n';

	std::string interactionID{ responseData["id"].get<std::string>() };
	std::cout << interactionID << '\n';
	std::string functionID{};
	for (const json& e : responseData["steps"])
	{
		if (e["type"] == "function_call")
		{
			functionID = e["id"].get<std::string>();
		}
	}
	std::cout << functionID << '\n';

	std::ofstream log("C:/C++ Projects/Lynx/log.txt", std::ios::app);
	if (!log.is_open())
	{
		std::cout << "Failed to open log file.\n"; // Log write is optional so a guard clause isn't necessary
	}
	else
	{
		auto now{ std::chrono::system_clock::now() };
		std::string timeStamp{ std::format("{0:%F %T}", now) };
		log << "[" << timeStamp << "]\n";
		//log << answer << "\n\n";
	}

	return 0;
}