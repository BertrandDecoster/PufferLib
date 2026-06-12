// Copyright 2024
// Shared CSV parsing utilities (see csv_utils.h).

#include "csv_utils.h"

#include <sstream>

namespace companions {

std::string Trim(const std::string& s) {
  size_t start = s.find_first_not_of(" \t\r\n");
  if (start == std::string::npos) return "";
  size_t end = s.find_last_not_of(" \t\r\n");
  return s.substr(start, end - start + 1);
}

std::vector<std::string> ParseCSVLine(const std::string& line) {
  std::vector<std::string> result;
  std::string cell;
  bool in_quotes = false;

  for (size_t i = 0; i < line.size(); ++i) {
    char c = line[i];
    if (c == '"') {
      in_quotes = !in_quotes;
    } else if (c == ',' && !in_quotes) {
      result.push_back(Trim(cell));
      cell.clear();
    } else {
      cell += c;
    }
  }
  result.push_back(Trim(cell));
  return result;
}

std::vector<std::string> SplitOn(const std::string& s, char delim) {
  std::vector<std::string> result;
  std::stringstream ss(s);
  std::string token;
  while (std::getline(ss, token, delim)) {
    result.push_back(token);
  }
  return result;
}

}  // namespace companions
