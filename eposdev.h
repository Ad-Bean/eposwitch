// EPOS 设备状态（requestType=1 的 DeviceList 响应解析）
//
// 官方服务在 requestType=1 / 9 时会返回完整设备状态，字段共 42 个，
// 这是官方 UI 显示所有信息的唯一数据源。
//
// 字段实测样本（EPOS H3PRO）：
//   AutoSleepStatus:"1"        自动休眠状态
//   DeviceType:"BT"            BT / USB / Wired
//   HSConnection:"Dongle"      连接方式（Dongle=2.4G dongle, BT=蓝牙）
//   SideToneGain:"0"           侧音增益
//   Smartbutton:"1"            智能按钮
//   batterypercentage: 80      电量
//   deviceDisplayName:"EPOS H3PRO"
//   deviceType: 3
//   directSoundEnabled: 0      直达声
//   dongleFirmwareVersion:"3.12.1" / firmwareVersion:"1.3.5"
//   frcAvailability:""         麦克风 FRC 是否可用（空=不可用）
//   hpfAvailability:""         麦克风高通是否可用
//   latencyLevel:"-1"          延迟模式
//   light3DAvailability:""     3D 灯效是否可用
//   micDirection:""            麦克风指向
//   micEqEnabled/micEqLevels[] 麦克风 EQ
//   micFrcEnabled/micFrcPreset 麦克风 FRC
//   micHpfEnabled              麦克风高通
//   micMetersEnabled           麦克风电平表
//   micNrEnabled/micNrPreset   麦克风降噪
//   noiseGateEnabled/noiseGateLevel
//   noiseReductionLevel:"0"    ★ 降噪等级（耳机端）
//   nrAvailability:""          ★ 降噪是否可用
//   reverbEnabled/reverbLevel
//   speakerEqEnabled/speakerEqLevels[]
//   vpLanguage/vplanguagelist  语音提示语言
#pragma once
#include <string>
#include <vector>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <windows.h>

namespace eposdev {

struct DeviceInfo {
    // --- 身份 ---
    std::wstring deviceName;        // EPOS H3PRO
    std::wstring deviceDisplayName; // EPOS H3PRO
    std::wstring serialNumber;      // 耳机序列号
    std::wstring dongleSerialNumber;
    int vid = 0, pid = 0;
    int dongleVid = 0, donglePid = 0;
    int deviceType = 0;            // 3 = 耳机(headset), 5 = 接收器(dongle)
    std::wstring deviceTypeStr;     // BT / USB
    std::wstring hsConnection;      // Dongle / BT
    std::wstring manufacturer;

    // --- 电源 ---
    int battery = -1;
    std::wstring autoSleepStatus;

    // --- 固件 ---
    std::wstring firmwareVersion;
    std::wstring dongleFirmwareVersion;
    std::wstring newFirmwareVersion;
    std::wstring newDongleFirmwareVersion;

    // --- 音频效果 ---
    bool  speakerEqEnabled = false;
    double speakerEq[9] = {0};
    bool  micEqEnabled = false;
    double micEq[9] = {0};
    bool  reverbEnabled = false;
    double reverbLevel = 0;
    bool  noiseGateEnabled = false;
    double noiseGateLevel = 0;
    bool  directSoundEnabled = false;

    // --- 麦克风处理 ---
    int  micNrEnabled = 0, micNrPreset = 0;
    int  micFrcEnabled = 0, micFrcPreset = 0;
    int  micHpfEnabled = 0;
    int  micMetersEnabled = 0;
    std::wstring micDirection;

    // --- ★ 耳机端功能 ---
    int  noiseReductionLevel = -1;   // 降噪等级（-1 = 未知）
    bool nrStateKnown = false;       // 降噪状态是否可信（H3PRO 上恒为 false）
    bool nrAvailable = false;        // 降噪是否可用
    int  sideToneGain = -1;          // 侧音增益
    std::wstring smartButton;
    int  latencyLevel = -1;

    // --- 可用性标记（空字符串 = 该功能本机不支持）---
    std::wstring nrAvailability;
    std::wstring frcAvailability;
    std::wstring hpfAvailability;
    std::wstring light3DAvailability;

    // --- 语音提示 ---
    std::wstring vpLanguage;
    std::vector<std::wstring> vpLanguageList;

    bool valid = false;

