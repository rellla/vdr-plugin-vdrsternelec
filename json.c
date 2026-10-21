/*
 * vdrsternelec - A VDR plugin to update VDR*ELEC systems
 *
 * Copyright (C) 2026 Andreas Baierl <ichgeh@imkreisrum.de>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * json.c: Minimal JSON parser
 */

#include "json.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

#define JSON_MAX_DEPTH 64

// --- cJsonValue ------------------------------------------------------------

std::string cJsonValue::AsString(void) const
{
  switch (type) {
    case jtString: return stringValue;
    case jtBool:   return boolValue ? "true" : "false";
    case jtNumber: {
         char buf[64];
         if (std::fabs(numberValue) < 1e15 && numberValue == (double)(long long)numberValue)
            snprintf(buf, sizeof(buf), "%lld", (long long)numberValue);
         else
            snprintf(buf, sizeof(buf), "%.17g", numberValue);
         return buf;
         }
    default: return "";
    }
}

const cJsonValue *cJsonValue::Get(const std::string &Key) const
{
  if (type != jtObject)
     return NULL;
  for (size_t i = 0; i < objectValue.size(); i++) {
      if (objectValue[i].first == Key)
         return &objectValue[i].second;
      }
  return NULL;
}

std::string cJsonValue::GetString(const std::string &Key, const std::string &Default) const
{
  const cJsonValue *v = Get(Key);
  if (!v || v->IsNull() || v->IsArray() || v->IsObject())
     return Default;
  return v->AsString();
}

// --- cJsonParser -----------------------------------------------------------

class cJsonParser {
private:
  const std::string &text;
  size_t pos;
  std::string error;
  void SkipWhitespace(void);
  bool Fail(const char *Message);
  bool ParseValue(cJsonValue &Value, int Depth);
  bool ParseString(std::string &Result);
  bool ParseNumber(cJsonValue &Value);
  bool ParseLiteral(const char *Literal);
  static void AppendUtf8(std::string &s, unsigned long cp);
  bool ParseHex4(unsigned long &cp);
public:
  cJsonParser(const std::string &Text) : text(Text), pos(0) {}
  bool Parse(cJsonValue &Result);
  const std::string &Error(void) const { return error; }
  };

void cJsonParser::SkipWhitespace(void)
{
  while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t' || text[pos] == '\n' || text[pos] == '\r'))
        pos++;
}

bool cJsonParser::Fail(const char *Message)
{
  if (error.empty()) {
     char buf[128];
     snprintf(buf, sizeof(buf), "%s at offset %zu", Message, pos);
     error = buf;
     }
  return false;
}

bool cJsonParser::ParseLiteral(const char *Literal)
{
  size_t len = 0;
  while (Literal[len])
        len++;
  if (text.compare(pos, len, Literal) != 0)
     return Fail("invalid literal");
  pos += len;
  return true;
}

void cJsonParser::AppendUtf8(std::string &s, unsigned long cp)
{
  if (cp < 0x80)
     s += char(cp);
  else if (cp < 0x800) {
     s += char(0xC0 | (cp >> 6));
     s += char(0x80 | (cp & 0x3F));
     }
  else if (cp < 0x10000) {
     s += char(0xE0 | (cp >> 12));
     s += char(0x80 | ((cp >> 6) & 0x3F));
     s += char(0x80 | (cp & 0x3F));
     }
  else {
     s += char(0xF0 | (cp >> 18));
     s += char(0x80 | ((cp >> 12) & 0x3F));
     s += char(0x80 | ((cp >> 6) & 0x3F));
     s += char(0x80 | (cp & 0x3F));
     }
}

bool cJsonParser::ParseHex4(unsigned long &cp)
{
  if (pos + 4 > text.size())
     return Fail("truncated unicode escape");
  cp = 0;
  for (int i = 0; i < 4; i++) {
      char c = text[pos++];
      cp <<= 4;
      if (c >= '0' && c <= '9')
         cp |= c - '0';
      else if (c >= 'a' && c <= 'f')
         cp |= c - 'a' + 10;
      else if (c >= 'A' && c <= 'F')
         cp |= c - 'A' + 10;
      else
         return Fail("invalid unicode escape");
      }
  return true;
}

