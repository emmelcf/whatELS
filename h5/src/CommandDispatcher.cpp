#include "CommandDispatcher.h"
#include "Motion.h"
#include "Axis.h"
#include "Config.h"
#include "Input.h"
#include "WebServerModule.h"

static Axis* parseAxis(char c) {
  if (c == 'Z' || c == 'z') return &z;
  if (c == 'X' || c == 'x') return &x;
  if (c == 'Y' || c == 'y') return &y;
  return nullptr;
}

bool CommandDispatcher::dispatch(const char* payload, size_t length, String* reply) {
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

  // 1. Sysinfo request
  if (length >= 8 && strncmp(payload, "?sysinfo", 8) == 0) {
    String rep = "SYSINFO.version=" CONTROLLER_VERSION_TEXT "\n";
    rep += "SYSINFO.chip=" + String(ESP.getChipModel()) + " rev " + String(ESP.getChipRevision()) + "\n";
    rep += "SYSINFO.flash=" + String(ESP.getFlashChipSize() / 1024 / 1024) + " MB\n";
    rep += "SYSINFO.heap=" + String(ESP.getFreeHeap() / 1024) + " KB\n";
    if (reply) *reply = rep;
    queueWebSocketText(rep);
    return true;
  }

  // 2. Canonical CMD.* Namespace
  if (length >= 4 && strncmp(payload, "CMD.", 4) == 0) {
    const char* cmd = payload + 4;
    size_t cmdLen = length - 4;

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
      cycleFeedStep();
      return true;
    }
    // CMD.jogStepDu=<du>
    if (cmdLen >= 10 && strncmp(cmd, "jogStepDu=", 10) == 0) {
      long val = atol(cmd + 10);
      if (val > 0) moveStep = val;
      return true;
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
    // CMD.mode=<modeId>
    if (cmdLen >= 5 && strncmp(cmd, "mode=", 5) == 0) {
      int m = atoi(cmd + 5);
      setOperatingMode(m);
      return true;
    }
    // CMD.on=<0|1>
    if (cmdLen >= 3 && strncmp(cmd, "on=", 3) == 0) {
      int on = atoi(cmd + 3);
      setIsOnFromTask(on != 0);
      return true;
    }
    // CMD.measure=<0|1|2>
    if (cmdLen >= 8 && strncmp(cmd, "measure=", 8) == 0) {
      int meas = atoi(cmd + 8);
      setMeasure(meas);
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
