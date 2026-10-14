#include "PresetService.h"
#include "SystemPresetsJson.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <vector>

// Helper to split a comma-separated list into tokens
static std::vector<String> splitTokens(const String& s, char delim = ',') {
  std::vector<String> tokens;
  int start = 0;
  int end = s.indexOf(delim);
  while (end != -1) {
    String tok = s.substring(start, end);
    tok.trim();
    if (tok.length() > 0) tokens.push_back(tok);
    start = end + 1;
    end = s.indexOf(delim, start);
  }
  String last = s.substring(start);
  last.trim();
  if (last.length() > 0) tokens.push_back(last);
  return tokens;
}

// Helper to extract a query parameter by key, e.g. "path=threads/iso_metric" or "$select=id,name"
static String extractParam(const char* query, const char* key) {
  if (!query || !key) return "";
  String q = query;
  String target = key;
  target += "=";

  int idx = q.indexOf(target);
  if (idx == -1) return "";

  int start = idx + target.length();
  // Find delimiter (space, &, or end of string)
  int end = start;
  bool inQuotes = false;
  while (end < (int)q.length()) {
    char c = q.charAt(end);
    if (c == '\'' || c == '"') inQuotes = !inQuotes;
    else if (!inQuotes && (c == ' ' || c == '&' || c == '\n' || c == '\r')) break;
    end++;
  }
  String val = q.substring(start, end);
  val.trim();
  if (val.startsWith("\"") && val.endsWith("\"") && val.length() >= 2) {
    val = val.substring(1, val.length() - 1);
  } else if (val.startsWith("'") && val.endsWith("'") && val.length() >= 2) {
    val = val.substring(1, val.length() - 1);
  }
  return val;
}

bool PresetService::dispatch(const char* payload, size_t length, String* reply) {
  if (!payload || length == 0) return false;

  // Trim leading whitespace
  while (length > 0 && (*payload == ' ' || *payload == '\t' || *payload == '\r' || *payload == '\n')) {
    payload++;
    length--;
  }

  if (strncasecmp(payload, "discover", 8) == 0) {
    const char* sub = payload + 8;
    while (*sub == ' ' || *sub == '\t') sub++;
    String path = extractParam(sub, "path");
    return discover(path.c_str(), reply);
  } else if (strncasecmp(payload, "get", 3) == 0) {
    const char* sub = payload + 3;
    while (*sub == ' ' || *sub == '\t') sub++;
    return get(sub, reply);
  }

  if (reply) *reply = "{\"status\":\"error\",\"message\":\"Unknown PRESET sub-command\"}";
  return false;
}

