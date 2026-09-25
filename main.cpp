#define CPPHTTPLIB_OPENSSL_SUPPORT
#define _CRT_SECURE_NO_WARNINGS
#include <iostream>
#include <fstream>
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

	std::ifstream file("tasks.json");
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
			return task1["deadline"].get<std::string>() < task2["deadline"].get<std::string>();
		}
	);

	std::string taskList{};
	for (const auto& e : tasks)
	{
		if (e["status"] == "done")
		{
			continue;
		}
		taskList += "Task: " + e["task"].get<std::string>() +
			" | Category: " + e["category"].get<std::string>() +
			" | Deadline: " + e["deadline"].get<std::string>() +
			" | Status: " + e["status"].get<std::string>() + '\n';
	}

	httplib::Client cli("https://generativelanguage.googleapis.com");
	httplib::Headers headers = { // httplib::Headers behaves like a map container
		{ "x-goog-api-key", *apiKey }
	};

	json textPart = { {"text", taskList} }; 
	json partsArray = json::array({ textPart });
	json contentEntry = { {"parts", partsArray} };
	json contentsArray = json::array({contentEntry});
	json body = { { "contents", contentsArray } };
	std::cout << body.dump(2);

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
	std::cout << "Status: " << res->status << '\n';
	json responseData = json::parse(res->body);
	std::string answer{ responseData["candidates"][0]["content"]["parts"][0]["text"] };
	std::cout << answer << '\n';


	

	return 0;
}