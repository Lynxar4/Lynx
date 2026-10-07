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
	constexpr int iterationLimit{ 10 };
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

std::optional<json> getObjectives()
{
	std::ifstream file("C:/C++ Projects/Lynx/objectives.json");
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
		std::cout << "objectives.json is not a valid json. Description: " << e.what() << '\n';
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
		{ "query", query },
		{"include_answer", true},
		{"max_results", 3}
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

std::string formatSearchResult(const json& searchResult)
{
	std::string formattedString{};
	if (!searchResult["answer"].is_null())
	{
		formattedString += "Answer: " + searchResult["answer"].get<std::string>() + "\n\n";
	}
	for (const auto& r : searchResult["results"])
	{
		formattedString += "- " + r["title"].get<std::string>() + "\n" 
			+ r["content"].get<std::string>() + "\n"
			+ r["url"].get<std::string>() + "\n\n";
	}
	return formattedString;
}

std::string getCommand()
{
	std::cout << "Enter a command: ";
	std::string command{};
	std::getline(std::cin >> std::ws, command);
	return command;
}

json buildFunctionResult(std::string_view functionName, std::string_view callID, std::string_view result)
{
	json resultPart{ {"type", "text"}, {"text", result} };
	return json {
		{"type", "function_result"},
		{"name", functionName},
		{"call_id", callID},
		{"result", json::array({resultPart})}
	};
}

std::optional<std::string> readFileContents(std::string fileName)
{
	std::ifstream file{"C:/Lynx files/" + fileName};
	if (!file)
	{
		std::cerr << fileName << " could not be opened for reading.\n";
		return std::nullopt;
	}

	std::string fileText{};
	std::string line{};
	while (std::getline(file, line))
	{
		fileText += line + '\n';
	}

	return fileText;
}

bool writeFile(std::string fileName, std::string_view content)
{
	std::ofstream outf{ "C:/Lynx files/" + fileName };
	if (!outf)
	{
		std::cerr << fileName << " could not be opened for writing.\n";
		return false;
	}
	outf << content;
	return true;
}

int main()
{
	auto geminiApiKey{ getApiKey("GEMINI_API_KEY") };
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

	std::string command{ getCommand() };
	json parameters = {
		{"type", "object"},
		{"properties", json::object()}, // properties are arguments for the function
		{"required", json::array()}
	};

	json taskDeclaration = {
		{"type", "function"},
		{"name", "getTasks"},
		{"description", "Gets user's tasklist with status and deadlines. Use this only when information about the user's tasks is needed."},
		{"parameters", parameters}
	};

	json queryProperty = {
		{"query", {
			{"type", "string"},
			{"description", "The search query"}
			}}
	};

	json webSearchParameters = {
		{"type", "object"},
		{"properties", queryProperty},
		{"required", json::array({"query"})}
	};

	json webSearchDeclaration = {
		{"type", "function"},
		{"name", "webSearch"},
		{"description", "Search the web"},
		{"parameters", webSearchParameters}
	};

	json fileNameProperty = {
		{"type", "string"},
		{"description", "The name of the file"}
	};

	json readFileParameters = {
		{"type", "object"},
		{"properties", {
			{"fileName", fileNameProperty}
			}},
		{"required", json::array({"fileName"})}
	};

	json readFileDeclaration = {
		{"type", "function"},
		{"name", "readFile"},
		{"description", "Read the user's file"},
		{"parameters", readFileParameters},
	};

	json contentProperty = {
		{"type", "string"},
		{"description", "content to put in the file"}
	};

	json writeFileParameter = {
		{"type", "object"},
		{"properties", {
			{"fileName", fileNameProperty},
			{"content", contentProperty}
			}},
		{"required", json::array({"fileName", "content"})}
	};

	json writeFileDeclaration = {
		{"type", "function"},
		{"name", "writeFile"},
		{"description", "Write a txt file in this directory C:/Lynx files"},
		{"parameters", writeFileParameter},
	};

	json objectivesDeclaration = {
		{"type", "function"},
		{"name", "getObjectives"},
		{"description", "Get the current user objectives to determine what to work on"},
		{"parameters", parameters}
	};
	
	json body = {
		{"model", config::modelName},
		{"input", command},
		{"tools", json::array({taskDeclaration, webSearchDeclaration, readFileDeclaration, objectivesDeclaration, writeFileDeclaration})}
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

	int iterations{};
	while (status == "requires_action" && iterations < config::iterationLimit)
	{
		++iterations;
		bool calledTool{ false };
		json inputArray = json::array();
		for (const json& e : responseData["steps"])
		{
			if (e["type"] == "function_call" && e["name"] == "getTasks")
			{
				functionID = e["id"].get<std::string>();
				std::string tasklist{ getListString(sortedTasks) };
				json result{buildFunctionResult("getTasks", functionID, tasklist)};
				inputArray.push_back(result);
				calledTool = true;
			}
			else if (e["type"] == "function_call" && e["name"] == "webSearch")
			{
				functionID = e["id"].get<std::string>();
				std::string query{ e["arguments"]["query"] };
				json searchResult{ getSearchResult(*tavilyApiKey, query) };
				std::string formattedResult{ formatSearchResult(searchResult) };
				json result{ buildFunctionResult("webSearch", functionID, formattedResult) };
				inputArray.push_back(result);
				calledTool = true;
			}
			else if (e["type"] == "function_call" && e["name"] == "readFile")
			{
				functionID = e["id"].get<std::string>();
				std::string fileName{ e["arguments"]["fileName"] };
				std::optional<std::string> fileContent{ readFileContents(fileName) };
				if (!fileContent)
					return 1;
				json result{ buildFunctionResult("readFile", functionID, *fileContent) };
				inputArray.push_back(result);
				calledTool = true;
			}
			else if (e["type"] == "function_call" && e["name"] == "getObjectives")
			{
				functionID = e["id"].get<std::string>();
				std::optional<json> objectives{ getObjectives() };
				if (!objectives)
				{
					std::cerr << "Failed to get objectives";
					return 1;
				}
				std::string objectivesString{ (*objectives).dump() };
				json result{ buildFunctionResult("getObjectives", functionID, objectivesString) };
				inputArray.push_back(result);
				calledTool = true;
			}
			else if (e["type"] == "function_call" && e["name"] == "writeFile")
			{
				functionID = e["id"].get<std::string>();
				bool wroteFile{ writeFile(e["arguments"]["fileName"], e["arguments"]["content"]) };
				std::string status{};
				if (wroteFile)
				{
					status = "The user successfully recieved the file you wrote.";
				}
				else
				{
					status = "An error occured while writing the file.";
				}
				json result{ buildFunctionResult("writeFile", functionID, status) };
				inputArray.push_back(result);
				calledTool = true;
			}
		}

		if (!calledTool) // stops the loop if nothing matched
			break;

		json body2{
		{"model", config::modelName},
		{"input", inputArray},
		{"tools", json::array({taskDeclaration, webSearchDeclaration, readFileDeclaration, objectivesDeclaration, writeFileDeclaration})},
		{"previous_interaction_id", interactionID}
		};

		if (config::debug)
		{
			std::cerr << "\n Body being sent:\n";
			std::cerr << body2.dump(2) << '\n';
		}

		auto res2 = cli.Post(config::apiPath, headers, body2.dump(), "application/json");
		if (!res2)
		{
			std::cerr << "Request failed.\n";
			return 1;
		}
		if (res2->status != 200)
		{
			std::cout << "API error " << res2->status << ": " << res2->body << '\n';
			return 1;
		}

		responseData = json::parse(res2->body);
		if (config::debug)
		{
			std::cerr << "\nResponse: " << iterations << '\n';
			std::cerr << responseData.dump(2);
		}
		status = responseData["status"].get<std::string>();
	}

	answer = getAnswer(responseData);
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