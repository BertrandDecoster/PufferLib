// Copyright 2024
// Shared CSV parsing utilities for the data-driven config loaders
// (agent_config.cc, effect_config.cc). One CSV dialect for both data files:
// comma-separated, double-quote aware, fields trimmed; ';' is the in-field
// list separator (cadence, area) since ',' is the column delimiter.

#ifndef COMPANIONS_CORE_CSV_UTILS_H_
#define COMPANIONS_CORE_CSV_UTILS_H_

#include <string>
#include <vector>

namespace companions {

// Strips leading/trailing whitespace (" \t\r\n").
std::string Trim(const std::string& s);

// Splits one CSV line on ',' respecting double quotes. Quote characters are
// consumed (not kept in the output); commas inside quotes do not split.
// Every field is Trim()ed. Always returns at least one element.
std::vector<std::string> ParseCSVLine(const std::string& line);

// Splits on a delimiter with std::getline semantics (no trailing empty token,
// empty input yields no tokens). Tokens are returned raw - callers decide
// about trimming / skipping empties. Used for in-field ';' lists.
std::vector<std::string> SplitOn(const std::string& s, char delim);

}  // namespace companions

#endif  // COMPANIONS_CORE_CSV_UTILS_H_
