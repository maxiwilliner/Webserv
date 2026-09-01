#include "../include/webserv.hpp"


bool                        Utils::ends_with(const std::string& fullString, const std::string& ending) {
	if (fullString.length() < ending.length()) return false;
	return fullString.compare(fullString.length() - ending.length(), ending.length(), ending) == 0;
}

bool                        Utils::isDirectory(const std::string& path) {
    struct stat info;
    return stat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
}

bool                        Utils::fileExists(const std::string& path) {
    struct stat info;
    return stat(path.c_str(), &info) == 0;
}

std::vector<std::string> 	Utils::splitBySpaces(const std::string& line) {
    std::vector<std::string> tokens;
    std::string token;

    for (size_t i = 0; i < line.length(); ++i) {
        char c = line[i];

        if (c == ' ') {
            if (!token.empty()) {
                tokens.push_back(token);
                token.clear();
            }
        } else {
            token += c;
        }
    }

    if (!token.empty())
        tokens.push_back(token);

    return tokens;
}

size_t                      Utils::getNextLine(const std::string& buffer, size_t& pos, std::string& line) {
    if (pos >= buffer.size()) return 0;

    size_t nextNewline = buffer.find('\n', pos);
    if (nextNewline == std::string::npos) {
        line = buffer.substr(pos);
        pos = buffer.size();
    } else {
        line = buffer.substr(pos, nextNewline - pos);
        pos = nextNewline + 1;
    }
    return 1;
}

size_t                      Utils::firstDifference(const std::string& s1, const std::string& s2) {
    size_t minLen = std::min(s1.length(), s2.length());

    for (size_t i = 0; i < minLen; ++i) {
        if (s1[i] != s2[i])
            return i;
    }
    // If one is a prefix of the other, difference is at minLen
    if (s1.length() != s2.length())
        return minLen;

    // They are identical
    return std::string::npos;
}

std::string         Utils::toLowerAlphaOnly(const std::string& str) {
    std::string result;
    result.reserve(str.size());
    for (std::string::const_iterator it = str.begin(); it != str.end(); ++it) {
        if (*it >= 'A' && *it <= 'Z')
            result += *it + ('a' - 'A'); // convert uppercase to lowercase
        else
            result += *it;              // leave unchanged
    }
    return result;
}

bool        Utils::endsWith(const std::string &str, const std::string &suffix) {
    if (suffix.size() > str.size())
        return false;
    return str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string Utils::ft_trim(const std::string &str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

std::string Utils::statusText(int code) {
    switch (code) {
        case 200: return "OK";
        case 201: return "Created";
        case 204: return "No Content";
        case 301: return "Moved Permanently";
        case 302: return "Found";
        case 400: return "Bad Request";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 413: return "Payload Too Large";
        case 500: return "Internal Server Error";
        case 502: return "Bad Gateway";
        case 504: return "Gateway Timeout";
        case 505: return "HTTP Version Not Supported";
        default: return "Unknown";
    }
}

std::string Utils::getFileExtension(const std::string &path) {
    size_t dotPos = path.rfind('.');
    if (dotPos == std::string::npos)
        return "";
    return path.substr(dotPos);
}

std::string Utils::urlDecode(const std::string &encode) {
    std::string decoded;
    for (size_t i = 0; i < encode.length(); ++i) {
        if (encode[i] == '%' && i + 2 < encode.length() && isxdigit(encode[i + 1]) && isxdigit(encode[i + 2])) {
            std::string hexStr = encode.substr(i + 1, 2);
            char ch = static_cast<char>(std::strtol(hexStr.c_str(), NULL, 16));
            decoded += ch;
            i += 2;
        } else if (encode[i] == '+') {
            decoded += ' ';
        } else {
            decoded += encode[i];
        }
    }
    return decoded;
}

