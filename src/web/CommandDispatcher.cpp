#include "CommandDispatcher.h"
#include "Motion.h"
#include "Axis.h"
#include "Config.h"
#include "Input.h"
#include "MachineModel.h"
#include "RichClientAdapter.h"
#include "WebServerModule.h"
#include "SettingsService.h"
#include "PresetService.h"
#include "modes/ModeRegistry.h"
#include "modes/composite/UnifiedGearboxMode.h"
#include "motion/MotionConversions.h"

static Axis* parseAxis(char c) {
  if (c == 'Z' || c == 'z') return &z;
  if (c == 'X' || c == 'x') return &x;
  return nullptr;
}

bool CommandDispatcher::dispatch(const char* payload, size_t length, String* reply, uint8_t clientId) {
  if (!payload || length == 0) return false;

  // Trim leading whitespace
  while (length > 0 && (*payload == ' ' || *payload == '\t' || *payload == '\r' || *payload == '\n')) {
    payload++;
    length--;
  }
  // Trim trailing whitespace
  while (length > 0 && (payload[length - 1] == ' ' || payload[length - 1] == '\t' || payload[length - 1] == '\r' || payload[length - 1] == '\n')) {
    length--;
  }
  if (length == 0) return false;

  // Guarantee clean, null-terminated local buffer for all string tokens
  char cleanBuf[256];
  if (length >= sizeof(cleanBuf)) length = sizeof(cleanBuf) - 1;
  memcpy(cleanBuf, payload, length);
  cleanBuf[length] = '\0';
  payload = cleanBuf;

  // 1. Sysinfo request
  if (length >= 8 && strncmp(payload, "?sysinfo", 8) == 0) {
    String rep = "SYSINFO.version=" CONTROLLER_VERSION_TEXT "\n";
    rep += "SYSINFO.chip=" + String(ESP.getChipModel()) + " rev " + String(ESP.getChipRevision()) + "\n";
    rep += "SYSINFO.flash=" + String(ESP.getFlashChipSize() / 1024 / 1024) + " MB\n";
    rep += "SYSINFO.heap=" + String(ESP.getFreeHeap() / 1024) + " KB\n";
#if ENABLE_SPINDLE_SIM
    rep += "SYSINFO.features=spindleSim\n";
#endif
    if (reply) *reply = rep;
    return true;
  }

  // 2. Settings Namespace (SETTINGS.get / SETTINGS.set / SETTINGS.reset / SETTINGS.preset / SETTINGS.getGroup / SETTINGS.resetGroup)
  if (length >= 8 && strncasecmp(payload, "SETTINGS", 8) == 0) {
    const char* sub = payload + 8;
    if (*sub == '.') sub++;
    if (strncasecmp(sub, "getGroup=", 9) == 0) {
      return SettingsService::getGroup(sub + 9, reply);
    } else if (strncasecmp(sub, "resetGroup=", 11) == 0) {
      return SettingsService::resetGroup(sub + 11, reply);
    } else if (strcasecmp(sub, "preset.list") == 0) {
      return SettingsService::listPresets(reply);
    } else if (strncasecmp(sub, "preset.get=", 11) == 0) {
      return SettingsService::getPresetJson(sub + 11, reply);
    } else if (strncasecmp(sub, "preset.apply=", 13) == 0) {
      return SettingsService::applyPreset(sub + 13, reply);
    } else if (strcasecmp(sub, "preset.save") == 0) {
      return SettingsService::saveCurrentAsPreset(nullptr, reply);
    } else if (strncasecmp(sub, "preset.save=", 12) == 0) {
      return SettingsService::saveCurrentAsPreset(sub + 12, reply);
    } else if (strncasecmp(sub, "preset.delete=", 14) == 0) {
      return SettingsService::deletePreset(sub + 14, reply);
    } else if (strncasecmp(sub, "preset.setDefault=", 18) == 0) {
      return SettingsService::setDefaultPreset(sub + 18, reply);
    } else if (strcasecmp(sub, "preset.resetToDefault") == 0) {
      return SettingsService::resetToDefaultPreset(reply);
    } else if (strncasecmp(sub, "preset=", 7) == 0) {
      return SettingsService::setPreset(sub + 7, reply);
    } else if (strncasecmp(sub, "get", 3) == 0) {
      const char* key = (sub[3] == '=') ? sub + 4 : nullptr;
      return SettingsService::get(key, reply);
    } else if (strncasecmp(sub, "set=", 4) == 0) {
      return SettingsService::set(sub + 4, reply);
    } else if (strncasecmp(sub, "reset=", 6) == 0) {
      return SettingsService::reset(sub + 6, reply);
    } else if (strncasecmp(sub, "schema", 6) == 0) {
      const char* name = (sub[6] == '=') ? sub + 7 : nullptr;
      return SettingsService::getSchema(name, reply);
    }
  }

  // 2b. Preset Namespace (PRESET.discover / PRESET.get)
  if (length >= 6 && strncasecmp(payload, "PRESET", 6) == 0) {
    const char* sub = payload + 6;
    if (*sub == '.') sub++;
    while (*sub == ' ' || *sub == '\t') sub++;
    return PresetService::dispatch(sub, strlen(sub), reply);
  }

  // 2c. Mode Namespace (MODE.discover)
  if (length >= 4 && strncasecmp(payload, "MODE", 4) == 0) {
    const char* sub = payload + 4;
    if (*sub == '.') sub++;
    while (*sub == ' ' || *sub == '\t') sub++;
    if (strncasecmp(sub, "discover", 8) == 0) {
      const char* q = strchr(sub, '?');
      uint32_t caps = 0;
      bool includeAll = false;

      if (q) {
        q++;
        if (strncasecmp(q, "all=1", 5) == 0 || strncasecmp(q, "all=true", 8) == 0) {
          includeAll = true;
          caps = 0xFFFFFFFF;
        } else if (strncasecmp(q, "caps=", 5) == 0) {
          caps = (uint32_t)strtoul(q + 5, nullptr, 0);
        } else if (strncasecmp(q, "profile=", 8) == 0) {
          const char* profSlug = q + 8;
          const LatheConfiguration* prof = findPresetBySlug(profSlug);
          if (prof) {
            caps = prof->capabilities;
          } else {
            LatheConfiguration loaded;
            if (loadProfileFromLittleFS(profSlug, &loaded)) {
              caps = loaded.capabilities;
            } else {
              caps = SettingsService::getActiveProfile().capabilities;
            }
          }
        } else {
          caps = SettingsService::getActiveProfile().capabilities;
        }
      } else {
        caps = SettingsService::getActiveProfile().capabilities;
      }

      if (reply) {
        *reply = ModeRegistry::serializeDiscoveryJson(caps, includeAll) + "\n";
      }
      return true;
    }
  }

  // 3. Domain State Queries (?get=K,I / ?state / ?get=ALL)
  if ((length >= 4 && strncmp(payload, "?get", 4) == 0) ||
      (length >= 6 && strncmp(payload, "?state", 6) == 0)) {
    uint8_t mask = parseDomainQuery(payload);
    MachineModel m = MachineModel::captureAll();
    String rep = RichClientAdapter::buildTelemetry(m, mask);
    if (reply) *reply = rep;
    return true;
  }

  // 4. Canonical CMD.* Namespace
  if (length >= 4 && strncmp(payload, "CMD.", 4) == 0) {
    const char* cmd = payload + 4;
    size_t cmdLen = length - 4;

    // CMD.get=<domains>
    if (cmdLen >= 4 && strncmp(cmd, "get=", 4) == 0) {
      uint8_t mask = parseDomainQuery(cmd + 4);
      MachineModel m = MachineModel::captureAll();
      String rep = RichClientAdapter::buildTelemetry(m, mask);
      if (reply) *reply = rep;
      return true;
    }

    // CMD.modes alias for MODE.discover
    if (cmdLen >= 5 && strncasecmp(cmd, "modes", 5) == 0) {
      if (reply) {
        uint32_t caps = SettingsService::getActiveProfile().capabilities;
        *reply = ModeRegistry::serializeDiscoveryJson(caps, false) + "\n";
      }
      return true;
    }

    // CMD.settings.get / CMD.settings.set
    if (cmdLen >= 12 && strncasecmp(cmd, "settings.", 9) == 0) {
      const char* sub = cmd + 9;
      if (strncasecmp(sub, "get", 3) == 0) {
        const char* key = (sub[3] == '=') ? sub + 4 : nullptr;
        return SettingsService::get(key, reply);
      } else if (strncasecmp(sub, "set=", 4) == 0) {
        return SettingsService::set(sub + 4, reply);
      }
    }

    // =========================================================================
    // CANONICAL RICH CLIENT MOTION COMMANDS (ADR 0001 & ADR 0002)
    // =========================================================================

    // CMD.startFeed=<payload>
    // Primary Key-Value: axis:<ax>,sync:<sync>,feed:<rate>,dir:<dir>,[dist:<dist>]
    // Legacy Positional: <axis>,<sync>,<feedRate>,<dir>,[dist]
    if (cmdLen >= 10 && strncasecmp(cmd, "startFeed=", 10) == 0) {
      const char* p = cmd + 10;
      AxisId axis = AXIS_Z;
      bool isSync = true;
      int32_t feedRateDu = 0;
      int8_t dir = 0;
      int32_t distDu = -1;
      bool hasFeed = false;
      bool hasDir = false;

      // Check if Key-Value syntax (contains ':')
      if (strchr(p, ':') != nullptr) {
        const char* cur = p;
        while (*cur) {
          while (*cur == ' ' || *cur == ',') cur++;
          if (!*cur) break;

          // Parse key
          char key[16] = {0};
          size_t kIdx = 0;
          while (*cur && *cur != ':' && *cur != '=' && *cur != ',' && kIdx < sizeof(key) - 1) {
            key[kIdx++] = *cur++;
          }
          if (*cur != ':' && *cur != '=') {
            while (*cur && *cur != ',') cur++;
            continue;
          }
          cur++; // Skip ':' or '='

          // Parse value (handling optional double quotes)
          char val[64] = {0};
          size_t vIdx = 0;
          if (*cur == '"') {
            cur++; // Skip opening quote
            while (*cur && *cur != '"' && vIdx < sizeof(val) - 1) {
              val[vIdx++] = *cur++;
            }
            if (*cur == '"') cur++; // Skip closing quote
          } else {
            while (*cur && *cur != ',' && vIdx < sizeof(val) - 1) {
              val[vIdx++] = *cur++;
            }
          }

          // Map key to parameter
          if (strcasecmp(key, "axis") == 0 || strcasecmp(key, "a") == 0) {
            axis = MotionConversions::parseAxisId(val, AXIS_Z);
          } else if (strcasecmp(key, "sync") == 0 || strcasecmp(key, "s") == 0) {
            isSync = (strcasecmp(val, "1") == 0 || strcasecmp(val, "sync") == 0 || strcasecmp(val, "true") == 0);
          } else if (strcasecmp(key, "feed") == 0 || strcasecmp(key, "rate") == 0 || strcasecmp(key, "f") == 0) {
            if (strchr(val, '.')) {
              feedRateDu = MotionConversions::mmToDu(atof(val));
            } else {
              feedRateDu = atol(val);
            }
            hasFeed = (feedRateDu > 0);
          } else if (strcasecmp(key, "dir") == 0 || strcasecmp(key, "d") == 0) {
            dir = MotionConversions::semanticToDirection(axis, val);
            hasDir = (dir != 0);
          } else if (strcasecmp(key, "dist") == 0) {
            if (strchr(val, '.')) {
              distDu = MotionConversions::mmToDu(atof(val));
            } else {
              distDu = atol(val);
            }
          }
        }

        if (!hasFeed || !hasDir) {
          if (reply) *reply = "ERR:CMD.startFeed requires feed:<rate> and dir:<dir>\n";
          return false;
        }
      } else {
        // Fallback: Positional CSV <axis>,<sync>,<feedRate>,<dir>,[dist]
        char axisToken[8] = {0};
        char syncToken[16] = {0};
        char rateToken[32] = {0};
        char dirToken[16] = {0};
        char distToken[32] = {0};

        int parsed = sscanf(p, "%7[^,],%15[^,],%31[^,],%15[^,],%31s",
                            axisToken, syncToken, rateToken, dirToken, distToken);
        if (parsed < 4) {
          if (reply) *reply = "ERR:CMD.startFeed requires axis:<axis>,sync:<sync>,feed:<rate>,dir:<dir>,[dist:<dist>]\n";
          return false;
        }

        axis = MotionConversions::parseAxisId(axisToken, AXIS_Z);
        isSync = (strcasecmp(syncToken, "1") == 0 || strcasecmp(syncToken, "sync") == 0 || strcasecmp(syncToken, "true") == 0);
        dir = MotionConversions::semanticToDirection(axis, dirToken);
        if (dir == 0) {
          if (reply) *reply = "ERR:CMD.startFeed invalid direction\n";
          return false;
        }

        if (strchr(rateToken, '.')) {
          feedRateDu = MotionConversions::mmToDu(atof(rateToken));
        } else {
          feedRateDu = atol(rateToken);
        }

        if (parsed >= 5 && distToken[0] && strcmp(distToken, "-1") != 0) {
          if (strchr(distToken, '.')) {
            distDu = MotionConversions::mmToDu(atof(distToken));
          } else {
            distDu = atol(distToken);
          }
        }
      }

      UnifiedGearboxMode* gb = ModeRegistry::getGearboxMode();
      if (!gb) {
        if (reply) *reply = "ERR:Gearbox mode not registered\n";
        return false;
      }

      ModeRegistry::activateMode(ModeIds::GEARBOX);
      bool ok = gb->startFeed(axis, isSync, feedRateDu, dir, distDu);
      if (ok) {
        if (reply) *reply = "OK:CMD.startFeed\n";
        return true;
      } else {
        if (reply) *reply = "ERR:CMD.startFeed failed\n";
        return false;
      }
    }

    // CMD.stop
    if ((cmdLen == 4 && strncasecmp(cmd, "stop", 4) == 0) ||
        (cmdLen == 5 && strncasecmp(cmd, "stop\n", 5) == 0)) {
      IELSMode* active = ModeRegistry::getActiveMode();
      if (active && active->isRunning()) {
        active->stop(StopType::NORMAL_STOP);
      }
      stopVelocityFeed();
      SettingsService::requestIsOn(false);
      if (reply) *reply = "OK:CMD.stop\n";
      return true;
    }

    // =========================================================================
    // TRANSITIONAL / COMPATIBILITY MODAL SETTERS (QUARANTINED - ADR 0002)
    // =========================================================================

    // CMD.feedDupr=<du>
    if (cmdLen >= 9 && strncmp(cmd, "feedDupr=", 9) == 0) {
      long val = atol(cmd + 9);
      setFeedrateDupr(val, false);
      return true;
    }
    // CMD.feedMm=<mm>
    if (cmdLen >= 7 && strncmp(cmd, "feedMm=", 7) == 0) {
      float val = atof(cmd + 7);
      setFeedrateMm(val, false);
      return true;
    }
    // CMD.feedTpi=<tpi>
    if (cmdLen >= 8 && strncmp(cmd, "feedTpi=", 8) == 0) {
      float val = atof(cmd + 8);
      setFeedrateTpi(val, false);
      return true;
    }
    // CMD.feedInch=<inch>
    if (cmdLen >= 9 && strncmp(cmd, "feedInch=", 9) == 0) {
      float val = atof(cmd + 9);
      setFeedrateInch(val, false);
      return true;
    }
    // CMD.feedRev
    if (cmdLen == 7 && strncmp(cmd, "feedRev", 7) == 0) {
      IELSMode* active = ModeRegistry::getActiveMode();
      if (active && !active->canExecuteCommand(CommandId::CMD_SET_DIR, active->getState())) {
        if (reply) *reply = "ERR:CMD.feedRev=locked during active motion\n";
        return false;
      }
      reverseFeedDirection();
      return true;
    }
    // CMD.feedStepDu=<du>
    if (cmdLen >= 11 && strncmp(cmd, "feedStepDu=", 11) == 0) {
      long val = atol(cmd + 11);
      setFeedStepDu(val);
      return true;
    }
    // CMD.feedStepCycle
    if (cmdLen == 13 && strncmp(cmd, "feedStepCycle", 13) == 0) {
      IELSMode* active = ModeRegistry::getActiveMode();
      if (active && !active->canExecuteCommand(CommandId::CMD_STEP_CYCLE, active->getState())) {
        if (reply) *reply = "ERR:CMD.feedStepCycle=locked during active motion\n";
        return false;
      }
      cycleFeedStep();
      return true;
    }
    // CMD.jogStepDu=<du>
    if (cmdLen >= 10 && strncmp(cmd, "jogStepDu=", 10) == 0) {
      long val = atol(cmd + 10);
      if (val > 0) SettingsService::setJogStepDu(val);
      return true;
    }
    // CMD.jogHold=<axis>,<semanticDir>
    if (cmdLen >= 8 && strncmp(cmd, "jogHold=", 8) == 0) {
      char ax = cmd[8];
      const char* p = strchr(cmd + 8, ',');
      if (p) {
        p++;
        while (*p == ' ' || *p == '\t') p++;
        byte code = 0;
        if (ax == 'Z' || ax == 'z') {
          if (strcasecmp(p, "CHUCK") == 0 || strcasecmp(p, "LEFT") == 0 || strcmp(p, "-1") == 0) code = B_LEFT;
          else if (strcasecmp(p, "TAIL") == 0 || strcasecmp(p, "RIGHT") == 0 || strcmp(p, "1") == 0 || strcmp(p, "+1") == 0) code = B_RIGHT;
        } else if (ax == 'X' || ax == 'x') {
          if (strcasecmp(p, "INFEED") == 0 || strcasecmp(p, "IN") == 0 || strcasecmp(p, "UP") == 0 || strcmp(p, "-1") == 0) code = B_UP;
          else if (strcasecmp(p, "RETRACT") == 0 || strcasecmp(p, "OUT") == 0 || strcasecmp(p, "DOWN") == 0 || strcmp(p, "1") == 0 || strcmp(p, "+1") == 0) code = B_DOWN;
        }
        if (code != 0) {
          queueWebUiEvent(code, true);
          return true;
        }
      }
    }
    // CMD.jogRelease=<axis> or CMD.jogRelease
    if (cmdLen >= 11 && strncmp(cmd, "jogRelease=", 11) == 0) {
      char ax = cmd[11];
      if (ax == 'Z' || ax == 'z') {
        queueWebUiEvent(B_LEFT, false);
        queueWebUiEvent(B_RIGHT, false);
        return true;
      } else if (ax == 'X' || ax == 'x') {
        queueWebUiEvent(B_UP, false);
        queueWebUiEvent(B_DOWN, false);
        return true;
      } else if (strcasecmp(cmd + 11, "ALL") == 0) {
        queueWebUiEvent(B_LEFT, false);
        queueWebUiEvent(B_RIGHT, false);
        queueWebUiEvent(B_UP, false);
        queueWebUiEvent(B_DOWN, false);
        return true;
      }
    }
    if (cmdLen == 10 && strncmp(cmd, "jogRelease", 10) == 0) {
      queueWebUiEvent(B_LEFT, false);
      queueWebUiEvent(B_RIGHT, false);
      queueWebUiEvent(B_UP, false);
      queueWebUiEvent(B_DOWN, false);
      return true;
    }
    // CMD.jogStep=<axis>,<semanticDir>
    if (cmdLen >= 8 && strncmp(cmd, "jogStep=", 8) == 0) {
      char ax = cmd[8];
      const char* p = strchr(cmd + 8, ',');
      if (p) {
        p++;
        while (*p == ' ' || *p == '\t') p++;
        byte code = 0;
        if (ax == 'Z' || ax == 'z') {
          if (strcasecmp(p, "CHUCK") == 0 || strcasecmp(p, "LEFT") == 0 || strcmp(p, "-1") == 0) code = B_LEFT;
          else if (strcasecmp(p, "TAIL") == 0 || strcasecmp(p, "RIGHT") == 0 || strcmp(p, "1") == 0 || strcmp(p, "+1") == 0) code = B_RIGHT;
        } else if (ax == 'X' || ax == 'x') {
          if (strcasecmp(p, "INFEED") == 0 || strcasecmp(p, "IN") == 0 || strcasecmp(p, "UP") == 0 || strcmp(p, "-1") == 0) code = B_UP;
          else if (strcasecmp(p, "RETRACT") == 0 || strcasecmp(p, "OUT") == 0 || strcasecmp(p, "DOWN") == 0 || strcmp(p, "1") == 0 || strcmp(p, "+1") == 0) code = B_DOWN;
        }
        if (code != 0) {
          queueWebUiEvent(code, true);
          queueWebUiEvent(code, false);
          return true;
        }
      }
    }
    // CMD.taperRatio=<ratioK>
    if (cmdLen >= 11 && strncmp(cmd, "taperRatio=", 11) == 0) {
      float val = atof(cmd + 11);
      setTaperRatio(val);
      return true;
    }
    // CMD.taperAngle=<angleDeg>
    if (cmdLen >= 11 && strncmp(cmd, "taperAngle=", 11) == 0) {
      float val = atof(cmd + 11);
      setTaperAngle(val);
      return true;
    }
    // CMD.taperPreset=<presetId>,<angleDeg>,<ratioVal>
    if (cmdLen >= 12 && strncmp(cmd, "taperPreset=", 12) == 0) {
      int presetId = 0;
      float angleDeg = 0.0f;
      float ratioVal = 0.0f;
      sscanf(cmd + 12, "%d,%f,%f", &presetId, &angleDeg, &ratioVal);
      setTaperPreset(presetId, angleDeg, ratioVal);
      return true;
    }
    // CMD.axisZero=<axis>
    if (cmdLen >= 9 && strncmp(cmd, "axisZero=", 9) == 0) {
      IELSMode* active = ModeRegistry::getActiveMode();
      if (active && !active->canExecuteCommand(CommandId::CMD_ZERO_AXIS, active->getState())) {
        if (reply) *reply = "ERR:CMD.axisZero=locked during active motion\n";
        return false;
      }
      Axis* a = parseAxis(cmd[9]);
      if (a) setAxisZero(a);
      return true;
    }
    // CMD.axisPosDu=<axis>,<posDu>
    if (cmdLen >= 10 && strncmp(cmd, "axisPosDu=", 10) == 0) {
      Axis* a = parseAxis(cmd[10]);
      if (a && cmd[11] == ',') {
        long du = atol(cmd + 12);
        setAxisPositionDu(a, du);
        return true;
      }
    }
    // CMD.axisLimitDu=<axis>,<left|right>,<limitDu>
    if (cmdLen >= 12 && strncmp(cmd, "axisLimitDu=", 12) == 0) {
      char ax = cmd[12];
      Axis* a = parseAxis(ax);
      const char* p = strchr(cmd + 12, ',');
      if (a && p) {
        p++;
        bool isLeft = (*p == 'L' || *p == 'l' || *p == '1' || *p == 'U' || *p == 'u');
        const char* p2 = strchr(p, ',');
        if (p2) {
          long du = atol(p2 + 1);
          setAxisLimitDu(a, isLeft, du);
          return true;
        }
      }
    }
    // CMD.axisClearLimit=<axis>,<left|right>
    if (cmdLen >= 15 && strncmp(cmd, "axisClearLimit=", 15) == 0) {
      char ax = cmd[15];
      Axis* a = parseAxis(ax);
      const char* p = strchr(cmd + 15, ',');
      if (a && p) {
        p++;
        bool isLeft = (*p == 'L' || *p == 'l' || *p == '1' || *p == 'U' || *p == 'u');
        clearAxisLimit(a, isLeft);
        return true;
      }
    }
    // CMD.gearAxis=<Z|X|0|1>
    // CMD.gearAxis=<Z|X|ZX|Z+X|0|1|2>
    if (cmdLen >= 9 && strncmp(cmd, "gearAxis=", 9) == 0) {
      IELSMode* active = ModeRegistry::getActiveMode();
      if (active && !active->canExecuteCommand(CommandId::CMD_SET_AXIS, active->getState())) {
        if (reply) *reply = "ERR:CMD.gearAxis=locked during active motion\n";
        return false;
      }
      const char* val = cmd + 9;
      const char* axStr = "Z";
      int targetLowLevelMode = static_cast<int>(LatheMode::NORMAL);
      if (strcasecmp(val, "X") == 0 || strcmp(val, "1") == 0) {
        axStr = "X";
        targetLowLevelMode = static_cast<int>(LatheMode::XGEAR);
      } else if (strcasecmp(val, "ZX") == 0 || strcasecmp(val, "Z+X") == 0 || strcmp(val, "2") == 0 || strcasecmp(val, "TAPER") == 0) {
        axStr = "ZX";
        targetLowLevelMode = static_cast<int>(LatheMode::CONE);
      }
      UnifiedGearboxMode* gb = ModeRegistry::getGearboxMode();
      if (gb) {
        gb->setActiveAxis(axStr);
        if (gb->isSync()) {
          setOperatingMode(targetLowLevelMode);
        }
        if (reply) *reply = String("OK:CMD.gearAxis=") + axStr + "\n";
        return true;
      }
      return false;
    }
    // CMD.gearSync=<1|0|SYNC|ASYNC|true|false>
    if (cmdLen >= 9 && strncmp(cmd, "gearSync=", 9) == 0) {
      IELSMode* active = ModeRegistry::getActiveMode();
      if (active && !active->canExecuteCommand(CommandId::CMD_SET_SYNC, active->getState())) {
        if (reply) *reply = "ERR:CMD.gearSync=locked during active motion\n";
        return false;
      }
      bool sync = (strcasecmp(cmd + 9, "0") != 0 && strcasecmp(cmd + 9, "ASYNC") != 0 && strcasecmp(cmd + 9, "false") != 0);
      UnifiedGearboxMode* gb = ModeRegistry::getGearboxMode();
      if (gb) {
        gb->setSync(sync);
        if (!sync) {
          setOperatingMode(static_cast<int>(LatheMode::ASYNC));
        } else {
          const char* curAx = gb->getActiveAxis();
          if (strcasecmp(curAx, "X") == 0) setOperatingMode(static_cast<int>(LatheMode::XGEAR));
          else if (strcasecmp(curAx, "ZX") == 0) setOperatingMode(static_cast<int>(LatheMode::CONE));
          else setOperatingMode(static_cast<int>(LatheMode::NORMAL));
        }
        if (reply) *reply = String("OK:CMD.gearSync=") + (sync ? "1" : "0") + "\n";
        return true;
      }
      return false;
    }
    // CMD.gearDir=<1|-1|FWD|REV|LEFT|RIGHT|IN|OUT> or CMD.feedDir=...
    if ((cmdLen >= 8 && strncmp(cmd, "gearDir=", 8) == 0) ||
        (cmdLen >= 8 && strncmp(cmd, "feedDir=", 8) == 0)) {
      IELSMode* active = ModeRegistry::getActiveMode();
      if (active && !active->canExecuteCommand(CommandId::CMD_SET_DIR, active->getState())) {
        if (reply) *reply = "ERR:CMD.gearDir=locked during active motion\n";
        return false;
      }
      const char* val = cmd + 8;
      UnifiedGearboxMode* gb = ModeRegistry::getGearboxMode();
      if (gb) {
        gb->setDirection(val);
        if (reply) *reply = String("OK:CMD.gearDir=") + (gb->getDirection() > 0 ? "1" : "-1") + "\n";
        return true;
      }
      return false;
    }
    // CMD.feedNudge=<+1|-1>
    if (cmdLen >= 10 && strncmp(cmd, "feedNudge=", 10) == 0) {
      IELSMode* active = ModeRegistry::getActiveMode();
      if (active && !active->canExecuteCommand(CommandId::CMD_FEED_NUDGE, active->getState())) {
        if (reply) *reply = "ERR:CMD.feedNudge=locked during active motion\n";
        return false;
      }
      int nudgeDir = (cmd[10] == '-' || atoi(cmd + 10) < 0) ? -1 : 1;
      UnifiedGearboxMode* gb = ModeRegistry::getGearboxMode();
      if (gb) {
        gb->nudgeFeed(nudgeDir);
        if (reply) *reply = String("OK:CMD.feedNudge=") + nudgeDir + "\n";
        return true;
      }
      return false;
    }
    // CMD.mode=<modeId|modeSlug>
    if (cmdLen >= 5 && strncmp(cmd, "mode=", 5) == 0) {
      const char* mStr = cmd + 5;
      IELSMode* active = ModeRegistry::getActiveMode();
      if (active && !active->canExecuteCommand(CommandId::CMD_SWITCH_MODE, active->getState())) {
        if (reply) *reply = "ERR:CMD.mode=locked during active cutting motion\n";
        return false;
      }

      // 1. Check for slug match in ModeRegistry
      IELSMode* targetBySlug = ModeRegistry::getModeBySlug(mStr);
      if (targetBySlug) {
        ModeRegistry::activateMode(targetBySlug->getId());
        if (targetBySlug->getId() == ModeIds::GEARBOX) {
          UnifiedGearboxMode* gb = ModeRegistry::getGearboxMode();
          if (gb) {
            if (!gb->isSync()) setOperatingMode(static_cast<int>(LatheMode::ASYNC));
            else if (strcasecmp(gb->getActiveAxis(), "X") == 0) setOperatingMode(static_cast<int>(LatheMode::XGEAR));
            else if (strcasecmp(gb->getActiveAxis(), "ZX") == 0) setOperatingMode(static_cast<int>(LatheMode::CONE));
            else setOperatingMode(static_cast<int>(LatheMode::NORMAL));
          }
        }
        if (reply) *reply = String("OK:CMD.mode=") + targetBySlug->getSlug() + "\n";
        return true;
      }

      // 2. Check for numeric mode ID
      int m = atoi(mStr);
      if (m == ModeIds::GEARBOX) {
        ModeRegistry::activateMode(ModeIds::GEARBOX);
        UnifiedGearboxMode* gb = ModeRegistry::getGearboxMode();
        if (gb) {
          if (!gb->isSync()) setOperatingMode(static_cast<int>(LatheMode::ASYNC));
          else if (strcasecmp(gb->getActiveAxis(), "X") == 0) setOperatingMode(static_cast<int>(LatheMode::XGEAR));
          else if (strcasecmp(gb->getActiveAxis(), "ZX") == 0) setOperatingMode(static_cast<int>(LatheMode::CONE));
          else setOperatingMode(static_cast<int>(LatheMode::NORMAL));
        }
        if (reply) *reply = "OK:CMD.mode=gearbox\n";
        return true;
      }

      // Standard legacy mode IDs
      setOperatingMode(m);
      if (reply) *reply = String("OK:CMD.mode=") + m + "\n";
      return true;
    }
#if ENABLE_SPINDLE_SIM
    // CMD.spindleSim=<0|1>,<signedRpm>   (re-send every <=1s as a heartbeat while active)
    if (cmdLen >= 11 && strncmp(cmd, "spindleSim=", 11) == 0) {
      int active = atoi(cmd + 11);
      const char* p = strchr(cmd + 11, ',');
      float rpm = p ? atof(p + 1) : 0.0f;
      spindleSimSet(active != 0, rpm);
      if (reply) *reply = String("SIM.spindle=") + (spindleSimIsActive() ? "1" : "0") + "," + String(spindleSimGetRpm(), 1) + "\n";
      return true;
    }
    // CMD.spindleSimTurn=<signedDegrees>  (hand-rotate the chuck; ignored unless override active)
    if (cmdLen >= 15 && strncmp(cmd, "spindleSimTurn=", 15) == 0) {
      spindleSimTurnDeg(atof(cmd + 15));
      return true;
    }
#endif
    // CMD.on=<0|1> (Deprecated: use CMD.startFeed / CMD.stop per ADR 0002)
    if (cmdLen >= 3 && strncmp(cmd, "on=", 3) == 0) {
      int on = atoi(cmd + 3);
      if (on == 0) {
        IELSMode* active = ModeRegistry::getActiveMode();
        if (active && active->isRunning()) {
          active->stop(StopType::NORMAL_STOP);
        }
        stopVelocityFeed();
        SettingsService::requestIsOn(false);
      } else {
        SettingsService::requestIsOn(true);
      }
      return true;
    }
    // CMD.measure=<0|1|2>
    if (cmdLen >= 8 && strncmp(cmd, "measure=", 8) == 0) {
      int meas = atoi(cmd + 8);
      SettingsService::setMeasure(meas);
      return true;
    }
  }

  // 3. Backward Compatibility: @pitch: and @dupr:
  if (length >= 7 && (strncmp(payload, "@pitch:", 7) == 0 || strncmp(payload, "@pitch=", 7) == 0)) {
    float val = atof(payload + 7);
    if (abs(val) > 0.00001f) {
      setFeedrateMm(val, false);
      return true;
    }
  }
  if (length >= 6 && (strncmp(payload, "@dupr:", 6) == 0 || strncmp(payload, "@dupr=", 6) == 0)) {
    long du = atol(payload + 6);
    if (du != 0) {
      setFeedrateDupr(du, false);
      return true;
    }
  }

  return false;
}
