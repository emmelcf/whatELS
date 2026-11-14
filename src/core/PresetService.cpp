#include "PresetService.h"
#include "SystemPresetsJson.h"
#include "SystemProfilesJson.h"
#include "SettingsService.h"
#include "motion/LatheProfile.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <strings.h>
#include <vector>

namespace {

bool isSafeSegment(const String& s) {
  if (s.isEmpty() || s.indexOf("..") != -1 || s.indexOf('/') != -1 || s.indexOf('\\') != -1) {
    return false;
  }
  return true;
}

void appendUnique(std::vector<String>& list, const String& val) {
  if (val.isEmpty()) return;
  for (const auto& existing : list) {
    if (existing.equalsIgnoreCase(val)) return;
  }
  list.push_back(val);
}

std::vector<String> splitTokens(const String& s, char delim = ',') {
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

String extractParam(const char* query, const char* key) {
  if (!query || !key) return "";
  String q = query;
  String target = String(key) + "=";

  int idx = q.indexOf(target);
  if (idx == -1) return "";

  int start = idx + target.length();
  int end = start;
  bool inQuotes = false;
  while (end < static_cast<int>(q.length())) {
    char c = q.charAt(end);
    if (c == '\'' || c == '"') inQuotes = !inQuotes;
    else if (!inQuotes && (c == ' ' || c == '&' || c == '\n' || c == '\r')) break;
    end++;
  }

  String val = q.substring(start, end);
  val.trim();
  if (val.length() >= 2 &&
      ((val.startsWith("\"") && val.endsWith("\"")) ||
       (val.startsWith("'") && val.endsWith("'")))) {
    val = val.substring(1, val.length() - 1);
  }
  return val;
}

String extractBasename(const char* rawName) {
  String name = rawName ? rawName : "";
  if (name.startsWith("/")) name = name.substring(1);
  int slash = name.lastIndexOf('/');
  if (slash != -1) name = name.substring(slash + 1);
  return name;
}

void scanJsonCatalogsInDir(const char* dirPath, std::vector<String>& outCatalogs) {
  if (!LittleFS.exists(dirPath)) return;
  File dir = LittleFS.open(dirPath);
  if (!dir || !dir.isDirectory()) return;

  File f = dir.openNextFile();
  while (f) {
    if (!f.isDirectory()) {
      String name = extractBasename(f.name());
      if (name.endsWith(".json")) {
        appendUnique(outCatalogs, name.substring(0, name.length() - 5));
      }
    }
    f = dir.openNextFile();
  }
}

void ensureUserDomainDir(const String& domain) {
  if (!LittleFS.exists("/presets")) LittleFS.mkdir("/presets");
  if (!LittleFS.exists("/presets/user")) LittleFS.mkdir("/presets/user");
  String domainDir = "/presets/user/" + domain;
  if (!LittleFS.exists(domainDir.c_str())) LittleFS.mkdir(domainDir.c_str());
}

bool loadProfilesAsCatalog(JsonDocument& outDoc) {
  String fileContent;
  if (LittleFS.exists("/presets/profiles.json")) {
    File f = LittleFS.open("/presets/profiles.json", "r");
    if (f) {
      fileContent = f.readString();
      f.close();
    }
  }

  JsonDocument rawDoc;
  DeserializationError err = fileContent.isEmpty()
      ? deserializeJson(rawDoc, SYSTEM_PROFILES_JSON)
      : deserializeJson(rawDoc, fileContent);
  if (err) return false;

  JsonObject meta = outDoc["meta"].to<JsonObject>();
  meta["domain"] = "profiles";
  meta["standard_id"] = "system";
  meta["standard_title"] = "Machine Hardware Profiles";
  meta["version"] = rawDoc["version"] | 1;
  meta["default_profile_id"] = rawDoc["default_profile_id"] | "wm210v_stock";

  JsonArray items = outDoc["items"].to<JsonArray>();
  JsonArray profiles = rawDoc["profiles"].as<JsonArray>();
  for (JsonObject p : profiles) {
    JsonObject item = items.add<JsonObject>();
    const char* id = p["identity"]["profile_id"] | "";
    const char* name = p["identity"]["model_name"] | id;
    item["id"] = id;
    item["designation"] = name;
    for (JsonPair kv : p) {
      item[kv.key()] = kv.value();
    }
  }
  return true;
}

bool removeFromFile(const char* filePath, const char* targetId, bool* outRemoved) {
  *outRemoved = false;
  if (!LittleFS.exists(filePath)) return true;

  File f = LittleFS.open(filePath, "r");
  if (!f) return false;
  String content = f.readString();
  f.close();

  JsonDocument doc;
  if (deserializeJson(doc, content)) return false;

  JsonArray items = doc["items"].as<JsonArray>();
  if (items.isNull()) return true;

  for (size_t i = 0; i < items.size(); ++i) {
    const char* idStr = items[i]["id"];
    if (idStr && strcasecmp(idStr, targetId) == 0) {
      items.remove(i);
      *outRemoved = true;
      break;
    }
  }

  if (!*outRemoved) return true;

  if (items.size() == 0) {
    return LittleFS.remove(filePath);
  }

  File outFile = LittleFS.open(filePath, "w");
  if (!outFile) return false;
  serializeJson(doc, outFile);
  outFile.close();
  return true;
}

} // namespace

bool PresetService::discover(const char* path, String* reply) {
  if (!path || *path == '\0' || strcmp(path, "/") == 0) {
    std::vector<String> domains;
    appendUnique(domains, "profiles");
    appendUnique(domains, "threads");
    appendUnique(domains, "tapers");

    for (size_t i = 0; i < SYSTEM_PRESET_ENTRY_COUNT; ++i) {
      appendUnique(domains, SYSTEM_PRESET_ENTRIES[i].domain);
    }

    if (LittleFS.exists("/presets")) {
      File root = LittleFS.open("/presets");
      if (root && root.isDirectory()) {
        File f = root.openNextFile();
        while (f) {
          if (f.isDirectory()) {
            String d = extractBasename(f.name());
            if (!d.isEmpty() && d != "user") appendUnique(domains, d);
          }
          f = root.openNextFile();
        }
      }
    }

    JsonDocument outDoc;
    outDoc["status"] = "ok";
    JsonArray arr = outDoc["domains"].to<JsonArray>();
    for (const auto& d : domains) arr.add(d);
    if (reply) serializeJson(outDoc, *reply);
    return true;
  }

  String targetDomain = path;
  targetDomain.trim();
  if (targetDomain.startsWith("/")) targetDomain = targetDomain.substring(1);
  if (targetDomain.endsWith("/")) targetDomain = targetDomain.substring(0, targetDomain.length() - 1);
  if (!isSafeSegment(targetDomain)) {
    if (reply) *reply = "{\"status\":\"error\",\"message\":\"Invalid domain path\"}";
    return false;
  }

  std::vector<String> catalogs;
  if (targetDomain.equalsIgnoreCase("profiles")) {
    appendUnique(catalogs, "system");
  }

  for (size_t i = 0; i < SYSTEM_PRESET_ENTRY_COUNT; ++i) {
    if (targetDomain.equalsIgnoreCase(SYSTEM_PRESET_ENTRIES[i].domain)) {
      appendUnique(catalogs, SYSTEM_PRESET_ENTRIES[i].catalog);
    }
  }

  String fsDir = "/presets/" + targetDomain;
  scanJsonCatalogsInDir(fsDir.c_str(), catalogs);

  String userFsDir = "/presets/user/" + targetDomain;
  scanJsonCatalogsInDir(userFsDir.c_str(), catalogs);

  JsonDocument outDoc;
  outDoc["status"] = "ok";
  outDoc["domain"] = targetDomain;
  JsonArray arr = outDoc["catalogs"].to<JsonArray>();
  for (const auto& c : catalogs) arr.add(c);
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
  if (!isSafeSegment(domain) || !isSafeSegment(catalog) || (!itemId.isEmpty() && !isSafeSegment(itemId))) {
    if (reply) *reply = "{\"status\":\"error\",\"message\":\"Invalid path segment\"}";
    return false;
  }

  String selectStr = extractParam(query, "$select");
  String filterStr = extractParam(query, "$filter");
  String metaStr = extractParam(query, "$metadata");
  bool metadataOnly = (metaStr.equalsIgnoreCase("true") || metaStr == "1");

  std::vector<String> selectFields;
  if (!selectStr.isEmpty()) selectFields = splitTokens(selectStr, ',');

  String filterKey;
  String filterVal;
  if (!filterStr.isEmpty()) {
    int eqIdx = filterStr.indexOf(" eq ");
    if (eqIdx != -1) {
      filterKey = filterStr.substring(0, eqIdx);
      filterKey.trim();
      filterVal = filterStr.substring(eqIdx + 4);
      filterVal.trim();
      if (filterVal.length() >= 2 &&
          ((filterVal.startsWith("'") && filterVal.endsWith("'")) ||
           (filterVal.startsWith("\"") && filterVal.endsWith("\"")))) {
        filterVal = filterVal.substring(1, filterVal.length() - 1);
      }
    }
  }

  if (itemId.isEmpty() && filterKey == "id" && !filterVal.isEmpty()) {
    itemId = filterVal;
    filterKey = "";
    filterVal = "";
  }

  JsonDocument inDoc;
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
  } else if (domain.equalsIgnoreCase("profiles") &&
             (catalog.equalsIgnoreCase("system") || catalog.equalsIgnoreCase("profiles"))) {
    if (!loadProfilesAsCatalog(inDoc)) {
      if (reply) *reply = "{\"status\":\"error\",\"message\":\"Failed to parse profiles catalog\"}";
      return false;
    }
    if (!metadataOnly && itemId.isEmpty() && selectFields.empty() && filterKey.isEmpty()) {
      if (reply) serializeJson(inDoc, *reply);
      return true;
    }
  } else {
    for (size_t i = 0; i < SYSTEM_PRESET_ENTRY_COUNT; ++i) {
      if (domain.equalsIgnoreCase(SYSTEM_PRESET_ENTRIES[i].domain) &&
          catalog.equalsIgnoreCase(SYSTEM_PRESET_ENTRIES[i].catalog)) {
        romJson = SYSTEM_PRESET_ENTRIES[i].json;
        break;
      }
    }
  }

