#include <cassert>
#include <cmath>
#include <iostream>
#include <string>

#include "core/MachineModel.h"
#include "core/PersistentSetting.h"
#include "mocks/MockSettingsStorage.h"

using namespace whatels::core;

static int s_callbackCount = 0;
static bool s_lastBool = false;

static void onBoolChange(const bool& val) {
  s_callbackCount++;
  s_lastBool = val;
}

static void testPersistentSettingWithMockStorage() {
  MockSettingsStorage mock;
  PersistentSettingBase::setDefaultStorage(&mock);

  // Pre-seed some keys
  mock.seedBool("prefs", "dia", false);
  mock.seedI32("prefs", "measure", 2);
  mock.seedFloat("sys", "mpg_z_ppr", 400.0f);
  mock.seedString("sys", "wifi_host", "lathe-01");

  PersistentSetting<bool> diaMode("prefs", "dia", true, onBoolChange);
  PersistentSetting<int> measure("prefs", "measure", 0);
  PersistentSetting<long> jogStep("prefs", "move_step", 1000);
  PersistentSetting<uint8_t> keyBind("sys", "k_left", 42);
  PersistentSetting<float> mpgZ("sys", "mpg_z_ppr", 600.0f);
  PersistentSetting<String> host("sys", "wifi_host", "whatels");

  diaMode.init();
  measure.init();
  jogStep.init();
  keyBind.init();
  mpgZ.init();
  host.init();

  // Seeded values loaded
  assert(diaMode.get() == false);
  assert(measure.get() == 2);
  assert(fabsf(mpgZ.get() - 400.0f) < 0.001f);
  assert(host.get() == "lathe-01");

  // Unseeded values fall back to defaults
  assert(jogStep.get() == 1000);
  assert(keyBind.get() == 42);

  // Dirty-check suppresses redundant writes
  s_callbackCount = 0;
  assert(diaMode.set(false) == false);
  assert(s_callbackCount == 0);
  assert(mpgZ.set(400.0f) == false);

  // Actual mutation persists to MockSettingsStorage and fires callback
  assert(diaMode.set(true) == true);
  assert(s_callbackCount == 1);
  assert(s_lastBool == true);
  assert(mock.namespaces["prefs"]["dia"] == "1");

  assert(jogStep.set(2500) == true);
  assert(mock.namespaces["prefs"]["move_step"] == "2500");

  assert(keyBind.set(99) == true);
  assert(mock.namespaces["sys"]["k_left"] == "99");

  assert(host.set(String("shop-lathe")) == true);
  assert(mock.namespaces["sys"]["wifi_host"] == "shop-lathe");

  // Per-instance storage override
  MockSettingsStorage isolated;
  PersistentSetting<int> custom("prefs", "custom", 7);
  custom.setStorage(&isolated);
  custom.init();
  assert(custom.get() == 7);
  assert(custom.set(15) == true);
  assert(isolated.namespaces["prefs"]["custom"] == "15");
  assert(mock.namespaces["prefs"].find("custom") == mock.namespaces["prefs"].end());

  PersistentSettingBase::setDefaultStorage(nullptr);
}

static void testIntentStateModel() {
  IntentState intent = {};
  intent.thread.format = THREAD_TPI;
  intent.thread.authorPitch = 13.0f;
  intent.thread.dupr = 19538;
  intent.taper.modeType = TAPER_RATIO;
  intent.taper.authorVal = 3.5f;
  intent.taper.coneRatio = 1.0f / 7.0f;
  intent.taper.presetId = -1;
  intent.feedStepDu = 1000;
  intent.jogStepDu = 500;
  intent.direction = -1;
  intent.targetDistDu = -1;

  assert(intent.thread.format == THREAD_TPI);
  assert(fabsf(intent.thread.authorPitch - 13.0f) < 0.0001f);
  assert(intent.thread.dupr == 19538);
  assert(intent.taper.modeType == TAPER_RATIO);
  assert(fabsf(intent.taper.authorVal - 3.5f) < 0.0001f);
  assert(intent.taper.presetId == -1);
  assert(intent.direction == -1);
  assert(intent.targetDistDu == -1);

  intent.direction = 1;
  intent.targetDistDu = 250000; // 25.0 mm finite stroke
  assert(intent.direction == 1);
  assert(intent.targetDistDu == 250000);
}

int main() {
  std::cout << "Running PersistentSetting + MockSettingsStorage unit tests...\n";
  testPersistentSettingWithMockStorage();
  testIntentStateModel();
  std::cout << "ALL SETTINGS STORAGE TESTS PASSED!\n";
  return 0;
}