bool cJsonParser::ParseString(std::string &Result)
{
  // assumes text[pos] == '"'
  pos++;
  Result.clear();
  while (pos < text.size()) {
        char c = text[pos++];
        if (c == '"')
           return true;
        if ((unsigned char)c < 0x20)
           return Fail("control character in string");
        if (c != '\\') {
           Result += c;
           continue;
           }
        if (pos >= text.size())
           break;
        c = text[pos++];
        switch (c) {
          case '"':  Result += '"'; break;
          case '\\': Result += '\\'; break;
          case '/':  Result += '/'; break;
          case 'b':  Result += '\b'; break;
          case 'f':  Result += '\f'; break;
          case 'n':  Result += '\n'; break;
          case 'r':  Result += '\r'; break;
          case 't':  Result += '\t'; break;
          case 'u': {
               unsigned long cp;
               if (!ParseHex4(cp))
                  return false;
               if (cp >= 0xD800 && cp <= 0xDBFF) {
                  // surrogate pair
                  if (pos + 2 <= text.size() && text[pos] == '\\' && text[pos + 1] == 'u') {
                     pos += 2;
                     unsigned long lo;
                     if (!ParseHex4(lo))
                        return false;
                     if (lo >= 0xDC00 && lo <= 0xDFFF)
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                     else
                        cp = 0xFFFD;
                     }
                  else
                     cp = 0xFFFD;
                  }
               else if (cp >= 0xDC00 && cp <= 0xDFFF)
                  cp = 0xFFFD;
               AppendUtf8(Result, cp);
               }
               break;
          default:
               return Fail("invalid escape sequence");
          }
        }
  return Fail("unterminated string");
}

bool cJsonParser::ParseNumber(cJsonValue &Value)
{
  size_t start = pos;
  if (pos < text.size() && text[pos] == '-')
     pos++;
  if (pos >= text.size() || !(text[pos] >= '0' && text[pos] <= '9'))
     return Fail("invalid number");
  while (pos < text.size() && ((text[pos] >= '0' && text[pos] <= '9') || text[pos] == '.' || text[pos] == 'e' || text[pos] == 'E' || text[pos] == '+' || text[pos] == '-'))
        pos++;
  std::string num = text.substr(start, pos - start);
  char *end = NULL;
  double d = strtod(num.c_str(), &end);
  if (!end || *end)
     return Fail("invalid number");
  Value.type = cJsonValue::jtNumber;
  Value.numberValue = d;
  return true;
}

bool cJsonParser::ParseValue(cJsonValue &Value, int Depth)
{
  if (Depth > JSON_MAX_DEPTH)
     return Fail("nesting too deep");
  SkipWhitespace();
  if (pos >= text.size())
     return Fail("unexpected end of input");
  char c = text[pos];
  if (c == '{') {
     pos++;
     Value.type = cJsonValue::jtObject;
     SkipWhitespace();
     if (pos < text.size() && text[pos] == '}') {
        pos++;
        return true;
        }
     for (;;) {
         SkipWhitespace();
         if (pos >= text.size() || text[pos] != '"')
            return Fail("expected object key");
         std::string key;
         if (!ParseString(key))
            return false;
         SkipWhitespace();
         if (pos >= text.size() || text[pos] != ':')
            return Fail("expected ':'");
         pos++;
         Value.objectValue.push_back(std::make_pair(key, cJsonValue()));
         if (!ParseValue(Value.objectValue.back().second, Depth + 1))
            return false;
         SkipWhitespace();
         if (pos < text.size() && text[pos] == ',') {
            pos++;
            continue;
            }
         if (pos < text.size() && text[pos] == '}') {
            pos++;
            return true;
            }
         return Fail("expected ',' or '}'");
         }
     }
  if (c == '[') {
     pos++;
     Value.type = cJsonValue::jtArray;
     SkipWhitespace();
     if (pos < text.size() && text[pos] == ']') {
        pos++;
        return true;
        }
     for (;;) {
         Value.arrayValue.push_back(cJsonValue());
         if (!ParseValue(Value.arrayValue.back(), Depth + 1))
            return false;
         SkipWhitespace();
         if (pos < text.size() && text[pos] == ',') {
            pos++;
            continue;
            }
         if (pos < text.size() && text[pos] == ']') {
            pos++;
            return true;
            }
         return Fail("expected ',' or ']'");
         }
     }
  if (c == '"') {
     Value.type = cJsonValue::jtString;
     return ParseString(Value.stringValue);
     }
  if (c == 't') {
     Value.type = cJsonValue::jtBool;
     Value.boolValue = true;
     return ParseLiteral("true");
     }
  if (c == 'f') {
     Value.type = cJsonValue::jtBool;
     Value.boolValue = false;
     return ParseLiteral("false");
     }
  if (c == 'n') {
     Value.type = cJsonValue::jtNull;
     return ParseLiteral("null");
     }
  return ParseNumber(Value);
}

bool cJsonParser::Parse(cJsonValue &Result)
{
  // skip UTF-8 BOM
  if (text.compare(0, 3, "\xEF\xBB\xBF") == 0)
     pos = 3;
  if (!ParseValue(Result, 0))
     return false;
  SkipWhitespace();
  if (pos != text.size())
     return Fail("trailing garbage");
  return true;
}

bool JsonParse(const std::string &Text, cJsonValue &Result, std::string &Error)
{
  cJsonParser Parser(Text);
  Result = cJsonValue();
  if (!Parser.Parse(Result)) {
     Error = Parser.Error();
     Result = cJsonValue();
     return false;
     }
  Error.clear();
  return true;
}
