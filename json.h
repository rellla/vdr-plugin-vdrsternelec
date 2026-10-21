/*
 * vdrsternelec - A VDR plugin to update VDR*ELEC systems
 *
 * Copyright (C) 2026 Andreas Baierl <ichgeh@imkreisrum.de>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * json.h: Minimal JSON parser (DOM style), no external dependencies
 */

#ifndef __VDRSTERNELEC_JSON_H
#define __VDRSTERNELEC_JSON_H

#include <string>
#include <utility>
#include <vector>

class cJsonValue {
public:
  enum eType { jtNull, jtBool, jtNumber, jtString, jtArray, jtObject };
private:
  eType type;
  bool boolValue;
  double numberValue;
  std::string stringValue;
  std::vector<cJsonValue> arrayValue;
  std::vector<std::pair<std::string, cJsonValue> > objectValue;
  friend class cJsonParser;
public:
  cJsonValue(void) : type(jtNull), boolValue(false), numberValue(0) {}
  eType Type(void) const { return type; }
  bool IsNull(void) const { return type == jtNull; }
  bool IsBool(void) const { return type == jtBool; }
  bool IsNumber(void) const { return type == jtNumber; }
  bool IsString(void) const { return type == jtString; }
  bool IsArray(void) const { return type == jtArray; }
  bool IsObject(void) const { return type == jtObject; }
  bool Bool(bool Default = false) const { return type == jtBool ? boolValue : Default; }
  double Number(double Default = 0) const { return type == jtNumber ? numberValue : Default; }
  const std::string &String(void) const { return stringValue; }
  ///< Returns the string value (empty if this is not a string).
  std::string AsString(void) const;
  ///< Returns strings as is and numbers/bools converted to text.
  const std::vector<cJsonValue> &Array(void) const { return arrayValue; }
  const std::vector<std::pair<std::string, cJsonValue> > &Object(void) const { return objectValue; }
  const cJsonValue *Get(const std::string &Key) const;
  ///< Returns the member with the given Key, or NULL if this is no object
  ///< or the key does not exist.
  std::string GetString(const std::string &Key, const std::string &Default = "") const;
  ///< Convenience: returns the member Key converted with AsString(), or Default.
  };

bool JsonParse(const std::string &Text, cJsonValue &Result, std::string &Error);
  ///< Parses Text into Result. Returns false and sets Error on syntax errors.

#endif //__VDRSTERNELEC_JSON_H
