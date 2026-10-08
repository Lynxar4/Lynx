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
	constexpr bool debug{ true };
	constexpr int iterationLimit{ 10 };
	constexpr int webSearchLimit{ 5 };
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

std::optional<json> getSearchResult(std::string apiKey, std::string query)
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
		return std::nullopt;
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

	std::optional<json> objectivesOpt{ getObjectives() };
	if (!objectivesOpt)
	{
		std::cout << "Failed to get objectives.\n";
		return 1;
	}
	std::string objectivesString{};
	for (const auto& o : *objectivesOpt)
	{
		if (o.value("active", false))
		{
			objectivesString += "- " + o["name"].get<std::string>() + ": " + o["description"].get<std::string>() + '\n';
		}
	}

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

	json tools = json::array({ taskDeclaration, webSearchDeclaration, readFileDeclaration, writeFileDeclaration });
	std::string systemInstruction{
		"The user's active objectives:\n" + objectivesString +
		"Tool results stay in the conversation. Never call a tool twice with the same arguments. "
		"Call all independent tools in the same turn. Do at most " + std::to_string(config::webSearchLimit) + " web searches. "
		"If you need to write a file, write it as soon as you have enough information." 
	};

	json body = {
		{"model", config::modelName},
		{"system_instruction", systemInstruction},
		{"input", command},
		{"tools", tools}
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
	int webSearchCounter{};
	int toolCalls{};
	while (status == "requires_action" && iterations < config::iterationLimit)
	{
		++iterations;
		json inputArray = json::array();
		for (const json& e : responseData["steps"])
		{
			if (e["type"] == "function_call" && e["name"] == "getTasks")
			{
				functionID = e["id"].get<std::string>();
				std::string tasklist{ getListString(sortedTasks) };
				json result{buildFunctionResult("getTasks", functionID, tasklist)};
				inputArray.push_back(result);
			}
			else if (e["type"] == "function_call" && e["name"] == "webSearch")
			{
				++webSearchCounter;
				functionID = e["id"].get<std::string>();
				if (webSearchCounter > config::webSearchLimit)
				{
					inputArray.push_back(buildFunctionResult("webSearch", functionID, "You have reached the maximum number of web searches. Complete the task with what you know."));
					continue;
				}
				std::string query{ e["arguments"]["query"] };
				std::optional<json> searchResultOpt{ getSearchResult(*tavilyApiKey, query) };
				if (!searchResultOpt)
				{
					inputArray.push_back(buildFunctionResult("webSearch", functionID, "Search failed."));
					continue;
				}
				std::string formattedResult{ formatSearchResult(*searchResultOpt) };
				json result{ buildFunctionResult("webSearch", functionID, formattedResult) };
				inputArray.push_back(result);
			}
			else if (e["type"] == "function_call" && e["name"] == "readFile")
			{
				functionID = e["id"].get<std::string>();
				std::string fileName{ e["arguments"]["fileName"] };
				std::optional<std::string> fileContent{ readFileContents(fileName) };
				if (!fileContent)
				{
					inputArray.push_back(buildFunctionResult("readFile", functionID, "File not found."));
					continue;
				}
				json result{ buildFunctionResult("readFile", functionID, *fileContent) };
				inputArray.push_back(result);
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
			}
			else if (e["type"] == "function_call")
			{
				inputArray.push_back(buildFunctionResult(e["name"], e["id"], "Unknown tool."));
			}
		}
		toolCalls += static_cast<int>(inputArray.size());

		if (inputArray.empty()) // stops the loop if nothing matched
		{
			break;
		}

		if (iterations == config::iterationLimit)
		{
			systemInstruction += "\nThis is your last turn. Do not call anymore tools. Answer with the information you have.";
		}

		json body2{
		{"model", config::modelName},
		{"system_instruction", systemInstruction},
		{"input", inputArray},
		{"tools", tools},
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
	if (answer.empty())
	{
		answer = "An error occured somewhere. First response status: " + status;
	}
	std::cout << "\nIterations: " << iterations << "\nTool calls: " << toolCalls << "\nSearches: " << webSearchCounter << '\n';
	std::cout << answer;

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