  if (inDoc.isNull()) {
    if (fileContent.isEmpty() && !romJson) {
      if (reply) *reply = "{\"status\":\"error\",\"message\":\"Preset catalog not found\"}";
      return false;
    }

    if (!metadataOnly && itemId.isEmpty() && selectFields.empty() && filterKey.isEmpty()) {
      if (reply) *reply = fileContent.isEmpty() ? String(romJson) : fileContent;
      return true;
    }

    DeserializationError err = fileContent.isEmpty() ? deserializeJson(inDoc, romJson)
                                                     : deserializeJson(inDoc, fileContent);
    if (err) {
      if (reply) *reply = "{\"status\":\"error\",\"message\":\"Failed to parse catalog JSON\"}";
      return false;
    }
  }

  if (metadataOnly) {
    JsonDocument outDoc;
    outDoc["status"] = "ok";
    outDoc["meta"] = inDoc["meta"];
    if (reply) serializeJson(outDoc, *reply);
    return true;
  }

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

  JsonArray inItems = inDoc["items"].as<JsonArray>();
  JsonDocument outDoc;
  outDoc["status"] = "ok";
  JsonArray outItems = outDoc["items"].to<JsonArray>();

  for (JsonObject it : inItems) {
    if (!filterKey.isEmpty()) {
      JsonVariant val = it[filterKey.c_str()];
      if (val.isNull()) continue;

      if (val.is<int>() || val.is<long>()) {
        if (val.as<long>() != filterVal.toInt()) continue;
      } else {
        const char* strVal = val.as<const char*>();
        if (!strVal || !filterVal.equalsIgnoreCase(strVal)) continue;
      }
    }

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

bool PresetService::save(const char* jsonPayload, String* reply) {
  if (!jsonPayload || *jsonPayload == '\0') {
    if (reply) *reply = "MISSING_PAYLOAD";
    return false;
  }

  JsonDocument entryDoc;
  if (deserializeJson(entryDoc, jsonPayload) || !entryDoc.is<JsonObject>()) {
    if (reply) *reply = "INVALID_JSON";
    return false;
  }

  String domain = entryDoc["domain"] | "";
  String id = entryDoc["id"] | "";
  String catalog = entryDoc["catalog"] | "custom";
  domain.trim();
  id.trim();
  catalog.trim();

  if (!isSafeSegment(domain) || !isSafeSegment(id) || !isSafeSegment(catalog)) {
    if (reply) *reply = "INVALID_PARAM";
    return false;
  }

  if (!domain.equalsIgnoreCase("profiles") &&
      !domain.equalsIgnoreCase("threads") &&
      !domain.equalsIgnoreCase("tapers")) {
    if (reply) *reply = "INVALID_DOMAIN";
    return false;
  }
  domain.toLowerCase();

  if (domain == "profiles" && isSystemCatalogProfile(id.c_str())) {
    if (reply) *reply = "PROTECTED_SYSTEM_ENTRY";
    return false;
  }

  ensureUserDomainDir(domain);
  String filePath = "/presets/user/" + domain + "/" + catalog + ".json";

  JsonDocument catalogDoc;
  if (LittleFS.exists(filePath.c_str())) {
    File inFile = LittleFS.open(filePath.c_str(), "r");
    if (inFile) {
      String content = inFile.readString();
      inFile.close();
      deserializeJson(catalogDoc, content);
    }
  }

  if (catalogDoc["meta"].isNull()) {
    JsonObject meta = catalogDoc["meta"].to<JsonObject>();
    meta["domain"] = domain;
    meta["catalog"] = catalog;
    meta["user"] = true;
  }

  JsonArray items = catalogDoc["items"].as<JsonArray>();
  if (items.isNull()) {
    items = catalogDoc["items"].to<JsonArray>();
  }

  bool replaced = false;
  for (size_t i = 0; i < items.size(); ++i) {
    const char* existingId = items[i]["id"];
    if (existingId && strcasecmp(existingId, id.c_str()) == 0) {
      items[i].set(entryDoc.as<JsonObject>());
      replaced = true;
      break;
    }
  }
  if (!replaced) {
    items.add(entryDoc.as<JsonObject>());
  }

  File outFile = LittleFS.open(filePath.c_str(), "w");
  if (!outFile) {
    if (reply) *reply = "STORAGE_WRITE_FAILED";
    return false;
  }
  serializeJson(catalogDoc, outFile);
  outFile.close();

  if (reply) *reply = id;
  return true;
}

bool PresetService::remove(const char* jsonPayload, String* reply) {
  if (!jsonPayload || *jsonPayload == '\0') {
    if (reply) *reply = "MISSING_PAYLOAD";
    return false;
  }

  JsonDocument reqDoc;
  if (deserializeJson(reqDoc, jsonPayload) || !reqDoc.is<JsonObject>()) {
    if (reply) *reply = "INVALID_JSON";
    return false;
  }

  String domain = reqDoc["domain"] | "";
  String id = reqDoc["id"] | "";
  String catalog = reqDoc["catalog"] | "";
  domain.trim();
  id.trim();
  catalog.trim();

  if (!isSafeSegment(domain) || !isSafeSegment(id) || (!catalog.isEmpty() && !isSafeSegment(catalog))) {
    if (reply) *reply = "INVALID_PARAM";
    return false;
  }
  domain.toLowerCase();

  if (domain == "profiles") {
    if (strcasecmp(SettingsService::getActiveProfile().profileId, id.c_str()) == 0) {
      if (reply) *reply = "PROTECTED_ACTIVE_PROFILE";
      return false;
    }
    if (isSystemCatalogProfile(id.c_str())) {
      if (reply) *reply = "PROTECTED_SYSTEM_ENTRY";
      return false;
    }
    char defaultSlug[32] = {0};
    if (getDefaultCatalogProfileId(defaultSlug, sizeof(defaultSlug)) &&
        strcasecmp(defaultSlug, id.c_str()) == 0) {
      if (reply) *reply = "PROTECTED_DEFAULT_PROFILE";
      return false;
    }
  }

  bool removed = false;
  if (!catalog.isEmpty()) {
    String filePath = "/presets/user/" + domain + "/" + catalog + ".json";
    if (!removeFromFile(filePath.c_str(), id.c_str(), &removed)) {
      if (reply) *reply = "STORAGE_WRITE_FAILED";
      return false;
    }
  } else {
    String userDir = "/presets/user/" + domain;
    if (LittleFS.exists(userDir.c_str())) {
      File dir = LittleFS.open(userDir.c_str());
      if (dir && dir.isDirectory()) {
        File f = dir.openNextFile();
        while (f && !removed) {
          if (!f.isDirectory()) {
            String name = extractBasename(f.name());
            if (name.endsWith(".json")) {
              String filePath = userDir + "/" + name;
              if (!removeFromFile(filePath.c_str(), id.c_str(), &removed)) {
                if (reply) *reply = "STORAGE_WRITE_FAILED";
                return false;
              }
            }
          }
          f = dir.openNextFile();
        }
      }
    }
  }

  if (!removed) {
    if (reply) *reply = "NOT_FOUND_OR_ROM_PROTECTED";
    return false;
  }

  if (reply) *reply = id;
  return true;
}
