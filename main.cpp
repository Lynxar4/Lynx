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

int main()
{
	auto apiKey{ getApiKey() };
	if (!apiKey)
	{
		return 1;
	}

	std::ifstream file("C:/C++ Projects/Lynx/tasks.json");
	if (!file.is_open())
	{
		std::cout << "Failed to open json file.\n";
		return 1;
	}

	json data = json::parse(file);
	std::vector<json> tasks{};
	for (const auto& e : data)
	{
		tasks.push_back(e);
	}

	std::sort(tasks.begin(), tasks.end(), [](const json& task1, const json& task2)
		{
			bool t1IsExam{ task1.contains("type") && task1["type"] == "exam"};
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

	std::string taskList{};
	for (const auto& e : tasks)
	{
		if (e.contains("status") && e["status"] == "done")
		{
			continue;
		}
		std::string status{ e.contains("status") ? e["status"].get<std::string>() : "N/A" };
		taskList += "Task: " + e["task"].get<std::string>() +
			" | Category: " + e["category"].get<std::string>() +
			" | Deadline: " + e["deadline"].get<std::string>() +
			" | Status: " + status +
			" | Type: " + e["type"].get<std::string>() + '\n';
	}
	std::cout << taskList << '\n';

	httplib::Client cli("https://generativelanguage.googleapis.com");
	httplib::Headers headers = {
		{ "x-goog-api-key", *apiKey }
	};

	std::string prompt{ "Here are my current tasks: \n" + 
	taskList + "\n\n" +
	"Analyze these tasks and tell me what I should focus on first. Give reasoning behind your explanation." 
	};

	json textPart = { {"text", prompt} }; 
	json partsArray = json::array({ textPart });
	json contentEntry = { {"parts", partsArray} };
	json contentsArray = json::array({contentEntry});
	json body = { { "contents", contentsArray } };

	auto res = cli.Post(
		"/v1beta/models/gemini-3.5-flash-lite:generateContent",
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
	std::string answer{ responseData["candidates"][0]["content"]["parts"][0]["text"] };
	std::cout << answer << '\n';

	std::ofstream log("C:/C++ Projects/Lynx/log.txt", std::ios::app);
	if (!log.is_open())
	{
		std::cout << "Failed to open log file.\n";
	}
	else // Log write is optional so a guard clause isn't necessary
	{
		auto now{ std::chrono::system_clock::now() };
		std::string timeStamp{ std::format("{0:%F %T}", now) };
		log << "[" << timeStamp << "]\n";
		log << answer << "\n\n";
	}

	return 0;
}