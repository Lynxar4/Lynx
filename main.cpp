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
	constexpr auto geminiServer{ "https://generativelanguage.googleapis.com" };
	constexpr auto tavilyServer{ "https://api.tavily.com" };
	constexpr auto apiPath{ "/v1beta/interactions" };
	constexpr auto modelName{ "gemini-3.5-flash-lite" };
	constexpr auto tasksPath{ "C:/C++ Projects/Lynx/tasks.json" };
	constexpr auto logPath{ "C:/C++ Projects/Lynx/log.txt" };
	constexpr bool debug{ false };
}

std::optional<std::string> getApiKey(const char* keyName)
{
	const char* apiKey{ std::getenv(keyName) }; 
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

json getSearchResult(std::string apiKey, std::string query)
{
	httplib::Client cli(config::tavilyServer);
	httplib::Headers headers{
		{"Content-Type", "application/json"},
		{"Authorization", "Bearer " + apiKey},
	};
	json body{
		{ "query", query }
	};

	auto res = cli.Post(
		"/search",
		headers,
		body.dump(),
		"application/json"
	);

	if (!res)
	{
		std::cout << "Request failed.\n";
		return {};
	}
	if (res->status != 200)
	{
		std::cout << "API error " << res->status << ": " << res->body << '\n';
		return 1;
	}

	json responseData{ json::parse(res->body) };
	return responseData;
}

std::string getCommand()
{
	std::cout << "Enter a command: ";
	std::string command{};
	std::getline(std::cin >> std::ws, command);
	return command;
}

std::optional<json> sendFunctionResult(std::string_view interactionID, std::string_view functionID, std::string_view functionName, const json& tool, std::string_view result, httplib::Client& cli, httplib::Headers& headers)
{
	json resultPart{ {"type", "text"}, {"text", result} };
	json resultArray{ json::array({resultPart}) };
	json input{
		{"type", "function_result"},
		{"name", functionName},
		{"call_id", functionID},
		{"result", resultArray}
	};
	json inputArray{ json::array({input}) };
	json body2{
		{"model", config::modelName},
		{"input", inputArray},
		{"tools", json::array({tool})},
		{"previous_interaction_id", interactionID}
	};

	auto res = cli.Post(
		config::apiPath,
		headers,
		body2.dump(),
		"application/json"
	);
	if (!res)
	{
		std::cout << "Request failed.\n";
	}
	if (res->status != 200)
	{
		std::cout << "API error " << res->status << ": " << res->body << '\n';
		return 1;
	}

	json responseData{ json::parse(res->body) };
	return responseData;
}

int main()
{
	auto geminiApiKey{getApiKey("GEMINI_API_KEY")};
	if (!geminiApiKey)
	{
		return 1;
	}

	auto tavilyApiKey{ getApiKey("TAVILY_API_KEY") };
	if (!tavilyApiKey)
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

	httplib::Client cli(config::geminiServer);
	httplib::Headers headers = {
		{ "x-goog-api-key", *geminiApiKey }
	};

	std::string command{getCommand()};
	json parameters = {
		{"type", "object"},
		{"properties", json::object()}, // properties are arguments for the function
		{"required", json::array()}
	};

	json taskDeclaration = {
		{"type", "function"},
		{"name", "getTasks"},
		{"description", "gets user's tasklist with status and deadlines"},
		{"parameters", parameters}
	};

	json webSearch = {
		{"type", "function"},
		{"name", "webSearch"},
		{"description", "Search the web"},
		{"parameters", parameters}
	};
	
	json body = {
		{"model", config::modelName},
		{"input", command},
		{"tools", json::array({taskDeclaration, webSearch})}
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
	if (config::debug)
	{
		std::cout << responseData.dump(2);
	}
	for (const json& e : responseData["steps"])
	{
		if (e["type"] == "function_call" && e["name"] == "getTasks") 
		{
			functionID = e["id"].get<std::string>();
			std::string tasklist{ getListString(sortedTasks) };
			std::optional<json> responseData{sendFunctionResult(interactionID, functionID, "getTasks", taskDeclaration, tasklist, cli, headers)};
			if (!responseData)
				return 1;
			answer = getAnswer(*responseData);
		}
		else if (e["type"] == "function_call" && e["name"] == "webSearch")
		{
			functionID = e["id"].get<std::string>();
			std::string searchResult{ getSearchResult(*tavilyApiKey, command).dump() };
			std::optional<json> responseData{ sendFunctionResult(interactionID, functionID, "webSearch", webSearch, searchResult, cli, headers) };
			if (!responseData)
				return 1;
			answer = getAnswer(*responseData);
		}
	}
	std::cout << '\n';
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