bool PresetService::discover(const char* path, String* reply) {
  // If path is empty, return list of domains
  if (!path || *path == '\0' || strcmp(path, "/") == 0) {
    std::vector<String> domains;

    // 1. From Flash ROM
    for (size_t i = 0; i < SYSTEM_PRESET_ENTRY_COUNT; i++) {
      String d = SYSTEM_PRESET_ENTRIES[i].domain;
      bool exists = false;
      for (const auto& existing : domains) {
        if (existing == d) { exists = true; break; }
      }
      if (!exists) domains.push_back(d);
    }

    // 2. From LittleFS /presets/
    if (LittleFS.exists("/presets")) {
      File root = LittleFS.open("/presets");
      if (root && root.isDirectory()) {
        File f = root.openNextFile();
        while (f) {
          if (f.isDirectory()) {
            String d = f.name();
            // Strip leading slash if present
            if (d.startsWith("/")) d = d.substring(1);
            if (d.indexOf('/') != -1) d = d.substring(d.lastIndexOf('/') + 1);
            if (d.length() > 0 && d != "user") {
              bool exists = false;
              for (const auto& existing : domains) {
                if (existing == d) { exists = true; break; }
              }
              if (!exists) domains.push_back(d);
            }
          }
          f = root.openNextFile();
        }
      }
    }

    // Serialize reply
    JsonDocument outDoc;
    outDoc["status"] = "ok";
    JsonArray arr = outDoc["domains"].to<JsonArray>();
    for (const auto& d : domains) {
      arr.add(d);
    }
    if (reply) serializeJson(outDoc, *reply);
    return true;
  }

  // Otherwise, discover catalogs within a domain (e.g. "threads" or "tapers")
  String targetDomain = path;
  targetDomain.trim();
  if (targetDomain.startsWith("/")) targetDomain = targetDomain.substring(1);
  if (targetDomain.endsWith("/")) targetDomain = targetDomain.substring(0, targetDomain.length() - 1);

  std::vector<String> catalogs;

  // 1. From Flash ROM
  for (size_t i = 0; i < SYSTEM_PRESET_ENTRY_COUNT; i++) {
    if (targetDomain.equalsIgnoreCase(SYSTEM_PRESET_ENTRIES[i].domain)) {
      String c = SYSTEM_PRESET_ENTRIES[i].catalog;
      bool exists = false;
      for (const auto& existing : catalogs) {
        if (existing == c) { exists = true; break; }
      }
      if (!exists) catalogs.push_back(c);
    }
  }

  // 2. From LittleFS /presets/<domain>/
  String fsDir = "/presets/" + targetDomain;
  if (LittleFS.exists(fsDir.c_str())) {
    File dir = LittleFS.open(fsDir.c_str());
    if (dir && dir.isDirectory()) {
      File f = dir.openNextFile();
      while (f) {
        if (!f.isDirectory()) {
          String name = f.name();
          if (name.startsWith("/")) name = name.substring(1);
          if (name.indexOf('/') != -1) name = name.substring(name.lastIndexOf('/') + 1);
          if (name.endsWith(".json")) {
            String c = name.substring(0, name.length() - 5);
            bool exists = false;
            for (const auto& existing : catalogs) {
              if (existing == c) { exists = true; break; }
            }
            if (!exists) catalogs.push_back(c);
          }
        }
        f = dir.openNextFile();
      }
    }
  }

  // 3. From LittleFS /presets/user/<domain>/
  String userFsDir = "/presets/user/" + targetDomain;
  if (LittleFS.exists(userFsDir.c_str())) {
    File dir = LittleFS.open(userFsDir.c_str());
    if (dir && dir.isDirectory()) {
      File f = dir.openNextFile();
      while (f) {
        if (!f.isDirectory()) {
          String name = f.name();
          if (name.startsWith("/")) name = name.substring(1);
          if (name.indexOf('/') != -1) name = name.substring(name.lastIndexOf('/') + 1);
          if (name.endsWith(".json")) {
            String c = name.substring(0, name.length() - 5);
            bool exists = false;
            for (const auto& existing : catalogs) {
              if (existing == c) { exists = true; break; }
            }
            if (!exists) catalogs.push_back(c);
          }
        }
        f = dir.openNextFile();
      }
    }
  }

  JsonDocument outDoc;
  outDoc["status"] = "ok";
  outDoc["domain"] = targetDomain;
  JsonArray arr = outDoc["catalogs"].to<JsonArray>();
  for (const auto& c : catalogs) {
    arr.add(c);
  }
  if (reply) serializeJson(outDoc, *reply);
  return true;
}

