#include <cassert>
#include <cstring>
#include <iostream>
#include <string>

#include "motion/LatheProfile.h"
#include "mocks/MockProfileStorage.h"

using whatels::motion::MockProfileStorage;

int main() {
  std::cout << "Running LatheProfile + IProfileStorage host tests...\n";

  // 1. Null storage falls back to compiled-in ROM templates cleanly
  setProfileStorage(nullptr);
  {
    char defaultSlug[32] = {0};
    assert(getDefaultCatalogProfileId(defaultSlug, sizeof(defaultSlug)));
    assert(std::strlen(defaultSlug) > 0);

    LatheConfiguration cfg = {};
    assert(loadProfileFromLittleFS("wm210v_stock", &cfg));
    assert(std::strcmp(cfg.profileId, "wm210v_stock") == 0);

    // Mutating operations fail gracefully when no storage is injected
    LatheConfiguration custom = getDefaultProfile();
    std::strcpy(custom.profileId, "custom_1022");
    assert(!saveProfileToLittleFS(custom));
    assert(!deleteProfileFromLittleFS("custom_1022"));
    assert(!setDefaultCatalogProfileId("wm210v_stock"));
  }

  // 2. Inject MockProfileStorage and verify save / load / list / getJson / setDefault / delete
  MockProfileStorage mockStorage;
  setProfileStorage(&mockStorage);

  {
    LatheConfiguration custom = getDefaultProfile();
    std::strcpy(custom.profileId, "custom_1022");
    std::strcpy(custom.modelName, "Custom 10x22 Bench Lathe");
    custom.zAxis.leadscrewPitchMm = 3.0f;
    custom.zAxis.hasDroScale = true;

    assert(saveProfileToLittleFS(custom));
    assert(mockStorage.exists("/presets/profiles.json"));

    // System templates cannot be overwritten
    LatheConfiguration sysOverwrite = getDefaultProfile();
    std::strcpy(sysOverwrite.profileId, "wm210v_stock");
    assert(!saveProfileToLittleFS(sysOverwrite));

    // Load back from MockProfileStorage
    LatheConfiguration loaded = {};
    assert(loadProfileFromLittleFS("custom_1022", &loaded));
    assert(std::strcmp(loaded.profileId, "custom_1022") == 0);
    assert(std::strcmp(loaded.modelName, "Custom 10x22 Bench Lathe") == 0);
    assert(loaded.zAxis.leadscrewPitchMm == 3.0f);
    assert(loaded.hasCapability(CAP_DRO_Z));

    // JSON export contains custom profile
    String json;
    assert(getProfileJsonFromLittleFS("custom_1022", &json));
    assert(json.find("custom_1022") != std::string::npos);

    // List catalog includes both ROM :sys and user :user
    String list;
    assert(listCatalogProfiles(&list));
    assert(list.find("custom_1022:user") != std::string::npos);
    assert(list.find("wm210v_stock:sys") != std::string::npos);

    // Set default to custom_1022 -> cannot delete while default
    assert(setDefaultCatalogProfileId("custom_1022"));
    char defSlug[32] = {0};
    assert(getDefaultCatalogProfileId(defSlug, sizeof(defSlug)));
    assert(std::strcmp(defSlug, "custom_1022") == 0);
    assert(!deleteProfileFromLittleFS("custom_1022"));

    // Reset default back to wm210v_stock -> now custom_1022 can be deleted
    assert(setDefaultCatalogProfileId("wm210v_stock"));
    assert(deleteProfileFromLittleFS("custom_1022"));
    LatheConfiguration gone = {};
    assert(!loadProfileFromLittleFS("custom_1022", &gone));
  }

  setProfileStorage(nullptr);
  std::cout << ">>> ALL LATHE PROFILE STORAGE SEAM TESTS PASSED! <<<\n";
  return 0;
}
