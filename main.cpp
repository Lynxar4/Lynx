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

namespace config
{
	constexpr auto server{ "https://generativelanguage.googleapis.com" };
	constexpr auto apiPath{ "/v1beta/interactions" };
	constexpr auto modelName{ "gemini-3.5-flash-lite" };
	constexpr auto tasksPath{ "C:/C++ Projects/Lynx/tasks.json" };
	constexpr auto logPath{ "C:/C++ Projects/Lynx/log.txt" };
}

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
	std::ifstream file(config::tasksPath);
	if (!file.is_open())
	{
		return std::nullopt;
	}
	try
	{
		return json::parse(file);
	}
	catch (const json::parse_error& e)
	{
		std::cout << "tasks.json is not a valid json. Description: " << e.what() << '\n';
		return std::nullopt;

	}
}

std::vector<json> getSortedTasks(const json& tasksJson)
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

std::string getListString(const std::vector<json>& tasks)
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

std::string getAnswer(const json& response)
{
	std::string answer{};
	for (const json& e : response["steps"])
	{
		if (e["type"] == "model_output")
		{
			answer = e["content"][0]["text"];
		}
	}
	return answer;
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

	json tasks = *tasksOpt;
	std::vector<json> sortedTasks{ getSortedTasks(tasks) };

	httplib::Client cli(config::server);
	httplib::Headers headers = {
		{ "x-goog-api-key", *apiKey }
	};

	json parameters = {
		{"type", "object"},
		{"properties", json::object()}, // properties are arguments for the function
		{"required", json::array()}
	};

	json taskDeclaration = {
		{"type", "function"},
		{"name", "get_tasks"},
		{"description", "gets user's tasklist with status and deadlines"},
		{"parameters", parameters}
	};
	
	json body = {
		{"model", config::modelName},
		{"input", "Hey what is today's date?"},
		{"tools", json::array({taskDeclaration})}
	};

	auto res = cli.Post(
		config::apiPath,
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
	std::string status{ responseData["status"].get<std::string>() };
	if (status != "completed" && status != "requires_action")
	{
		std::cout << "Interaction failed with status " << status;
		return 1;
	}

	std::string interactionID{ responseData["id"].get<std::string>() };
	std::string functionID{};
	std::string answer{getAnswer(responseData)};
	for (const json& e : responseData["steps"])
	{
		if (e["type"] == "function_call")
		{
			functionID = e["id"].get<std::string>();
			std::string tasklist{ getListString(sortedTasks) };
			json resultPart{ {"type", "text"}, {"text", tasklist} };
			json resultArray = json::array({resultPart});
			json functionResult{ 
				{"type", "function_result"},
				{"name", "get_tasks"},
				{"call_id", functionID},
				{"result", resultArray} 
			};
			json input = json::array({ functionResult });
			json body2{
				{"model", config::modelName},
				{"input", input},
				{ "tools", json::array({taskDeclaration}) },
				{"previous_interaction_id", interactionID} 
			};

			auto res2 = cli.Post(
				config::apiPath,
				headers,
				body2.dump(),
				"application/json"
			);

			if (!res2)
			{
				std::cout << "Request failed.\n";
				return 1;
			}

			std::cout << "RESPONSE 2\n";
			if (res2->status != 200)
			{
				std::cout << "API error " << res2->status << ": " << res2->body << '\n';
				return 1;
			}
			json responseData2 = json::parse(res2->body);
			if (responseData2["status"] != "completed")
			{
				std::cout << "Interaction did not complete.\n";
				return 1;
			}
			answer = getAnswer(responseData2);
		}
	}
	std::cout << answer;
	if (answer.empty())
	{
		answer = "An error occured somewhere. First response status: " + status;
	}

	std::ofstream log(config::logPath, std::ios::app);
	if (!log.is_open())
	{
		std::cout << "Failed to open log file.\n"; // Log write is optional so a guard clause isn't necessary
	}
	else
	{
		auto now{ std::chrono::system_clock::now() };
		std::string timeStamp{ std::format("{0:%F %T}", now) };
		log << "[" << timeStamp << "]\n";
		log << answer << "\n\n";
	}

	return 0;
}