bool PresetService::get(const char* query, String* reply) {
  if (!query) {
    if (reply) *reply = "{\"status\":\"error\",\"message\":\"Missing query parameters\"}";
    return false;
  }

  String path = extractParam(query, "path");
  if (path.isEmpty()) {
    if (reply) *reply = "{\"status\":\"error\",\"message\":\"Missing path parameter\"}";
    return false;
  }

  // Parse path components: <domain>/<catalog>[/<item_id>]
  if (path.startsWith("/")) path = path.substring(1);
  if (path.endsWith("/")) path = path.substring(0, path.length() - 1);

  std::vector<String> parts = splitTokens(path, '/');
  if (parts.size() < 2) {
    if (reply) *reply = "{\"status\":\"error\",\"message\":\"Path must include at least <domain>/<catalog>\"}";
    return false;
  }

  String domain = parts[0];
  String catalog = parts[1];
  String itemId = (parts.size() >= 3) ? parts[2] : "";

  // Parse query options
  String selectStr = extractParam(query, "$select");
  String filterStr = extractParam(query, "$filter");
  String metaStr = extractParam(query, "$metadata");
  bool metadataOnly = (metaStr.equalsIgnoreCase("true") || metaStr == "1");

  std::vector<String> selectFields;
  if (!selectStr.isEmpty()) {
    selectFields = splitTokens(selectStr, ',');
  }

  // Simple filter parser: e.g. "choice eq 1" or "series eq 'coarse'" or "id eq 'm12_6g'"
  String filterKey = "";
  String filterVal = "";
  if (!filterStr.isEmpty()) {
    int eqIdx = filterStr.indexOf(" eq ");
    if (eqIdx != -1) {
      filterKey = filterStr.substring(0, eqIdx);
      filterKey.trim();
      filterVal = filterStr.substring(eqIdx + 4);
      filterVal.trim();
      if ((filterVal.startsWith("'") && filterVal.endsWith("'")) ||
          (filterVal.startsWith("\"") && filterVal.endsWith("\""))) {
        filterVal = filterVal.substring(1, filterVal.length() - 1);
      }
    }
  }

  // If itemId wasn't in path, but filter specified "id eq '...'", treat as itemId lookup
  if (itemId.isEmpty() && filterKey == "id" && !filterVal.isEmpty()) {
    itemId = filterVal;
    filterKey = ""; // Consumed as itemId
    filterVal = "";
  }

  // 1. Resolve JSON source: LittleFS user override vs LittleFS domain vs Flash ROM
  String fileContent;
  const char* romJson = nullptr;

  String userPath = "/presets/user/" + domain + "/" + catalog + ".json";
  String stdPath = "/presets/" + domain + "/" + catalog + ".json";

  if (LittleFS.exists(userPath.c_str())) {
    File f = LittleFS.open(userPath.c_str(), "r");
    if (f) { fileContent = f.readString(); f.close(); }
  } else if (LittleFS.exists(stdPath.c_str())) {
    File f = LittleFS.open(stdPath.c_str(), "r");
    if (f) { fileContent = f.readString(); f.close(); }
  } else {
    // Check Flash ROM
    for (size_t i = 0; i < SYSTEM_PRESET_ENTRY_COUNT; i++) {
      if (domain.equalsIgnoreCase(SYSTEM_PRESET_ENTRIES[i].domain) &&
          catalog.equalsIgnoreCase(SYSTEM_PRESET_ENTRIES[i].catalog)) {
        romJson = SYSTEM_PRESET_ENTRIES[i].json;
        break;
      }
    }
  }

  if (fileContent.isEmpty() && !romJson) {
    if (reply) *reply = "{\"status\":\"error\",\"message\":\"Preset catalog not found\"}";
    return false;
  }

  // Fast-path: if full catalog requested with no $select, no $filter, no itemId, no $metadata:
  if (!metadataOnly && itemId.isEmpty() && selectFields.empty() && filterKey.isEmpty()) {
    if (reply) {
      if (!fileContent.isEmpty()) *reply = fileContent;
      else if (romJson) *reply = romJson;
    }
    return true;
  }

  // Otherwise, deserialize and process
  JsonDocument inDoc;
  DeserializationError err = fileContent.isEmpty() ? deserializeJson(inDoc, romJson)
                                                   : deserializeJson(inDoc, fileContent);
  if (err) {
    if (reply) *reply = "{\"status\":\"error\",\"message\":\"Failed to parse catalog JSON\"}";
    return false;
  }

  // 2. Metadata-only request
  if (metadataOnly) {
    JsonDocument outDoc;
    outDoc["status"] = "ok";
    outDoc["meta"] = inDoc["meta"];
    if (reply) serializeJson(outDoc, *reply);
    return true;
  }

  // 3. Single Item Request (by ID or designation)
  if (!itemId.isEmpty()) {
    JsonArray items = inDoc["items"].as<JsonArray>();
    JsonObject matchedItem;
    bool found = false;

    for (JsonObject it : items) {
      const char* idStr = it["id"];
      const char* desStr = it["designation"];
      if ((idStr && itemId.equalsIgnoreCase(idStr)) || (desStr && itemId.equalsIgnoreCase(desStr))) {
        matchedItem = it;
        found = true;
        break;
      }
    }

    if (!found) {
      if (reply) *reply = "{\"status\":\"error\",\"message\":\"Item not found in catalog\"}";
      return false;
    }

    JsonDocument outDoc;
    outDoc["status"] = "ok";

    if (selectFields.empty()) {
      outDoc["item"] = matchedItem;
    } else {
      JsonObject proj = outDoc["item"].to<JsonObject>();
      for (const auto& key : selectFields) {
        if (matchedItem[key.c_str()].is<JsonVariant>()) {
          proj[key.c_str()] = matchedItem[key.c_str()];
        }
      }
    }

    if (reply) serializeJson(outDoc, *reply);
    return true;
  }

  // 4. Catalog query with $select and/or $filter
  JsonArray inItems = inDoc["items"].as<JsonArray>();
  JsonDocument outDoc;
  outDoc["status"] = "ok";
  JsonArray outItems = outDoc["items"].to<JsonArray>();

  for (JsonObject it : inItems) {
    // Check filter condition if present
    if (!filterKey.isEmpty()) {
      JsonVariant val = it[filterKey.c_str()];
      if (val.isNull()) continue;

      if (val.is<int>() || val.is<long>()) {
        long numVal = filterVal.toInt();
        if (val.as<long>() != numVal) continue;
      } else {
        const char* strVal = val.as<const char*>();
        if (!strVal || !filterVal.equalsIgnoreCase(strVal)) continue;
      }
    }

    // Apply projection ($select)
    if (selectFields.empty()) {
      outItems.add(it);
    } else {
      JsonObject proj = outItems.add<JsonObject>();
      for (const auto& key : selectFields) {
        if (it[key.c_str()].is<JsonVariant>()) {
          proj[key.c_str()] = it[key.c_str()];
        }
      }
    }
  }

  if (reply) serializeJson(outDoc, *reply);
  return true;
}