    // deviceType: 3 = 耳机（带电量/固件/降噪），5 = 接收器
    bool IsHeadset() const { return deviceType == 3; }
    bool IsDongle()  const { return deviceType == 5; }
    // 降噪档位：>=0 表示有效档位，-1 表示未知/不适用
    bool NoiseReductionKnown() const { return noiseReductionLevel >= 0; }
};

// ---------------- 轻量 JSON 取值工具 ----------------
// 在一段文本里找 "key":  后面的值（支持字符串/数字/数组/对象）
inline bool JsonFind(const std::string& s, const char* key, size_t& posOut) {
    std::string pat = std::string("\"") + key + "\":";
    size_t p = s.find(pat);
    if (p == std::string::npos) return false;
    posOut = p + pat.size();
    return true;
}

inline std::wstring JsonStr(const std::string& s, const char* key, const wchar_t* def = L"") {
    size_t p;
    if (!JsonFind(s, key, p)) return def;
    if (p >= s.size() || s[p] != '"') return def;
    size_t e = s.find('"', p + 1);
    if (e == std::string::npos) return def;
    // UTF-8 -> UTF-16
    std::string raw = s.substr(p + 1, e - p - 1);
    int wlen = ::MultiByteToWideChar(CP_UTF8, 0, raw.c_str(), (int)raw.size(), nullptr, 0);
    if (wlen <= 0) return def;
    std::wstring out;
    out.resize(wlen);
    ::MultiByteToWideChar(CP_UTF8, 0, raw.c_str(), (int)raw.size(), &out[0], wlen);
    return out;
}

inline double JsonNum(const std::string& s, const char* key, double def = 0) {
    size_t p;
    if (!JsonFind(s, key, p)) return def;
    // ★ 服务把不少数值字段写成**字符串**（"latencyLevel":"-1"）。
    //   直接 strtod 会读到引号、返回 0 —— 之前延迟模式因此被误显示为"标准"。
    while (p < s.size() && s[p] == ' ') p++;
    if (p < s.size() && s[p] == '"') {
        p++;
        if (p < s.size() && s[p] == '"') return def;   // 空串 -> 用默认值
    }
    return strtod(s.c_str() + p, nullptr);
}

inline bool JsonBoolish(const std::string& s, const char* key, bool def = false) {
    size_t p;
    if (!JsonFind(s, key, p)) return def;
    if (p >= s.size()) return def;
    if (s[p] == 't' || s[p] == 'T') return true;
    if (s[p] == 'f' || s[p] == 'F') return false;
    return (int)strtol(s.c_str() + p, nullptr, 10) != 0;
}

// 解析数字数组 [a,b,c,...]
inline int JsonNumArray(const std::string& s, const char* key, double* out, int maxN) {
    size_t p;
    if (!JsonFind(s, key, p)) return 0;
    size_t lb = s.find('[', p);
    if (lb == std::string::npos) return 0;
    size_t rb = s.find(']', lb);
    if (rb == std::string::npos) return 0;
    const char* q = s.c_str() + lb + 1;
    int n = 0;
    while (n < maxN && q < s.c_str() + rb) {
        char* end = nullptr;
        double d = strtod(q, &end);
        if (end == q) break;
        out[n++] = d;
        q = end;
        while (*q == ',' || *q == ' ') q++;
    }
    return n;
}

// 解析字符串数组
inline std::vector<std::wstring> JsonStrArray(const std::string& s, const char* key) {
    std::vector<std::wstring> out;
    size_t p;
    if (!JsonFind(s, key, p)) return out;
    size_t lb = s.find('[', p);
    if (lb == std::string::npos) return out;
    size_t rb = s.find(']', lb);
    if (rb == std::string::npos) return out;
    size_t q = lb + 1;
    while (q < rb) {
        size_t q1 = s.find('"', q);
        if (q1 == std::string::npos || q1 >= rb) break;
        size_t q2 = s.find('"', q1 + 1);
        if (q2 == std::string::npos) break;
        out.push_back(JsonStr(s.substr(q1, q2 - q1 + 1), "", L""));
        q = q2 + 1;
    }
    return out;
}

// 从 DeviceList JSON 中切出每个设备对象的原文
inline std::vector<std::string> SplitDeviceObjects(const std::string& json) {
    std::vector<std::string> out;
    size_t lb = json.find("\"DeviceList\":[");
    if (lb == std::string::npos) return out;
    size_t start = json.find('[', lb);
    if (start == std::string::npos) return out;
    int depth = 0;
    size_t objStart = 0;
    bool inStr = false;
    for (size_t i = start; i < json.size(); i++) {
        char c = json[i];
        if (inStr) {
            if (c == '\\') { i++; continue; }
            if (c == '"') inStr = false;
            continue;
        }
        if (c == '"') { inStr = true; continue; }
        if (c == '{') {
            if (depth == 0) objStart = i;
            depth++;
        } else if (c == '}') {
            depth--;
            if (depth == 0) {
                out.push_back(json.substr(objStart, i - objStart + 1));
                if (out.size() >= 16) break;
            }
        } else if (c == ']' && depth == 0) {
            break;
        }
    }
    return out;
}

// 解析单个设备对象
inline DeviceInfo ParseDevice(const std::string& obj) {
    DeviceInfo d;
    d.deviceName        = JsonStr(obj, "deviceName");
    d.deviceDisplayName = JsonStr(obj, "deviceDisplayName");
    d.serialNumber     = JsonStr(obj, "serialNumber");
    d.dongleSerialNumber = JsonStr(obj, "dongleSerialNumber");
    d.vid   = (int)JsonNum(obj, "vid", 0);
    d.pid   = (int)JsonNum(obj, "pid", 0);
    d.dongleVid = (int)JsonNum(obj, "dongleVid", 0);
    d.donglePid = (int)JsonNum(obj, "donglePid", 0);
    d.deviceType = (int)JsonNum(obj, "deviceType", 0);
    d.deviceTypeStr = JsonStr(obj, "DeviceType");
    d.hsConnection  = JsonStr(obj, "HSConnection");
    d.manufacturer  = JsonStr(obj, "manufacturer");

    d.battery = (int)JsonNum(obj, "batterypercentage", -1);
    d.autoSleepStatus = JsonStr(obj, "AutoSleepStatus");

    d.firmwareVersion         = JsonStr(obj, "firmwareVersion");
    d.dongleFirmwareVersion   = JsonStr(obj, "dongleFirmwareVersion");
    d.newFirmwareVersion      = JsonStr(obj, "newfirmwareVersion");
    d.newDongleFirmwareVersion= JsonStr(obj, "newdongleFirmwareVersion");

    d.speakerEqEnabled = JsonBoolish(obj, "speakerEqEnabled");
    JsonNumArray(obj, "speakerEqLevels", d.speakerEq, 9);
    d.micEqEnabled = JsonBoolish(obj, "micEqEnabled");
    JsonNumArray(obj, "micEqLevels", d.micEq, 9);
    d.reverbEnabled = JsonBoolish(obj, "reverbEnabled");
    d.reverbLevel   = JsonNum(obj, "reverbLevel", 0);
    d.noiseGateEnabled = JsonBoolish(obj, "noiseGateEnabled");
    d.noiseGateLevel   = JsonNum(obj, "noiseGateLevel", 0);
    d.directSoundEnabled = JsonBoolish(obj, "directSoundEnabled");

    d.micNrEnabled = (int)JsonNum(obj, "micNrEnabled", 0);
    d.micNrPreset  = (int)JsonNum(obj, "micNrPreset", 0);
    d.micFrcEnabled = (int)JsonNum(obj, "micFrcEnabled", 0);
    d.micFrcPreset  = (int)JsonNum(obj, "micFrcPreset", 0);
    d.micHpfEnabled = (int)JsonNum(obj, "micHpfEnabled", 0);
    d.micMetersEnabled = (int)JsonNum(obj, "micMetersEnabled", 0);
    d.micDirection  = JsonStr(obj, "micDirection");

    // 降噪：值可能是字符串 "0" 也可能是数字；空字符串 = 不适用
    {
        size_t p;
        if (JsonFind(obj, "noiseReductionLevel", p)) {
            if (p < obj.size() && obj[p] == '"') {
                // 字符串形式
                size_t e = obj.find('"', p + 1);
                std::string raw = (e == std::string::npos) ? "" : obj.substr(p + 1, e - p - 1);
                if (raw.empty()) d.noiseReductionLevel = -1;
                else d.noiseReductionLevel = (int)strtol(raw.c_str(), nullptr, 10);
            } else {
                d.noiseReductionLevel = (int)strtol(obj.c_str() + p, nullptr, 10);
            }
        }
    }
    d.nrAvailability = JsonStr(obj, "nrAvailability");
    d.nrAvailable = !d.nrAvailability.empty();
    // ★ H3PRO 实测：noiseReductionLevel 恒为 "0" 且 nrAvailability 为空串。
    //   空 nrAvailability = 服务不提供该设备的降噪档位信息，
    //   此时 noiseReductionLevel 的值不可信（不代表耳机实际的 ANC 开关）。
    d.nrStateKnown = d.nrAvailable || d.noiseReductionLevel > 0;
    {
        size_t p;
        if (JsonFind(obj, "SideToneGain", p)) {
            if (p < obj.size() && obj[p] == '"') {
                size_t e = obj.find('"', p + 1);
                std::string raw = (e == std::string::npos) ? "" : obj.substr(p + 1, e - p - 1);
                if (raw.empty()) d.sideToneGain = -1;
                else d.sideToneGain = (int)strtol(raw.c_str(), nullptr, 10);
            } else {
                d.sideToneGain = (int)strtol(obj.c_str() + p, nullptr, 10);
            }
        }
    }
    d.smartButton = JsonStr(obj, "Smartbutton");
    d.latencyLevel = (int)JsonNum(obj, "latencyLevel", -1);

    d.frcAvailability      = JsonStr(obj, "frcAvailability");
    d.hpfAvailability      = JsonStr(obj, "hpfAvailability");
    d.light3DAvailability  = JsonStr(obj, "light3DAvailability");

    d.vpLanguage = JsonStr(obj, "vpLanguage");
    d.vpLanguageList = JsonStrArray(obj, "vplanguagelist");

    d.valid = true;
    return d;
}

// 解析完整响应（DeviceList + 其它）
inline std::vector<DeviceInfo> ParseDeviceList(const std::string& json,
                                               std::string* versionMismatch = nullptr) {
    std::vector<DeviceInfo> out;
    auto objs = SplitDeviceObjects(json);
    for (auto& o : objs) out.push_back(ParseDevice(o));
    if (versionMismatch) {
        size_t p;
        *versionMismatch = "0";
        if (JsonFind(json, "VersionMismatch", p))
            *versionMismatch = json.substr(p, json.find(',', p) - p);
    }
    return out;
}

}  // namespace eposdev
