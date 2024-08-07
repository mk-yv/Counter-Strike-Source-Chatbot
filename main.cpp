#include <iostream>
#include <fstream>
#include <regex>
#include <string>
#include <thread>
#include <chrono>
#include <curl/curl.h>
#include <windows.h>
#include <json/json.h>

// Constants
const std::string LOG_FILE_PATH = "C:\\Program Files (x86)\\Steam\\steamapps\\common\\Counter-Strike Source\\cstrike\\consolelog.txt"; //you might need to change this
const std::string PREFIX = ".explain"; //prefix
const std::regex LOG_REGEX(R"(^(\d{2}\/\d{2}\/\d{4} - \d{2}:\d{2}:\d{2}): (\*DEAD\* )?([^|]+) :\s+(.+)$)");

// Function declarations
void send_command_to_css(const std::string& content);
void log_command();
std::string parse_log_entry(const std::string& entry);
std::string extract_content_from_response(const std::string& response);
void send_prompt_to_ollama(const std::string& prompt);
void monitor_log_file();

// Callback function for CURL
size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

// Extract the prompt
std::string parse_log_entry(const std::string& entry) {
    std::smatch match;
    if (std::regex_match(entry, match, LOG_REGEX)) {
        std::string message = match[4].str();
        if (message.find(PREFIX) == 0) {
            return message.substr(PREFIX.length());
        }
    }
    return "";
}

// Extract content from the JSON response
std::string extract_content_from_response(const std::string& response) {
    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errs;

    std::istringstream s(response);
    if (!Json::parseFromStream(builder, s, &root, &errs)) {
        std::cerr << "Failed to parse JSON: " << errs << std::endl;
        return "";
    }

    return root["message"]["content"].asString();
}

// Send the prompt to Ollama
void send_prompt_to_ollama(const std::string& prompt) {
    CURL* curl;
    CURLcode res;
    std::string readBuffer;

    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();
    if (curl) {
        std::string postFields = R"({"model":"gemma:2b","messages":[{"role":"user","content":")" + prompt + R"("}],"stream":false})"; //you can find models here: https://ollama.com/library speed may vary depending on model

        curl_easy_setopt(curl, CURLOPT_URL, "http://localhost:11434/api/chat"); //default ollama port
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postFields.c_str());

        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);

        res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;    
        }
        else {
            std::cout << "Response from Ollama: " << readBuffer << std::endl;
            std::string content = extract_content_from_response(readBuffer);
            send_command_to_css(content);
        }

        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    }
    curl_global_cleanup();
}

// Logging
void log_command() {
    HWND hwnd = FindWindowA(NULL, "Counter-Strike Source");
    if (!hwnd) {
        std::cerr << "Error: Please make sure Counter-Strike Source is open" << std::endl;
        return;
    }

    std::string command = "con_logfile consolelog.txt; con_timestamp 1"; //enables console logging
    COPYDATASTRUCT cds = { 0, (DWORD)(command.size() + 1), (void*)command.c_str() };
    LRESULT result = SendMessageA(hwnd, WM_COPYDATA, 0, (LPARAM)&cds);

    std::cout << "Sent command: " << command << ", result: " << result << std::endl;
}

// Send command
void send_command_to_css(const std::string& content) {
    HWND hwnd = FindWindowA(NULL, "Counter-Strike Source");
    if (!hwnd) {
        std::cerr << "Error: Please make sure Counter-Strike Source is open" << std::endl;
        return;
    }

    std::string command = "say " + content;
    COPYDATASTRUCT cds = { 0, (DWORD)(command.size() + 1), (void*)command.c_str() };
    LRESULT result = SendMessageA(hwnd, WM_COPYDATA, 0, (LPARAM)&cds);

    std::cout << "Sent command: " << command << ", result: " << result << std::endl;
}

// Monitor the log file
void monitor_log_file() {
    std::ifstream log_file(LOG_FILE_PATH);
    if (!log_file.is_open()) {
        std::cerr << "Failed to open log file: " << LOG_FILE_PATH << std::endl;
        return;
    }

    log_file.seekg(0, std::ios::end);
    std::string line;
    while (true) {
        while (std::getline(log_file, line)) {
            std::string prompt = parse_log_entry(line);
            if (!prompt.empty()) {
                std::cout << "Extracted prompt: " << prompt << std::endl;
                send_prompt_to_ollama(prompt);
            }
        }

        std::this_thread::sleep_for(std::chrono::seconds(1));
        log_file.clear();
        log_file.seekg(0, std::ios::cur);
    }

    log_file.close();
}

// Main function
int main() {
    log_command();
    monitor_log_file();
    return 0;
}
