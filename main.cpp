// eposwitch - 音频输出设备智能切换器（Phase 1 骨架）
// 编译: g++ -std=c++17 -O2 -static -o eposwitch.exe main.cpp -lole32 -loleaut32 -luuid -lshell32 -luser32 -lgdi32 -lhid -lsetupapi -lcomctl32
#define _WIN32_WINNT 0x0601
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <mmreg.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <audioclient.h>
#include <audiopolicy.h>
#include <functiondiscoverykeys_devpkey.h>
#include <propsys.h>
#include <hidsdi.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <string>
#include <vector>
#include <map>
#include <cstdio>
#include <cwchar>
#include <cstdarg>
#include "epospipe.h"
#include "eposfx.h"
#include "epospanel.h"
#include "eui.h"

// ---------------- IPolicyConfig (undocumented) ----------------
static const CLSID CLSID_CPolicyConfigClient =
  {0x870af99c,0x171d,0x4f9e,{0xaf,0x0d,0xe6,0x3d,0xf4,0x0c,0x2b,0xc9}};
static const IID IID_IPolicyConfig =
  {0xf8679f50,0x850a,0x41cf,{0x9c,0x72,0x43,0x0f,0x29,0x02,0x90,0xc8}};

interface IPolicyConfig : public IUnknown {
  virtual HRESULT STDMETHODCALLTYPE GetMixFormat(PCWSTR, WAVEFORMATEX**) = 0;
  virtual HRESULT STDMETHODCALLTYPE GetDeviceFormat(PCWSTR, INT, WAVEFORMATEX**) = 0;
  virtual HRESULT STDMETHODCALLTYPE ResetDeviceFormat(PCWSTR) = 0;
  virtual HRESULT STDMETHODCALLTYPE SetDeviceFormat(PCWSTR, WAVEFORMATEX*, WAVEFORMATEX*) = 0;
  virtual HRESULT STDMETHODCALLTYPE GetProcessingPeriod(PCWSTR, INT, PINT64, PINT64) = 0;
  virtual HRESULT STDMETHODCALLTYPE SetProcessingPeriod(PCWSTR, PINT64) = 0;
  virtual HRESULT STDMETHODCALLTYPE GetShareMode(PCWSTR, void*) = 0;
  virtual HRESULT STDMETHODCALLTYPE SetShareMode(PCWSTR, void*) = 0;
  virtual HRESULT STDMETHODCALLTYPE GetPropertyValue(PCWSTR, INT, const PROPERTYKEY*, PROPVARIANT*) = 0;
  virtual HRESULT STDMETHODCALLTYPE SetPropertyValue(PCWSTR, INT, const PROPERTYKEY*, PROPVARIANT*) = 0;
  virtual HRESULT STDMETHODCALLTYPE SetDefaultEndpoint(PCWSTR, int) = 0;
  virtual HRESULT STDMETHODCALLTYPE SetEndpointVisibility(PCWSTR, INT) = 0;
};

// ---------------- 端点模型 ----------------
enum DevKind { KIND_BT, KIND_EPOS, KIND_REALTEK, KIND_OTHER, KIND_VIRTUAL, KIND_EXCLUDED };

struct Endpoint {
  std::wstring id;
  std::wstring name;
  DWORD state = 0;
  DevKind kind = KIND_OTHER;
  std::wstring fmt;      // 例如 "48000 Hz · 2ch · 32bit"（读不到则为空）
  std::wstring btMac;    // 蓝牙端点的 MAC（12 位十六进制，非蓝牙为空）
  int batt = -1;         // 蓝牙耳机电量 0..100（-1 = 未知）
};

#include "eui2.h"
#include "eui2draw.h"

static const wchar_t* KindName(DevKind k) {
  switch (k) {
    case KIND_BT: return L"蓝牙";
    case KIND_EPOS: return L"EPOS";
    case KIND_REALTEK: return L"主板声卡";
    case KIND_EXCLUDED: return L"排除";
    default: return L"其他";
  }
}

// ---------------- EQ 预设 ----------------
// Gaming Suite 的预设是 XML：<EQBandN name ="127"/>，取值 -127..127
// 换算成管道参数：dB = slider * 6 / 127
struct EqPreset {
    std::wstring name;
    double eq[9] = {0,0,0,0,0,0,0,0,0};
    bool   valid = false;
};

static bool ReadWholeFile(const std::wstring& path, std::string& out) {
    HANDLE f = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                             nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER li; ::GetFileSizeEx(f, &li);
    out.resize((size_t)li.QuadPart);
    DWORD rd = 0;
    if (!out.empty()) ::ReadFile(f, &out[0], (DWORD)out.size(), &rd, nullptr);
    out.resize(rd);
    ::CloseHandle(f);
    return true;
}

static bool ParseEqXml(const std::wstring& path, EqPreset& p) {
    std::string txt;
    if (!ReadWholeFile(path, txt)) return false;
    if (txt.find("EQBand1") == std::string::npos) return false;
    for (int i = 1; i <= 9; i++) {
        // 必须带 '<' 前缀，否则 "EQBand1" 会误匹配到 "MicEQBand1"
        char tag[64];
        _snprintf(tag, sizeof(tag), "<EQBand%d name =\"", i);
        size_t q = txt.find(tag);
        if (q == std::string::npos) {
            _snprintf(tag, sizeof(tag), "<EQBand%d  name =\"", i);
            q = txt.find(tag);
        }
        if (q == std::string::npos) continue;
        q += strlen(tag);
        p.eq[i - 1] = ::atof(txt.c_str() + q) * 6.0 / 127.0;
    }
    p.valid = true;
    return true;
}

static void CollectEqPresets(std::vector<EqPreset>& out) {
    out.clear();
    const wchar_t* kDirs[] = {
        L"C://ProgramData//EPOS//Gaming Suite//Config",
        L"C://ProgramData//EPOS//Gaming Suite//Config//CUSTOMPRESET",
        L"C://ProgramData//EPOS//Gaming Suite//Config//XMLFolder" };
    for (const wchar_t* dir : kDirs) {
        std::wstring dp = std::wstring(dir) + L"\\";
        WIN32_FIND_DATAW fd;
        HANDLE h = ::FindFirstFileW((dp + L"*.xml").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            EqPreset p;
            std::wstring name = fd.cFileName;
            size_t dot = name.find_last_of(L'.');
            if (dot != std::wstring::npos) name = name.substr(0, dot);
            p.name = name;
            if (ParseEqXml(dp + fd.cFileName, p)) {
                bool dup = false;
                for (auto& e : out) if (e.name == p.name) { dup = true; break; }
                if (!dup) out.push_back(p);
            }
        } while (::FindNextFileW(h, &fd));
        ::FindClose(h);
    }
}

// 收集当前存在的蓝牙设备名（SetupAPI 枚举 GUID_DEVCLASS_BLUETOOTH）
// 蓝牙音频端点的名字里一定包含蓝牙设备名，例如
//   端点 "Headphones (SOUNDPEATS GoFree 2)"  ⊃  蓝牙设备 "SOUNDPEATS GoFree 2"
// 这比按关键词猜名字可靠得多（"Headphones (...)" 不含 "Bluetooth" 任何关键词）。
static void CollectBluetoothNames(std::vector<std::wstring>& out) {
  out.clear();
  static const GUID kBluetoothClass =
    {0xe0cbf06c,0xcd8b,0x4647,{0xbb,0x8a,0x26,0x3b,0x43,0xf0,0xf9,0x74}};
  HDEVINFO di = SetupDiGetClassDevsW(&kBluetoothClass, nullptr, nullptr, DIGCF_PRESENT);
  if (di == INVALID_HANDLE_VALUE) return;
  SP_DEVINFO_DATA dd; dd.cbSize = sizeof(dd);
  for (DWORD i = 0; SetupDiEnumDeviceInfo(di, i, &dd); i++) {
    wchar_t buf[512] = {0};
    DWORD n = 0;
    if (SetupDiGetDeviceRegistryPropertyW(di, &dd, SPDRP_FRIENDLYNAME, nullptr,
                                         reinterpret_cast<PBYTE>(buf), sizeof(buf) - sizeof(wchar_t), &n)) {
      std::wstring nm = buf;
      if (!nm.empty()) out.push_back(nm);
    }
  }
  SetupDiDestroyDeviceInfoList(di);
}

// 端点归类规则
// ★ KIND_VIRTUAL 不再"隐藏"：设备页会显示在「虚拟设备」分组里，一样可选可调音量。
//   自动切换 / 热键循环仍会跳过它们（用户不会想被自动切到 Broadcast）。
static DevKind Classify(const std::wstring& name, const std::vector<std::wstring>& btNames) {
  auto has = [&](const wchar_t* s){ return name.find(s) != std::wstring::npos; };
  if (has(L"NVIDIA Broadcast")) return KIND_VIRTUAL;      // 虚拟降噪链
  if (has(L"NVIDIA High Definition")) return KIND_VIRTUAL; // HDMI/DP 音频
  if (has(L"Dongle")) return KIND_EPOS;                 // 2.4G dongle
  for (const auto& b : btNames)                         // 蓝牙设备名匹配
    if (!b.empty() && name.find(b) != std::wstring::npos) return KIND_BT;
  if (has(L"EPOS") || has(L"H3PRO")) return KIND_EPOS;
  if (has(L"Realtek")) return KIND_REALTEK;
  if (has(L"Bluetooth") || has(L"Hands-Free") || has(L"Avrcp")) return KIND_BT;
  if (has(L"Virtual") || has(L"CABLE") || has(L"Steam Streaming") ||
      has(L"Voicemod") || has(L"VB-Audio"))
    return KIND_VIRTUAL;                                 // 常见虚拟声卡
  return KIND_OTHER;
}

// 蓝牙耳机电量：Windows 设置页同源数据。
// 位置：HFP 服务节点（BTHENUM\{0000111E-...}）的容器属性
//   {104EA319-6EE2-4701-BD47-8DDBF425BBE5} pid 2（DEVPROP_TYPE_BYTE，0..100）
// 该属性不在公开的 WinRT/属性系统里（System.Devices.BatteryLife 等均为空），
// 只能从 PnP 层读。全部只读，与官方栈无冲突。
static void Log(const wchar_t* fmt, ...);
static const DEVPROPKEY PKEY_BtBattery = { {0x104EA319,0x6EE2,0x4701,{0xBD,0x47,0x8D,0xDB,0xF4,0x25,0xBB,0xE5}}, 2 };

// 从音频端点的实例 ID 提取蓝牙 MAC（形如 ...&0&9881BB113136_C00000000）
static std::wstring BtMacFromInstanceId(const std::wstring& id) {
  size_t p = id.find(L"_C00000000");
  if (p == std::wstring::npos || p < 12) return L"";
  std::wstring mac = id.substr(p - 12, 12);
  for (wchar_t c : mac)
    if (!((c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f') || (c >= L'A' && c <= L'F')))
      return L"";
  return mac;
}

// 读指定 MAC 的蓝牙耳机电量（遍历 BTHENUM 节点，HFP 节点上有效）。-1 = 未知
// 由 MMDevice 接口 ID 取 PnP 实例 ID（CM_Get_Device_Interface_PropertyW）
static std::wstring InstanceIdOfMmDevice(const std::wstring& mmId) {
  DEVPROPKEY k = { {0x78c34fc8,0x104a,0x4aca,{0x9e,0xa4,0x52,0x4d,0x52,0x99,0x6e,0x57}}, 256 }; // DEVPKEY_Device_InstanceId
  DEVPROPTYPE t = 0; ULONG sz = 0;
  if (CM_Get_Device_Interface_PropertyW(mmId.c_str(), &k, &t, nullptr, &sz, 0) != CR_BUFFER_SMALL || sz == 0)
    return L"";
  std::vector<BYTE> b(sz);
  if (CM_Get_Device_Interface_PropertyW(mmId.c_str(), &k, &t, b.data(), &sz, 0) != CR_SUCCESS)
    return L"";
  if (t != DEVPROP_TYPE_STRING) return L"";
  return std::wstring((wchar_t*)b.data());
}

static int ReadBtBatteryByMac(const std::wstring& macLower) {
  static std::wstring (*low)(std::wstring) = nullptr;
  (void)low;
  ULONG len = 0;
  if (CM_Get_Device_ID_List_SizeW(&len, L"BTHENUM", CM_GETIDLIST_FILTER_ENUMERATOR) != CR_SUCCESS)
    return -1;
  std::vector<wchar_t> ids(len);
  if (CM_Get_Device_ID_ListW(L"BTHENUM", ids.data(), len, CM_GETIDLIST_FILTER_ENUMERATOR) != CR_SUCCESS)
    return -1;
  std::wstring cur;
  for (const wchar_t* p = ids.data(); ; p++) {
    if (*p == 0) {
      if (cur.empty()) break;
      // 大小写不敏感地匹配 MAC
      std::wstring lowCur;
      for (wchar_t c : cur) lowCur.push_back((c >= L'A' && c <= L'Z') ? (wchar_t)(c - L'A' + L'a') : c);
      if (lowCur.find(macLower) != std::wstring::npos) {
        DEVINST dn = 0;
        DEVPROPTYPE t = 0;
        BYTE b = 0; ULONG sz = sizeof(b);
        if (CM_Locate_DevNodeW(&dn, cur.data(), CM_LOCATE_DEVNODE_NORMAL) == CR_SUCCESS &&
            CM_Get_DevNode_PropertyW(dn, &PKEY_BtBattery, &t, &b, &sz, 0) == CR_SUCCESS &&
            t == DEVPROP_TYPE_BYTE && b <= 100) {
          return (int)b;       // HFP 节点携带电量
        }
      }
      cur.clear();
      continue;
    }
    cur.push_back(*p);
  }
  return -1;
}

// ---------------- Core Audio 封装 ----------------
// --mic=子串：指定麦克风测试用哪个采集设备（AudioCore 里要用，先声明）
static std::wstring g_micNameFilter;

class AudioCore {
public:
  IMMDeviceEnumerator* en = nullptr;
  IPolicyConfig* pc = nullptr;

  bool Init() {
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                  __uuidof(IMMDeviceEnumerator), (void**)&en);
    if (FAILED(hr)) return false;
    CoCreateInstance(CLSID_CPolicyConfigClient, nullptr, CLSCTX_ALL,
                     IID_IPolicyConfig, (void**)&pc);
    return true;
  }
  ~AudioCore() { Shutdown(); }

  void Shutdown() {
    if (pc) { pc->Release(); pc = nullptr; }
    if (en) { en->Release(); en = nullptr; }
  }

  std::vector<Endpoint> ActiveRender() { return Active(eRender); }
  std::vector<Endpoint> ActiveCapture() { return Active(eCapture); }

  std::vector<Endpoint> Active(EDataFlow flow) {
    std::vector<Endpoint> out;
    std::vector<std::wstring> btNames;
    CollectBluetoothNames(btNames);
    IMMDeviceCollection* col = nullptr;
    if (!en || FAILED(en->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &col))) return out;
    UINT n = 0; col->GetCount(&n);
    for (UINT i = 0; i < n; i++) {
      IMMDevice* dev = nullptr;
      if (FAILED(col->Item(i, &dev))) continue;
      Endpoint ep;
      LPWSTR id = nullptr;
      if (SUCCEEDED(dev->GetId(&id))) { ep.id = id; CoTaskMemFree(id); }
      dev->GetState(&ep.state);
      IPropertyStore* ps = nullptr;
      std::wstring epInstanceId;
      if (SUCCEEDED(dev->OpenPropertyStore(STGM_READ, &ps))) {
        // PKEY_AudioEndpoint 系列 pid2：含完整 PnP 实例路径（...&0&<MAC>_C00000000）
        PROPERTYKEY kPath = { {0xb3f8fa53,0x0004,0x438e,{0x90,0x03,0x51,0xa4,0x6e,0x13,0x9b,0xfc}}, 2 };
        PROPVARIANT pvI; PropVariantInit(&pvI);
        if (SUCCEEDED(ps->GetValue(kPath, &pvI)) && pvI.vt == VT_LPWSTR)
          epInstanceId = pvI.pwszVal;
        PropVariantClear(&pvI);
        PROPVARIANT pv; PropVariantInit(&pv);
        if (SUCCEEDED(ps->GetValue(PKEY_Device_FriendlyName, &pv)) && pv.vt == VT_LPWSTR)
          ep.name = pv.pwszVal;
        PropVariantClear(&pv);
        ps->Release();
      }
      ep.kind = Classify(ep.name, btNames);
      ep.fmt = MixFormatOf(dev);
      // 蓝牙端点：提取 MAC 并读电量（Windows 设置页同源数据）
      if (epInstanceId.find(L"BTHENUM") != std::wstring::npos) {
        ep.btMac = BtMacFromInstanceId(epInstanceId);
        if (!ep.btMac.empty()) {
          std::wstring lowMac = ep.btMac;
          for (wchar_t& c : lowMac) if (c >= L'A' && c <= L'Z') c = (wchar_t)(c - L'A' + L'a');
          ep.batt = ReadBtBatteryByMac(lowMac);
        }
      }
      out.push_back(ep);
      dev->Release();
    }
    col->Release();
    return out;
  }

  // 读设备的混音格式（采样率/声道/位深），显示在设备页
  static std::wstring MixFormatOf(IMMDevice* dev) {
    if (!dev) return L"";
    IAudioClient* ac = nullptr;
    if (FAILED(dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&ac)) || !ac)
      return L"";
    WAVEFORMATEX* wf = nullptr;
    std::wstring out;
    if (SUCCEEDED(ac->GetMixFormat(&wf)) && wf) {
      wchar_t buf[128];
      swprintf(buf, 128, L"%u Hz · %uch · %ubit",
               (unsigned)wf->nSamplesPerSec, (unsigned)wf->nChannels,
               (unsigned)wf->wBitsPerSample);
      out = buf;
      CoTaskMemFree(wf);
    }
    ac->Release();
    return out;
  }

  std::wstring DefaultId(ERole role, bool capture = false) {
    std::wstring r;
    if (!en) return r;
    IMMDevice* dev = nullptr;
    if (FAILED(en->GetDefaultAudioEndpoint(capture ? eCapture : eRender, role, &dev))) return r;
    LPWSTR id = nullptr;
    if (SUCCEEDED(dev->GetId(&id))) { r = id; CoTaskMemFree(id); }
    dev->Release();
    return r;
  }

  // 设置默认端点。输入/输出通用：SetDefaultEndpoint 只认 role，
  // 数据流向由端点自身决定，所以传采集设备 id 一样有效。
  bool SetDefault(const std::wstring& id) {
    if (!pc) return false;
    bool ok = true;
    for (int role = 0; role < 3; role++) {
      HRESULT hr = pc->SetDefaultEndpoint(id.c_str(), role);
      if (FAILED(hr)) ok = false;
    }
    return ok;
  }

  // 取默认采集/渲染 id
  std::wstring DefaultCaptureId2() { return DefaultId(eConsole, true); }

  // ---------------- 音量（Core Audio IAudioEndpointVolume）----------------
  // 相对于调用 pipe 的 rt=15/17，这条路径更可靠：直接作用于 Windows 音频端点，
  // 也是用户在音量合成器里看到的值。返回 -1 表示读不到。
  int GetVolumePct(const std::wstring& id, bool capture) {
    if (!en) return -1;
    IMMDevice* dev = nullptr;
    if (FAILED(en->GetDevice(id.c_str(), &dev)) || !dev) return -1;
    IAudioEndpointVolume* vol = nullptr;
    int out = -1;
    if (SUCCEEDED(dev->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr,
                                 (void**)&vol)) && vol) {
      float f = 0;
      if (SUCCEEDED(vol->GetMasterVolumeLevelScalar(&f))) {
        out = (int)(f * 100.0f + 0.5f);
        if (out < 0) out = 0;
        if (out > 100) out = 100;
      }
      vol->Release();
    }
    dev->Release();
    (void)capture;
    return out;
  }

  bool SetVolumePct(const std::wstring& id, float pct, bool capture) {
    if (!en) return false;
    if (pct < 0) pct = 0;
    if (pct > 1) pct = 1;
    IMMDevice* dev = nullptr;
    if (FAILED(en->GetDevice(id.c_str(), &dev)) || !dev) return false;
    IAudioEndpointVolume* vol = nullptr;
    bool ok = false;
    if (SUCCEEDED(dev->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr,
                                 (void**)&vol)) && vol) {
      ok = SUCCEEDED(vol->SetMasterVolumeLevelScalar(pct, nullptr));
      vol->Release();
    }
    dev->Release();
    (void)capture;
    return ok;
  }

    // ★ MinGW 的 endpointvolume.h 只前向声明了 IAudioMeterInformation，
  //   这里按 COM 布局自己声明最小接口 + IID（官方 GUID: C02216F6-8C67-4B5B-9D00-D008E73E0064）
  struct IMeterInfo : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetPeakValue(float* pfPeak) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetMeteringChannelCount(UINT* pnChannelCount) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetChannelsPeakValues(UINT32 count, float* vals) = 0;
    virtual HRESULT STDMETHODCALLTYPE QueryHardwareSupport(DWORD* mask) = 0;
  };

  // 枚举采集端点，优先返回名字里带 EPOS 的那一个（耳机麦克风）。
  // 系统默认采集常常是 NVIDIA Broadcast 之类的虚拟设备，测它没有意义。
  std::wstring PreferredCaptureId() {
    std::wstring fallback;
    if (en) {
      IMMDeviceCollection* col = nullptr;
      if (SUCCEEDED(en->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &col)) && col) {
        UINT n = 0; col->GetCount(&n);
        for (UINT i = 0; i < n; i++) {
          IMMDevice* d = nullptr;
          if (FAILED(col->Item(i, &d)) || !d) continue;
          std::wstring name;
          IPropertyStore* ps = nullptr;
          if (SUCCEEDED(d->OpenPropertyStore(STGM_READ, &ps)) && ps) {
            PROPVARIANT pv; PropVariantInit(&pv);
            if (SUCCEEDED(ps->GetValue(PKEY_Device_FriendlyName, &pv)) && pv.vt == VT_LPWSTR)
              name = pv.pwszVal;
            PropVariantClear(&pv); ps->Release();
          }
          LPWSTR pid = nullptr;
          std::wstring id;
          if (SUCCEEDED(d->GetId(&pid)) && pid) { id = pid; CoTaskMemFree(pid); }
          d->Release();
          if (!g_micNameFilter.empty() && name.find(g_micNameFilter) != std::wstring::npos) {
            col->Release(); return id;
          }
          if (g_micNameFilter.empty() && name.find(L"EPOS") != std::wstring::npos) {
            col->Release(); return id;
          }
          if (fallback.empty()) fallback = id;
        }
        col->Release();
      }
    }
    return fallback;
  }

  // 按端点 id 取友好名（显示在麦克风测试卡片上，避免用户不知道在测哪个设备）
  std::wstring EndpointName(const std::wstring& id) {
    if (!en || id.empty()) return L"";
    IMMDevice* d = nullptr;
    if (FAILED(en->GetDevice(id.c_str(), &d)) || !d) return L"";
    std::wstring name;
    IPropertyStore* ps = nullptr;
    if (SUCCEEDED(d->OpenPropertyStore(STGM_READ, &ps)) && ps) {
      PROPVARIANT pv; PropVariantInit(&pv);
      if (SUCCEEDED(ps->GetValue(PKEY_Device_FriendlyName, &pv)) && pv.vt == VT_LPWSTR)
        name = pv.pwszVal;
      PropVariantClear(&pv); ps->Release();
    }
    d->Release();
    return name;
  }

  // 采集端点的实时峰值（0..1）。用于麦克风页的输入测试。
  // 这是 Windows 混音器里那个"输入电平"，不依赖耳机固件。
  float GetPeak(const std::wstring& id) {
    if (!en || id.empty()) return -1.0f;
    IMMDevice* dev = nullptr;
    if (FAILED(en->GetDevice(id.c_str(), &dev)) || !dev) return -1.0f;
    // 放在函数内：类的 static const GUID 成员在 C++17 下需要 out-of-class 定义
    static const GUID kIIDMeter =
      {0xC02216F6,0x8C67,0x4B5B,{0x9D,0x00,0xD0,0x08,0xE7,0x3E,0x00,0x64}};
    IMeterInfo* mi = nullptr;
    float p = -1.0f;
    if (SUCCEEDED(dev->Activate(kIIDMeter, CLSCTX_ALL, nullptr, (void**)&mi)) && mi) {
      mi->GetPeakValue(&p);
      mi->Release();
    }
    dev->Release();
    return p;
  }

  bool IsMuted(const std::wstring& id) {
    if (!en) return false;
    IMMDevice* dev = nullptr;
    if (FAILED(en->GetDevice(id.c_str(), &dev)) || !dev) return false;
    IAudioEndpointVolume* vol = nullptr;
    BOOL m = FALSE;
    if (SUCCEEDED(dev->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr,
                                 (void**)&vol)) && vol) {
      vol->GetMute(&m);
      vol->Release();
    }
    dev->Release();
    return m != FALSE;
  }

  bool SetMuted(const std::wstring& id, bool mute) {
    if (!en) return false;
    IMMDevice* dev = nullptr;
    if (FAILED(en->GetDevice(id.c_str(), &dev)) || !dev) return false;
    IAudioEndpointVolume* vol = nullptr;
    bool ok = false;
    if (SUCCEEDED(dev->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr,
                                 (void**)&vol)) && vol) {
      ok = SUCCEEDED(vol->SetMute(mute ? TRUE : FALSE, nullptr));
      vol->Release();
    }
    dev->Release();
    return ok;
  }

  // ---------------- 每应用音量（IAudioSessionManager2）----------------
  // Windows 没有公开的"每应用静音"API（音量合成器自己也没用 ISimpleAudioVolume 之外的
  // 东西），所以静音 = 记住之前的音量然后把会话音量设成 0，恢复 = 写回 prev。
  struct AppVol {
    DWORD pid = 0;
    std::wstring exe;
    int vol = 100;
    bool muted = false;
  };

  static std::wstring ProcessBaseName(DWORD pid) {
    std::wstring s;
    if (!pid) return s;
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return s;
    wchar_t buf[1024] = {0};
    DWORD n = 1024;
    if (QueryFullProcessImageNameW(h, 0, buf, &n)) {
      s = buf;
      size_t k = s.find_last_of(L'\\');
      if (k != std::wstring::npos) s = s.substr(k + 1);
    }
    CloseHandle(h);
    return s;
  }

  // 枚举默认播放设备上的所有会话，同进程的多条会话合并成一行
  std::vector<AppVol> EnumAppVolumes() {
    std::vector<AppVol> out;
    if (!en) return out;
    IMMDevice* dev = nullptr;
    if (FAILED(en->GetDefaultAudioEndpoint(eRender, eConsole, &dev)) || !dev) return out;
    IAudioSessionManager2* mgr = nullptr;
    if (FAILED(dev->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr,
                             (void**)&mgr)) || !mgr) {
      dev->Release(); return out;
    }
    IAudioSessionEnumerator* se = nullptr;
    if (SUCCEEDED(mgr->GetSessionEnumerator(&se)) && se) {
      int n = 0;
      if (SUCCEEDED(se->GetCount(&n))) {
        for (int i = 0; i < n; i++) {
          IAudioSessionControl* ctl = nullptr;
          if (FAILED(se->GetSession(i, &ctl)) || !ctl) continue;
          IAudioSessionControl2* c2 = nullptr;
          if (SUCCEEDED(ctl->QueryInterface(__uuidof(IAudioSessionControl2), (void**)&c2)) && c2) {
            DWORD pid = 0;
            if (SUCCEEDED(c2->GetProcessId(&pid)) && pid) {
              bool dup = false;
              for (const auto& a : out) if (a.pid == pid) { dup = true; break; }
              if (!dup) {
                AppVol a;
                a.pid = pid;
                a.exe = ProcessBaseName(pid);
                ISimpleAudioVolume* sv = nullptr;
                if (SUCCEEDED(ctl->QueryInterface(__uuidof(ISimpleAudioVolume), (void**)&sv)) && sv) {
                  float f = 0;
                  if (SUCCEEDED(sv->GetMasterVolume(&f)))
                    a.vol = (int)(f * 100.0f + 0.5f);
                  BOOL m = FALSE;
                  if (SUCCEEDED(sv->GetMute(&m))) a.muted = m != FALSE;
                  sv->Release();
                }
                out.push_back(a);
              }
            }
            c2->Release();
          }
          ctl->Release();
        }
      }
      se->Release();
    }
    mgr->Release();
    dev->Release();
    return out;
  }

  // 把某进程的所有会话音量一起改（Chrome 这类会有多条会话）
  // ★ MinGW 的 ISimpleAudioVolume 方法名叫 SetMasterVolume（MSDK 叫 SetMasterVolumeLevelScalar）
  bool SetAppVolumeByPid(DWORD pid, float pct) {
    if (!en || !pid) return false;
    if (pct < 0) pct = 0;
    if (pct > 1) pct = 1;
    bool ok = false;
    IMMDevice* dev = nullptr;
    if (FAILED(en->GetDefaultAudioEndpoint(eRender, eConsole, &dev)) || !dev) return false;
    IAudioSessionManager2* mgr = nullptr;
    if (SUCCEEDED(dev->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr,
                                (void**)&mgr)) && mgr) {
      IAudioSessionEnumerator* se = nullptr;
      if (SUCCEEDED(mgr->GetSessionEnumerator(&se)) && se) {
        int n = 0;
        if (SUCCEEDED(se->GetCount(&n))) {
          for (int i = 0; i < n; i++) {
            IAudioSessionControl* ctl = nullptr;
            if (FAILED(se->GetSession(i, &ctl)) || !ctl) continue;
            IAudioSessionControl2* c2 = nullptr;
            if (SUCCEEDED(ctl->QueryInterface(__uuidof(IAudioSessionControl2), (void**)&c2)) && c2) {
              DWORD p2 = 0;
              if (c2->GetProcessId(&p2) == S_OK && p2 == pid) {
                ISimpleAudioVolume* sv = nullptr;
                if (SUCCEEDED(ctl->QueryInterface(__uuidof(ISimpleAudioVolume), (void**)&sv)) && sv) {
                  ok |= SUCCEEDED(sv->SetMasterVolume(pct, nullptr));
                  sv->Release();
                }
              }
              c2->Release();
            }
            ctl->Release();
          }
        }
        se->Release();
      }
      mgr->Release();
    }
    dev->Release();
    return ok;
  }

  // 每应用静音（ISimpleAudioVolume 自带 SetMute，比"音量设 0"干净）
  bool SetAppMuteByPid(DWORD pid, bool mute) {
    if (!en || !pid) return false;
    bool ok = false;
    IMMDevice* dev = nullptr;
    if (FAILED(en->GetDefaultAudioEndpoint(eRender, eConsole, &dev)) || !dev) return false;
    IAudioSessionManager2* mgr = nullptr;
    if (SUCCEEDED(dev->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr,
                                (void**)&mgr)) && mgr) {
      IAudioSessionEnumerator* se = nullptr;
      if (SUCCEEDED(mgr->GetSessionEnumerator(&se)) && se) {
        int n = 0;
        if (SUCCEEDED(se->GetCount(&n))) {
          for (int i = 0; i < n; i++) {
            IAudioSessionControl* ctl = nullptr;
            if (FAILED(se->GetSession(i, &ctl)) || !ctl) continue;
            IAudioSessionControl2* c2 = nullptr;
            if (SUCCEEDED(ctl->QueryInterface(__uuidof(IAudioSessionControl2), (void**)&c2)) && c2) {
              DWORD p2 = 0;
              if (c2->GetProcessId(&p2) == S_OK && p2 == pid) {
                ISimpleAudioVolume* sv = nullptr;
                if (SUCCEEDED(ctl->QueryInterface(__uuidof(ISimpleAudioVolume), (void**)&sv)) && sv) {
                  ok |= SUCCEEDED(sv->SetMute(mute ? TRUE : FALSE, nullptr));
                  sv->Release();
                }
              }
              c2->Release();
            }
            ctl->Release();
          }
        }
        se->Release();
      }
      mgr->Release();
    }
    dev->Release();
    return ok;
  }

  // 取麦克风端点 id：
  //   优先 EPOS/H3PRO 的录音端点（本工具是 EPOS 控制面板，不该去改 NVIDIA Broadcast 的音量）
  //   找不到再退回系统默认录音设备
  std::wstring DefaultCaptureId() {
    std::wstring r;
    if (!en) return r;
    // 先找 EPOS 录音端点
    IMMDeviceCollection* col = nullptr;
    if (SUCCEEDED(en->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &col)) && col) {
      UINT n = 0; col->GetCount(&n);
      for (UINT i = 0; i < n; i++) {
        IMMDevice* d = nullptr;
        if (FAILED(col->Item(i, &d)) || !d) continue;
        std::wstring nm;
        IPropertyStore* ps = nullptr;
        if (SUCCEEDED(d->OpenPropertyStore(STGM_READ, &ps)) && ps) {
          PROPVARIANT pv; PropVariantInit(&pv);
          if (SUCCEEDED(ps->GetValue(PKEY_Device_FriendlyName, &pv)) && pv.vt == VT_LPWSTR)
            nm = pv.pwszVal;
          PropVariantClear(&pv);
          ps->Release();
        }
        if (nm.find(L"EPOS") != std::wstring::npos || nm.find(L"H3PRO") != std::wstring::npos) {
          LPWSTR id = nullptr;
          if (SUCCEEDED(d->GetId(&id))) { r = id; CoTaskMemFree(id); }
          d->Release();
          break;
        }
        d->Release();
      }
      col->Release();
    }
    if (!r.empty()) return r;
    // 退回默认
    IMMDevice* dev = nullptr;
    if (FAILED(en->GetDefaultAudioEndpoint(eCapture, eConsole, &dev)) || !dev) return r;
    LPWSTR id = nullptr;
    if (SUCCEEDED(dev->GetId(&id))) { r = id; CoTaskMemFree(id); }
    dev->Release();
    return r;
  }
};

// ---------------- 设备变化监听 ----------------
class NotifyClient : public IMMNotificationClient {
public:
  LONG ref = 1;
  HANDLE evt = nullptr;
  NotifyClient(HANDLE e) : evt(e) {}

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
    if (!ppv) return E_POINTER;
    if (riid == __uuidof(IUnknown) || riid == __uuidof(IMMNotificationClient)) {
      *ppv = static_cast<IMMNotificationClient*>(this);
      AddRef();
      return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&ref); }
  ULONG STDMETHODCALLTYPE Release() override {
    LONG r = InterlockedDecrement(&ref);
    if (r == 0) delete this;
    return r;
  }
  HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow, ERole, LPCWSTR) override {
    SetEvent(evt); return S_OK;
  }
  HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override { SetEvent(evt); return S_OK; }
  HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override { SetEvent(evt); return S_OK; }
  HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR, DWORD) override { SetEvent(evt); return S_OK; }
  HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override { return S_OK; }
};

// ---------------- 电量 / 在线状态数据源 ----------------
// 过渡实现: 解析 EPOS Gaming Suite 的日志 (该行是 info 级别，不需要 TRACE)。
// 日志样例:
//   [2026-10-22 15:59:52.780] [GamingSuite] [info] [47684] getBatteryPercentage :
//       Leaving and Battery percentage is: 80
// 局限: 依赖 Gaming Suite 服务在运行。协议自研后替换本函数即可。
static const wchar_t* kGsLog =
    L"C:\\ProgramData\\EPOS\\Gaming Suite\\Logs\\GamingSuite.log";

// 返回电量(0..100)，-1 表示读不到。freshOut 表示该读数是否新鲜(<20s)。
// Gaming Suite 日志状态解析
// 关键逻辑: 耳机在线 -> 持续产生电量行(Battery percentage response)
//           耳机离线 -> 服务每 ~7 秒刷一次超时(Timeout waiting for command response)
// 比较两者时间戳即可得到秒级在线判定，无需知道 HID 报文格式。
struct GsState {
  int    battery     = -1;   // 最近一次电量值
  double batteryAge  = 1e9;  // 距今秒数
  bool   sawTimeout  = false; // 是否见到超时事件
  double timeoutAge  = 1e9;
};

// 日志里的电量读数最多可信多久（秒）。
// 服务轮询电量间隔实测最长 ~10 分钟；留 20 分钟余量。
// 超过这个窗口还没读到新值 => 这段时间根本没查到设备 => 不能算"在线"。
static const double kGsBatteryFreshSec = 20 * 60;

static bool ParseLineTs(const char* line, int* Y, int* M, int* D,
                        int* hh, int* mm, int* ss, int* ms) {
  return sscanf(line, "[%d-%d-%d %d:%d:%d.%d]", Y, M, D, hh, mm, ss, ms) == 7;
}

static double AgeOfLine(const char* line) {
  int Y, M, D, hh, mm, ss, ms;
  if (!ParseLineTs(line, &Y, &M, &D, &hh, &mm, &ss, &ms)) return 1e9;
  SYSTEMTIME st; GetLocalTime(&st);
  FILETIME ftLog, ftNow, ftLocal;
  SYSTEMTIME sl = {(WORD)Y,(WORD)M,(WORD)st.wDayOfWeek,(WORD)D,
                   (WORD)hh,(WORD)mm,(WORD)ss,(WORD)ms};
  SystemTimeToFileTime(&sl, &ftLog);
  GetSystemTimeAsFileTime(&ftNow);
  FileTimeToLocalFileTime(&ftNow, &ftLocal);
  ULARGE_INTEGER a, b;
  a.LowPart = ftLog.dwLowDateTime;   a.HighPart = ftLog.dwHighDateTime;
  b.LowPart = ftLocal.dwLowDateTime; b.HighPart = ftLocal.dwHighDateTime;
  double diff = (double)(b.QuadPart - a.QuadPart) / 1e7;
  return (diff >= 0) ? diff : 1e9;
}

static bool ReadGsState(GsState& st) {
  st = GsState();
  // 缓存：日志没变化就不重复解析（每 2 秒轮询一次，避免无谓 I/O）
  static ULONGLONG lastSize = 0, lastWrite = 0;
  static GsState cache;
  static bool cacheValid = false;

  HANDLE hf = CreateFileW(kGsLog, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (hf == INVALID_HANDLE_VALUE) return false;
  ULONGLONG fsize = GetFileSize(hf, nullptr);
  FILETIME ft; GetFileTime(hf, &ft, nullptr, nullptr);
  ULONGLONG fwrite = ((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
  CloseHandle(hf);

  if (cacheValid && fsize == lastSize && fwrite == lastWrite) { st = cache; return true; }
  lastSize = fsize; lastWrite = fwrite;

  FILE* f = _wfopen(kGsLog, L"rb");
  if (!f) return false;
  _fseeki64(f, 0, SEEK_END);
  __int64 sz = _ftelli64(f);
  if (sz <= 0) { fclose(f); return false; }
  const __int64 kTail = 256 * 1024;
  __int64 start = (sz > kTail) ? sz - kTail : 0;
  _fseeki64(f, start, SEEK_SET);
  std::string buf;
  buf.resize((size_t)(sz - start));
  size_t rd = fread(&buf[0], 1, buf.size(), f);
  buf.resize(rd);
  fclose(f);

  size_t i = 0;
  while (i < buf.size()) {
    size_t j = buf.find('\n', i);
    if (j == std::string::npos) j = buf.size();
    if (j > i) {
      const char* line = buf.c_str() + i;
      size_t len = j - i;
      if (strstr(line, "Battery percentage response:")) {
        const char* v = strstr(line, "\"value\":");
        if (v) {
          int val = atoi(v + 8);
          if (val >= 0 && val <= 100) {
            st.battery = val;
            st.batteryAge = AgeOfLine(line);
          }
        }
      }
      if (strstr(line, "Timeout waiting for command response") ||
          strstr(line, "Write failed for opcode")) {
        double a = AgeOfLine(line);
        if (a < st.timeoutAge) { st.timeoutAge = a; st.sawTimeout = true; }
      }
    }
    i = j + 1;
  }
  cache = st;
  cacheValid = true;
  return (st.battery >= 0) || st.sawTimeout;
}

enum ProbeResult { PROBE_NO_DONGLE, PROBE_DONGLE_ONLY, PROBE_ONLINE, PROBE_UNKNOWN };

static const wchar_t* ProbeName(ProbeResult p) {
  switch (p) {
    case PROBE_NO_DONGLE: return L"未插入";
    case PROBE_DONGLE_ONLY: return L"dongle 在位";
    case PROBE_ONLINE: return L"在线";
    default: return L"未知";
  }
}

static bool FindEposHid(std::wstring& pathOut) {
  GUID hidGuid; HidD_GetHidGuid(&hidGuid);
  HDEVINFO di = SetupDiGetClassDevsW(&hidGuid, nullptr, nullptr,
                                     DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
  if (di == INVALID_HANDLE_VALUE) return false;
  bool found = false;
  SP_DEVICE_INTERFACE_DATA did; did.cbSize = sizeof(did);
  for (DWORD i = 0; SetupDiEnumDeviceInterfaces(di, nullptr, &hidGuid, i, &did); i++) {
    DWORD need = 0;
    SetupDiGetDeviceInterfaceDetailW(di, &did, nullptr, 0, &need, nullptr);
    if (!need) continue;
    auto* detail = (PSP_DEVICE_INTERFACE_DETAIL_DATA_W)malloc(need);
    detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
    if (SetupDiGetDeviceInterfaceDetailW(di, &did, detail, need, nullptr, nullptr)) {
      std::wstring p = detail->DevicePath;
      if (p.find(L"vid_1395") != std::wstring::npos &&
          p.find(L"pid_0303") != std::wstring::npos &&
          p.find(L"col01") != std::wstring::npos) {
        pathOut = p; found = true; free(detail); break;
      }
    }
    free(detail);
  }
  SetupDiDestroyDeviceInfoList(di);
  return found;
}

// ---- EPOS 命名管道客户端（取代读日志轮询，可秒级查电量 + 写 APO 参数）----
static epospipe::Client g_pipe;
static std::thread    g_pipeThread;
static std::atomic<bool> g_pipeRun{false};
static bool           g_pipeReady = false;

// ---- 音效面板状态（旧版，保留兼容）----
static epospanel::PanelState g_panel;
static bool           g_showFx = false;
static epospanel::Layout g_fxLayout;
static int            g_fxBand = -1;

// ---- 新版界面状态 ----
static eui2::AppState g_app;
static bool g_newUi = true;         // 是否启用新版界面
static int  g_eqDrag = -1;          // 正在拖动的 EQ 频段
static int  g_volDrag = -1;         // 正在拖动的音量滑块（0=扬声器 1=麦克风）
static int  g_devVolDrag = -1;      // 设备页：正在拖动第几行的音量条
static bool g_devVolCap = false;    // 设备页：拖的是不是录音端点
static int  g_mixVolDrag = -1;      // 混音器：正在拖动第几行的音量条
static DWORD g_presetReloadAt = 0;  // 上次重新读取预设列表的时刻（0 = 需要立即重读）
static int  g_sldDrag = -1;         // 正在拖动的滑块 (0=混响 1=噪声门 2=侧音)
static DWORD g_lastDevPoll = 0;

static int g_battery = -1;
static double g_batteryAge = 1e9;

static void Log(const wchar_t* fmt, ...);

static ProbeResult ProbeEpos(int* batteryOut) {
  // ★ 管道可用时不做 HID 枚举（SetupAPI 每次几十~几百毫秒），
  //   设备在线状态以服务返回的 DeviceList 为准，更准也更快。
  if (!g_pipe.Ready()) {
    std::wstring path;
    if (!FindEposHid(path)) { if (batteryOut) *batteryOut = -1; return PROBE_NO_DONGLE; }
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) { if (batteryOut) *batteryOut = -1; return PROBE_DONGLE_ONLY; }
    CloseHandle(h);
  }

  // 数据源: Gaming Suite 日志。
  // 在线判据 = "电量事件比超时事件新"：
  //   耳机在线 -> 持续有 Battery percentage response，无超时
  //   耳机离线 -> 每 ~7 秒一条 Timeout waiting for command response
  // 数据源优先级:
  //   1) 管道 rt=1 的 DeviceList —— 最可靠，能区分"耳机本体在不在"
  //   2) 管道 rt=2 的电量
  //   3) Gaming Suite 日志解析（兜底）
  //
  // 判据说明:
  //   rt=1 的 DeviceList 里 deviceType=3 是耳机本体、deviceType=5 是接收器 dongle。
  //   只有 dongle 没有 headset => 2.4G 无线链路断了（耳机没和 dongle 配对或已掉线）。
  static DWORD s_pd = 0;
  DWORD p0 = GetTickCount();
  if (g_pipe.Ready()) {
    DWORD p1 = GetTickCount();
    bool hp = g_pipe.HeadsetPresent();
    DWORD p2 = GetTickCount();
    if (GetTickCount() - p0 > 200 || p2 - p1 > 200) {
      Log(L"ProbeEpos slow: ready=%d headset=%d ready_ms=%lu hp_ms=%lu",
          (int)g_pipe.Ready(), (int)hp, p1 - p0, p2 - p1);
    }
    s_pd = p0;
    if (hp) {
      int b = g_pipe.LastBattery();
      if (batteryOut) *batteryOut = b;
      g_batteryAge = 0;
      return PROBE_ONLINE;
    }
    // dongle 在但耳机不在 —— 明确判定为"耳机离线"
    if (g_pipe.DonglePresent()) {
      if (batteryOut) *batteryOut = -1;
      g_batteryAge = -1;
      return PROBE_DONGLE_ONLY;
    }
  }

  int pipeBat = g_pipe.LastBattery();
  if (pipeBat >= 0) {
    if (batteryOut) *batteryOut = pipeBat;
    g_batteryAge = 0;
    return PROBE_ONLINE;
  }
  // ★ 关键性能修复：管道已就绪但设备列表还没到（刚启动的几秒）→ 直接返回 UNKNOWN。
  //   绝不能在这里回落到"解析 Gaming Suite 日志"：那个日志有 3~4 MB，
  //   而且我们每次请求都会往里写新行，缓存永远失效 →
  //   实测每次解析要 1.7~4 秒，窗口每 2 秒卡一次。
  if (g_pipe.Ready()) {
    if (batteryOut) *batteryOut = -1;
    return PROBE_UNKNOWN;
  }

  // 兜底：只有管道不可用（官方 UI 正在运行）才解析日志，且节流到 10 秒一次
  static DWORD s_lastGsRead = 0;
  static GsState s_gs;
  DWORD now = GetTickCount();
  if (s_lastGsRead == 0 || now - s_lastGsRead > 10000) {
    ReadGsState(s_gs);
    s_lastGsRead = now;
  }
  g_batteryAge = s_gs.batteryAge;
  if (batteryOut) *batteryOut = s_gs.battery;
  // 只有"新鲜的"电量读数才能证明在线。
  // 服务轮询电量间隔最长约 10 分钟，留 20 分钟余量；超过说明这段时间根本没查到设备，
  // 此时不能因为"没看到更新的超时"就误判为在线（那只是日志静默而已）。
  if (s_gs.battery >= 0 && s_gs.batteryAge < kGsBatteryFreshSec && s_gs.batteryAge < s_gs.timeoutAge)
    return PROBE_ONLINE;
  if (s_gs.sawTimeout) return PROBE_DONGLE_ONLY;   // 有超时且更新 -> 耳机离线
  return PROBE_UNKNOWN;
}

// ---------------- 策略 ----------------
struct Strategy {
  // 蓝牙优先 -> EPOS(在线) -> Realtek -> 其他
  // 关键安全约束: EPOS 探测结果为 UNKNOWN（协议未实现）时不做决策，
  // 保持现状，避免在耳机其实在线的情况下误切走。
  const Endpoint* Pick(const std::vector<Endpoint>& eps, ProbeResult epos, bool* uncertain = nullptr) {
    if (uncertain) *uncertain = false;
    const Endpoint* bt = nullptr, *ep = nullptr, *rt = nullptr, *other = nullptr;
    for (auto& e : eps) {
      if (e.kind == KIND_EXCLUDED || e.kind == KIND_VIRTUAL) continue;
      if (e.kind == KIND_BT && !bt) bt = &e;
      else if (e.kind == KIND_EPOS && !ep) ep = &e;
      else if (e.kind == KIND_REALTEK && !rt) rt = &e;
      else if (e.kind == KIND_OTHER && !other) other = &e;
    }
    if (bt) return bt;
    if (ep) {
      if (epos == PROBE_ONLINE) return ep;
      if (epos == PROBE_UNKNOWN) { if (uncertain) *uncertain = true; return nullptr; }
      // PROBE_DONGLE_ONLY / PROBE_NO_DONGLE: 视为不可用，继续往下
    }
    if (rt) return rt;
    if (ep) return ep;
    return other;
  }
};

// ==================== 现代 UI 层（GDI+ 自绘） ====================
#include <windowsx.h>
#include <gdiplus.h>
using namespace Gdiplus;

// ---- 全局状态（原全局区，重排版后必须保留） ----
static HINSTANCE g_inst = nullptr;
static AudioCore g_audio;
static NotifyClient* g_notify = nullptr;
static HANDLE g_evt = nullptr;
static Strategy g_strategy;
static bool g_autoSwitch = true;
static bool g_battAlertOn = true;    // 低电量提醒（设置页可关）
static bool g_restoreOn = true;      // 耳机重连后切回输出
static bool g_appPresetOn = true;    // 按前台应用自动切预设
static bool g_dryRun = false;
static std::vector<Endpoint> g_eps;   // 播放端点
static std::vector<Endpoint> g_epc;   // 录音端点
static ProbeResult g_probe = PROBE_UNKNOWN;
static DWORD g_lastSwitchTick = 0;


static void Log(const wchar_t* fmt, ...) {
  wchar_t buf[1024];
  va_list ap; va_start(ap, fmt);
  vswprintf(buf, 1024, fmt, ap);
  va_end(ap);
  wchar_t path[MAX_PATH];
  GetModuleFileNameW(nullptr, path, MAX_PATH);
  wchar_t* slash = wcsrchr(path, L'\\');
  if (slash) wcscpy(slash + 1, L"eposwitch.log");
  FILE* f = _wfopen(path, L"a, ccs=UTF-8");
  if (f) { fwprintf(f, L"%ls\n", buf); fclose(f); }
}



// ---------------- 全局快捷键（可关闭 / 可改键）----------------
// 组合键以纯 ASCII 存进 settings.ini（Ctrl+Alt+P），既好读也不怕编码问题。
#define HK_CYCLE_OUT 9001
#define HK_MUTE      9002
#define HK_CYCLE_EQ  9003

struct HotkeySlot {
  const wchar_t* zh;
  const wchar_t* en;
  UINT defMods;
  UINT defVk;
  UINT mods = 0;
  UINT vk   = 0;
  bool ok   = false;      // 是否注册成功（失败通常是别的软件占用了）
};
static HotkeySlot g_hkSlots[3] = {
  { L"切换播放设备",     L"Cycle playback device", MOD_CONTROL | MOD_ALT, 'P' },
  { L"扬声器静音",       L"Mute speakers",         MOD_CONTROL | MOD_ALT, 'M' },
  { L"循环切换 EQ 预设", L"Cycle EQ presets",      MOD_CONTROL | MOD_ALT, 'Q' },
};
static bool g_hkEnabled = true;
static int  g_hkCapture = -1;          // 正在录制的项（-1 = 没在录）
static std::wstring g_hkMsg;           // 短暂提示（不支持/被占用…）
static DWORD g_hkMsgAt = 0;

struct KeyNamePair { const wchar_t* name; UINT vk; };
static const KeyNamePair kKeyNames[] = {
  { L"SPACE", VK_SPACE }, { L"TAB", VK_TAB }, { L"CAPS", VK_CAPITAL },
  { L"INS", VK_INSERT }, { L"DEL", VK_DELETE }, { L"HOME", VK_HOME }, { L"END", VK_END },
  { L"PGUP", VK_PRIOR }, { L"PGDN", VK_NEXT },
  { L"UP", VK_UP }, { L"DOWN", VK_DOWN }, { L"LEFT", VK_LEFT }, { L"RIGHT", VK_RIGHT },
  { L"PAUSE", VK_PAUSE }, { L"SCROLL", VK_SCROLL },
  { L"`", VK_OEM_3 }, { L"-", VK_OEM_MINUS }, { L"=", VK_OEM_PLUS },
  { L"[", VK_OEM_4 }, { L"]", VK_OEM_6 }, { L"\\", VK_OEM_5 },
  { L";", VK_OEM_1 }, { L"'", VK_OEM_7 }, { L",", VK_OEM_COMMA },
  { L".", VK_OEM_PERIOD }, { L"/", VK_OEM_2 },
};

// 虚拟键 -> 可读名字（ASCII 大写）
static bool HkKeyName(UINT vk, wchar_t* out, int n) {
  if (!out || n < 2) return false;
  out[0] = 0;
  if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) {
    out[0] = (wchar_t)vk; out[1] = 0; return true;
  }
  if (vk >= VK_F1 && vk <= VK_F24) {
    swprintf(out, (size_t)n, L"F%u", (unsigned)(vk - VK_F1 + 1));
    return true;
  }
  for (const auto& k : kKeyNames) {
    if (k.vk == vk) { wcsncpy(out, k.name, (size_t)n); out[n - 1] = 0; return true; }
  }
  return false;
}

static std::wstring HkFormat(UINT mods, UINT vk) {
  wchar_t key[16];
  if (!vk || !HkKeyName(vk, key, 16)) return L"";
  std::wstring s;
  if (mods & MOD_CONTROL) s += L"Ctrl+";
  if (mods & MOD_ALT)     s += L"Alt+";
  if (mods & MOD_SHIFT)   s += L"Shift+";
  if (mods & MOD_WIN)     s += L"Win+";
  s += key;
  return s;
}

static std::wstring WidenAscii(const std::string& s) {
  std::wstring w;
  w.reserve(s.size());
  for (unsigned char c : s) w.push_back((wchar_t)c);
  return w;
}

static std::string NarrowAscii(const std::wstring& w) {
  std::string s;
  s.reserve(w.size());
  for (wchar_t c : w) if (c < 128) s.push_back((char)c);
  return s;
}

// "ctrl+alt+p" -> mods / vk；解析失败返回 false
static bool HkParse(const std::string& in, UINT& mods, UINT& vk) {
  mods = 0; vk = 0;
  if (in.empty()) return false;
  std::wstring s = WidenAscii(in);
  for (auto& c : s) if (c >= L'a' && c <= L'z') c = (wchar_t)(c - L'a' + L'A');
  size_t start = 0;
  while (start <= s.size()) {
    size_t p = s.find(L'+', start);
    std::wstring tok = s.substr(start, (p == std::wstring::npos ? s.size() : p) - start);
    if (!tok.empty()) {
      if (tok == L"CTRL" || tok == L"CONTROL") mods |= MOD_CONTROL;
      else if (tok == L"ALT")   mods |= MOD_ALT;
      else if (tok == L"SHIFT") mods |= MOD_SHIFT;
      else if (tok == L"WIN")   mods |= MOD_WIN;
      else {
        UINT found = 0;
        if (tok.size() == 1) {
          wchar_t c = tok[0];
          if ((c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9')) found = (UINT)c;
        }
        if (!found && tok[0] == L'F' && tok.size() >= 2) {
          int num = _wtoi(tok.c_str() + 1);
          if (num >= 1 && num <= 24) found = (UINT)(VK_F1 + num - 1);
        }
        for (const auto& k : kKeyNames) {
          if (!found && tok == k.name) found = k.vk;
        }
        if (!found) return false;
        vk = found;
      }
    }
    if (p == std::wstring::npos) break;
    start = p + 1;
  }
  return vk != 0;
}

// ---------------- 用户设置（语言 / 开关 / 快捷键）持久化 ----------------
// 存在 exe 同目录的 settings.ini，纯 ASCII，避免编码问题。
static std::wstring SettingsPath() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    wchar_t* slash = wcsrchr(path, L'\\');
    if (slash) wcscpy(slash + 1, L"settings.ini");
    return path;
}

// ---------------- 设置持久化 ----------------
// settings.ini 是 key=value 纯文本，所有设置项都在这里。
// 语言之前单独存过，现在统一成一张表，避免"改了一项丢另一项"。
struct AppSettings {
  bool english      = false;
  bool autostart    = false;
  bool closeToTray  = true;
  bool autoSwitch   = true;
  bool autoRestore  = true;
  bool battAlert    = true;
  bool appPreset    = true;
  bool hotkeys      = true;
  // 全局快捷键（ASCII，空串 = 该项已清除）
  std::string hkOut  = "CTRL+ALT+P";
  std::string hkMute = "CTRL+ALT+M";
  std::string hkEq   = "CTRL+ALT+Q";
};

static const wchar_t* kRunKey =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const wchar_t* kRunValue = L"eposwitch";

// 开机启动是否已启用（直接读注册表，比信任 ini 可靠）
static bool AutostartEnabled() {
  HKEY k = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &k) != ERROR_SUCCESS)
    return false;
  wchar_t buf[1024] = {0};
  DWORD cb = sizeof(buf), type = 0;
  bool on = (RegQueryValueExW(k, kRunValue, nullptr, &type, (LPBYTE)buf, &cb) == ERROR_SUCCESS)
            && type == REG_SZ;
  RegCloseKey(k);
  return on;
}

static bool SetAutostart(bool on) {
  HKEY k = nullptr;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0,
                      KEY_SET_VALUE, nullptr, &k, nullptr) != ERROR_SUCCESS)
    return false;
  bool ok = true;
  if (on) {
    wchar_t path[MAX_PATH] = {0};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring v = L"\"" + std::wstring(path) + L"\"";
    ok = (RegSetValueExW(k, kRunValue, 0, REG_SZ, (const BYTE*)v.c_str(),
                         (DWORD)((v.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS);
  } else {
    RegDeleteValueW(k, kRunValue);   // 不存在也算成功
  }
  RegCloseKey(k);
  return ok;
}

static AppSettings LoadSettings() {
  AppSettings a;
  FILE* f = _wfopen(SettingsPath().c_str(), L"r");
  if (f) {
    char line[256];
    while (fgets(line, sizeof(line), f)) {
      char* eq = strchr(line, '=');
      if (!eq || line[0] == ';' || line[0] == '#') continue;
      *eq = 0;
      std::string key(line), val(eq + 1);
      while (!val.empty() && (val.back() == '\n' || val.back() == '\r' ||
                              val.back() == ' ' || val.back() == '\t')) val.pop_back();
      bool on = (val == "1" || val == "on" || val == "true" || val == "en");
      if (key == "language")        a.english     = on;
      else if (key == "autostart")  a.autostart   = on;
      else if (key == "close_to_tray") a.closeToTray = on;
      else if (key == "auto_switch")   a.autoSwitch  = on;
      else if (key == "auto_restore")  a.autoRestore = on;
      else if (key == "batt_alert")    a.battAlert   = on;
      else if (key == "app_preset")    a.appPreset   = on;
      else if (key == "hotkeys")       a.hotkeys     = on;
      else if (key == "hotkey_out")    a.hkOut       = val;
      else if (key == "hotkey_mute")   a.hkMute      = val;
      else if (key == "hotkey_eq")     a.hkEq        = val;
    }
    fclose(f);
  }
  // 开机启动以注册表为准（用户可能在任务管理器里手动禁用过）
  a.autostart = AutostartEnabled();
  // 程序被移动/改名后，注册表里的路径会失效 —— 每次启动自愈一次
  if (a.autostart) SetAutostart(true);
  return a;
}

static void SaveSettings(const AppSettings& a) {
  FILE* f = _wfopen(SettingsPath().c_str(), L"w");
  if (!f) return;
  fprintf(f, "; EPOS Control settings\n");
  fprintf(f, "language=%s\n",      a.english     ? "en" : "zh");
  fprintf(f, "autostart=%d\n",     a.autostart ? 1 : 0);
  fprintf(f, "close_to_tray=%d\n", a.closeToTray ? 1 : 0);
  fprintf(f, "auto_switch=%d\n",   a.autoSwitch ? 1 : 0);
  fprintf(f, "auto_restore=%d\n",  a.autoRestore ? 1 : 0);
  fprintf(f, "batt_alert=%d\n",    a.battAlert ? 1 : 0);
  fprintf(f, "app_preset=%d\n",    a.appPreset ? 1 : 0);
  fprintf(f, "hotkeys=%d\n",       a.hotkeys ? 1 : 0);
  fprintf(f, "hotkey_out=%s\n",    a.hkOut.c_str());
  fprintf(f, "hotkey_mute=%s\n",   a.hkMute.c_str());
  fprintf(f, "hotkey_eq=%s\n",     a.hkEq.c_str());
  fclose(f);
}

// 从当前全局状态整理出一份设置（保存时用）
static AppSettings CurrentSettings() {
  AppSettings a;
  a.english     = g_app.english;
  a.autostart   = g_app.setAutostart;
  a.closeToTray = g_app.setCloseToTray;
  a.autoSwitch  = g_autoSwitch;
  a.autoRestore = g_restoreOn;
  a.battAlert   = g_battAlertOn;
  a.appPreset   = g_appPresetOn;
  a.hotkeys     = g_hkEnabled;
  a.hkOut  = NarrowAscii(HkFormat(g_hkSlots[0].mods, g_hkSlots[0].vk));
  a.hkMute = NarrowAscii(HkFormat(g_hkSlots[1].mods, g_hkSlots[1].vk));
  a.hkEq   = NarrowAscii(HkFormat(g_hkSlots[2].mods, g_hkSlots[2].vk));
  return a;
}

// 把设置应用回全局状态
static void ApplySettings(const AppSettings& a) {
  g_app.english       = a.english;
  g_panel.english     = a.english;
  g_app.setAutostart  = a.autostart;
  g_app.setCloseToTray = a.closeToTray;
  g_app.setAutoSwitch = a.autoSwitch;
  g_app.setAutoRestore = a.autoRestore;
  g_app.setBattAlert  = a.battAlert;
  g_app.setAppPreset  = a.appPreset;
  g_autoSwitch  = a.autoSwitch;
  g_restoreOn   = a.autoRestore;
  g_battAlertOn = a.battAlert;
  g_appPresetOn = a.appPreset;
  g_app.setHotkeys = a.hotkeys;
  g_hkEnabled = a.hotkeys;
  // 组合键：解析失败/为空一律按"未设置"处理（UI 显示"未设置"，不注册）
  UINT m = 0, v = 0;
  const std::string* src[3] = { &a.hkOut, &a.hkMute, &a.hkEq };
  for (int i = 0; i < 3; i++) {
    if (HkParse(*src[i], m, v)) { g_hkSlots[i].mods = m; g_hkSlots[i].vk = v; }
    else { g_hkSlots[i].mods = 0; g_hkSlots[i].vk = 0; }
    g_hkSlots[i].ok = false;
  }
}

static bool LoadEnglishPref() {
    std::wstring p = SettingsPath();
    FILE* f = _wfopen(p.c_str(), L"r");
    if (!f) return false;
    char line[128] = {0};
    bool en = false;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "language=en", 12) == 0) { en = true; break; }
    }
    fclose(f);
    return en;
}

static void SaveEnglishPref(bool english) {
    // ★ 只改语言时必须把其它设置一起写回去，否则会把它们清掉
    g_app.english = english;
    SaveSettings(CurrentSettings());
}

// 统一切换语言（侧栏下拉 / 设置页列表都走这里）
static void SetLanguage(bool english) {
    if (g_app.english == english) return;
    g_app.english = english;
    g_panel.english = english;
    SaveSettings(CurrentSettings());
    Log(L"ui: language -> %ls", english ? L"English" : L"Chinese");
}


#define WM_TRAYICON (WM_APP + 1)
// 启动后置初始化：等消息循环跑起来再做（保证窗口首帧可响应）
#define WM_APP_BOOT (WM_APP + 2)
// （全局快捷键的 ID 与可改键逻辑定义在上面的"全局快捷键"块里）
#define ID_TRAY 1001
#define IDM_SHOW   2001
#define IDM_AUTOSW 2002
#define IDM_EXIT   2003
#define IDM_EQ_BASE 2100
#define IDM_FX      2200
#define IDM_PAGE_BASE 2300
#define IDM_PRESET_BASE 2400
#define IDM_MUTE_SPK    2501
#define IDM_VOL_SPK_50  2502
#define IDM_VOL_SPK_100 2503
#define IDM_BATT_ALERT  2601
#define IDM_AUTO_RESTORE 2602
#define IDM_APP_PRESET  2603
#define IDM_RELOAD_RULES 2604
#define IDM_SETTINGS    2605

static ULONG_PTR g_gdiToken = 0;
static HWND  g_hwnd = nullptr;
static bool  g_winVisible = true;
static int   g_hoverRow = -1;
static int   g_hoverBtn = -1;
static bool  g_dragging = false;
static POINT g_dragOffset = {0,0};
static UINT  g_dpi = 96;
static DWORD g_tBoot = 0;            // 进程启动时刻（启动性能打点用）
static int   g_simBattery = -1;      // 调试用：--sim-battery=NN 模拟电量，-1 = 用真实值

// ---------------- 全局快捷键：注册 / 录制 ----------------
static void HkSay(const std::wstring& msg) { g_hkMsg = msg; g_hkMsgAt = GetTickCount(); }

// 全部注销后按当前设置重新注册；失败的项标 ok=false（多半是被别的软件占了）
static void RegisterAllHotkeys() {
  if (!g_hwnd) return;
  for (int i = 0; i < 3; i++) {
    UnregisterHotKey(g_hwnd, HK_CYCLE_OUT + i);
    g_hkSlots[i].ok = false;
  }
  if (!g_hkEnabled) return;
  for (int i = 0; i < 3; i++) {
    HotkeySlot& s = g_hkSlots[i];
    if (!s.vk) continue;
    s.ok = RegisterHotKey(g_hwnd, HK_CYCLE_OUT + i, s.mods | MOD_NOREPEAT, s.vk) != 0;
    if (!s.ok) {
      HkSay(std::wstring(g_app.english ? L"Cannot register " : L"无法注册 ") +
            HkFormat(s.mods, s.vk) +
            (g_app.english ? L": taken by another app" : L"：已被其它程序占用"));
    }
  }
}

// 进入录制态：先把所有热键注销，否则按到旧组合键会直接触发功能
static void HkBeginCapture(int i) {
  if (i < 0 || i > 2) return;
  g_hkCapture = i;
  g_app.hkEditing = i;          // 立刻反映到界面（不等 2 秒定时器）
  g_hkMsg.clear();
  if (g_hwnd) {
    for (int k = 0; k < 3; k++) UnregisterHotKey(g_hwnd, HK_CYCLE_OUT + k);
    SetForegroundWindow(g_hwnd);
    SetFocus(g_hwnd);
  }
  if (g_hwnd) InvalidateRect(g_hwnd, nullptr, FALSE);
}

static void HkCancelCapture() {
  if (g_hkCapture < 0) return;
  g_hkCapture = -1;
  g_app.hkEditing = -1;
  RegisterAllHotkeys();
  if (g_hwnd) InvalidateRect(g_hwnd, nullptr, FALSE);
}

// 把快捷键状态同步给界面（改键后立刻刷新，不用等 2 秒定时器）
static void HkSyncView() {
  g_app.setHotkeys = g_hkEnabled;
  g_app.hkEditing  = g_hkCapture;
  for (int i = 0; i < 3; i++) {
    g_app.hk[i].text = HkFormat(g_hkSlots[i].mods, g_hkSlots[i].vk);
    // vk==0（未设置）或总开关关掉时不算失败，不标红
    g_app.hk[i].ok = g_hkSlots[i].ok || g_hkSlots[i].vk == 0 || !g_hkEnabled;
  }
}


// 布局常量（96dpi 基准，按 DPI 缩放）
static const int BASE_W = 880;
static const int BASE_H = 760;
static int  SW = BASE_W, SH = BASE_H;

static inline int S(int v) { return MulDiv(v, g_dpi, 96); }

// 配色
static const Color C_BG      (255, 0x15, 0x17, 0x1B);
static const Color C_CARD    (255, 0x20, 0x23, 0x29);
static const Color C_CARD_HI (255, 0x2B, 0x2F, 0x37);
static const Color C_TEXT    (255, 0xE9, 0xED, 0xF1);
static const Color C_DIM     (255, 0x8A, 0x93, 0x9D);
static const Color C_TRACK   (255, 0x2A, 0x2E, 0x35);
static const Color C_OK      (255, 0x3D, 0xC1, 0x7E);
static const Color C_WARN    (255, 0xE0, 0xA4, 0x58);
static const Color C_BAD     (255, 0xD4, 0x69, 0x5F);
static const Color C_BLUE    (255, 0x46, 0xA8, 0xE0);

static Color LevelColor(int pct, bool online) {
  if (!online) return C_TRACK;
  if (pct < 0)  return C_DIM;
  if (pct <= 20) return C_BAD;
  if (pct <= 40) return C_WARN;
  return C_OK;
}

// 可切换的端点列表（排除虚拟设备）
static void BuildSwitchList(std::vector<const Endpoint*>& out) {
  out.clear();
  for (auto& e : g_eps) {
    if (e.kind == KIND_EXCLUDED || e.kind == KIND_VIRTUAL) continue;
    out.push_back(&e);
    if (out.size() >= 4) break;
  }
}

// ---------------- 设备页：每行音量/静音刷新 ----------------
// 只在设备页可见时刷新（2 秒一次 + 切页时立即一次），平时零开销。
// 每个端点要 Activate 一次 IAudioEndpointVolume，约 0.1ms/个，10 个端点无压力。
static void RefreshDevVolumes() {
  bool cap = (g_app.devTab == 1);
  std::vector<Endpoint>* list = cap ? g_app.captureEndpoints : g_app.endpoints;
  if (!list) { g_app.devVol.clear(); g_app.devMuted.clear(); return; }
  eui2::Layout L = eui2::ComputeLayout(g_app);
  eui2::DevRows dr = eui2::DevLayout(g_app, L, cap);
  g_app.devVol.assign(dr.idx.size(), -1);
  g_app.devMuted.assign(dr.idx.size(), false);
  for (int r = 0; r < (int)dr.idx.size(); r++) {
    const Endpoint& ep = (*list)[dr.idx[r]];
    g_app.devVol[r]   = g_audio.GetVolumePct(ep.id, cap);
    g_app.devMuted[r] = g_audio.IsMuted(ep.id);
  }
}


// ---- 布局计算 ----
static int RowTop(int i) { return S(292) + i * (S(56) + S(8)); }
static RectF CloseRect() { return RectF((REAL)(SW - S(46)), (REAL)S(14), (REAL)S(32), (REAL)S(32)); }
static RectF AutoRect()  { return RectF((REAL)S(16), (REAL)(SH - S(64)), (REAL)(SW - S(32)), (REAL)S(48)); }

// ---- 音效参数下发 ----
static void ApplySoundToDevice() {
  if (!g_pipe.Ready()) { Log(L"sound: pipe not ready"); return; }
  epospipe::Client::ApoParams ap;
  ap.speakerEq9 = g_panel.sound.speakerEq;
  ap.micEq9     = g_panel.sound.micEq;
  ap.reverbLevel    = g_panel.sound.reverb;
  ap.noiseGateLevel = g_panel.sound.noiseGate;
  ap.directSound    = g_panel.sound.directSound ? 0 : -1;
  ap.micHpfEnabled  = -1;
  ap.micFrcEnabled  = -1;
  ap.micFrcPreset   = -1;
  ap.micNrEnabled   = g_panel.sound.voiceEnhance ? -1 : 0;
  ap.micNrPreset    = -1;
  ap.micMetersEnabled = -1;
  ap.autoGainAdjust = -1;
  g_pipe.Request(epospipe::Client::BuildApoFull(ap));
  Log(L"sound applied: eq=[%.2f %.2f ...] rev=%.0f gate=%.0f",
      g_panel.sound.speakerEq[0], g_panel.sound.speakerEq[1],
      g_panel.sound.reverb, g_panel.sound.noiseGate);
  g_panel.dirty = false;
}

// 顶部标签栏（状态 / 音效）
static RectF TabRect(int i) {
  int w = S(84);
  return RectF((REAL)(S(16) + i * (w + S(8))), (REAL)S(12), (REAL)w, (REAL)S(28));
}

static void DrawTabs(Graphics& g) {
  FontFamily ff(L"Segoe UI");
  Font fT(&ff, (REAL)S(12), FontStyleBold, UnitPixel);
  SolidBrush brT(C_TEXT), brD(C_DIM), brW(Color(255,255,255,255));
  StringFormat sf; sf.SetAlignment(StringAlignmentCenter); sf.SetLineAlignment(StringAlignmentCenter);
  const wchar_t* names[2];
  if (g_panel.english) { names[0] = L"Status"; names[1] = L"Sound"; }
  else                  { names[0] = L"状态";  names[1] = L"音效";  }
  for (int i = 0; i < 2; i++) {
    RectF r = TabRect(i);
    bool sel = ((i == 1) == g_showFx);
    epospanel::DrawCard(g, r, sel);
    if (sel) {
      SolidBrush ac(C_BLUE);
      g.FillRectangle(&ac, r.X, r.Y + r.Height - (REAL)S(3), r.Width, (REAL)S(3));
    }
    RectF tb; g.MeasureString(names[i], -1, &fT, r, &sf, &tb);
    PointF org(r.X + (r.Width - tb.Width) / 2.0f,
               r.Y + (r.Height - tb.Height) / 2.0f);
    g.DrawString(names[i], -1, &fT, org, sel ? (Brush*)&brW : (Brush*)&brD);
  }
}

static void DrawUI(Graphics& g) {
  g.SetSmoothingMode(SmoothingModeAntiAlias);
  g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
  g.SetPixelOffsetMode(PixelOffsetModeNone);

  // 背景
  SolidBrush bg(C_BG);
  g.FillRectangle(&bg, 0, 0, SW, SH);

  // ---- 音效页 ----
  if (g_showFx) {
    DrawTabs(g);
    g_panel.ready = g_pipe.Ready();
    g_panel.presets = eposfx::AllPresets();
    g_fxLayout = epospanel::ComputeLayout(SW, SH, g_dpi, g_panel);
    epospanel::Draw(g, g_panel, g_fxLayout);
    return;
  }

  DrawTabs(g);

  // ---- 头部 ----
  bool online = (g_probe == PROBE_ONLINE);
  int  pctNow = (g_battery >= 0) ? g_battery : -1;
  bool stale  = (!online && pctNow >= 0);   // 有读数但未确认
  {
    FontFamily ff(L"Segoe UI");
    Font fTitle(&ff, (REAL)S(19), FontStyleBold, UnitPixel);
    Font fSub(&ff, (REAL)S(12), FontStyleRegular, UnitPixel);
    SolidBrush brT(C_TEXT), brD(C_DIM);

    g.DrawString(L"EPOS H3PRO", -1, &fTitle, PointF((REAL)S(20), (REAL)S(18)), &brT);

    // 状态点
    Color sc = !g_pipeReady ? C_WARN : (online ? C_OK : (stale ? C_WARN : C_DIM));
    SolidBrush sb(sc);
    REAL dotX = (REAL)S(20), dotY = (REAL)S(48);
    g.FillEllipse(&sb, dotX, dotY, (REAL)S(8), (REAL)S(8));
    const wchar_t* st = online ? L"已连接"
                     : stale  ? L"上次读数"
                     : (g_probe == PROBE_NO_DONGLE ? L"未插入" : L"未连接");
    g.DrawString(st, -1, &fSub, PointF(dotX + S(14), dotY - S(4)), &brD);

    // 关闭按钮
    RectF cr = CloseRect();
    Pen pn(C_DIM, 1.6f);
    pn.SetStartCap(LineCapRound); pn.SetEndCap(LineCapRound);
    if (g_hoverBtn == 100) {
      SolidBrush hb(C_CARD_HI);
      g.FillEllipse(&hb, cr);
    }
    REAL m = S(11);
    g.DrawLine(&pn, cr.X + m, cr.Y + m, cr.X + cr.Width - m, cr.Y + cr.Height - m);
    g.DrawLine(&pn, cr.X + cr.Width - m, cr.Y + m, cr.X + m, cr.Y + cr.Height - m);
  }

  // ---- 环形电量 ----
  {
    REAL cx = SW / 2.0f, cy = (REAL)S(178);
    REAL r  = (REAL)S(78);
    REAL th = (REAL)S(11);
    RectF rc(cx - r, cy - r, 2 * r, 2 * r);

    Pen track(C_TRACK, th);
    g.DrawEllipse(&track, rc);

    int pct = pctNow;
    if (pct > 0) {
      Color lc = stale ? C_WARN : LevelColor(pct, true);
      Pen prog(lc, th);
      prog.SetStartCap(LineCapRound);
      prog.SetEndCap(LineCapRound);
      g.DrawArc(&prog, rc, -90.0f, 360.0f * pct / 100.0f);
    }

    FontFamily ff(L"Segoe UI");
    StringFormat sf;
    sf.SetAlignment(StringAlignmentCenter);
    sf.SetLineAlignment(StringAlignmentCenter);
    SolidBrush brT(C_TEXT), brD(C_DIM);

    wchar_t big[16];
    if (pct >= 0) swprintf(big, 16, L"%d", pct);
    else          wcscpy(big, L"--");
    Font fBig(&ff, (REAL)S(52), FontStyleBold, UnitPixel);
    RectF rcBig(cx - r, cy - S(34), 2 * r, S(52));
    g.DrawString(big, -1, &fBig, rcBig, &sf, &brT);

    Font fPct(&ff, (REAL)S(15), FontStyleRegular, UnitPixel);
    RectF rcPct(cx - r, cy + S(20), 2 * r, S(22));
    g.DrawString(pct >= 0 ? L"%" : L"", -1, &fPct, rcPct, &sf, &brD);

    // 副标题
    const wchar_t* sub;
    if (!g_pipeReady)
      sub = L"Gaming Suite 在运行 · 功能受限";   // 官方 UI 占用 GSUIPipe
    else if (online)
      sub = L"耳机在线 · 2.4 GHz";
    else if (stale)
      sub = L"读数已过期";
    else
      sub = L"未检测到耳机";
    Font fSub(&ff, (REAL)S(13), FontStyleRegular, UnitPixel);
    StringFormat sfNoWrap;
    sfNoWrap.SetAlignment(StringAlignmentCenter);
    sfNoWrap.SetLineAlignment(StringAlignmentCenter);
    sfNoWrap.SetFormatFlags(StringFormatFlagsNoWrap);
    sfNoWrap.SetTrimming(StringTrimmingEllipsisCharacter);
    RectF rcSub((REAL)S(20), cy + r + S(8), (REAL)(SW - S(40)), S(22));
    g.DrawString(sub, -1, &fSub, rcSub, &sfNoWrap, &brD);
  }

  // ---- 设备列表 ----
  std::vector<const Endpoint*> list;
  BuildSwitchList(list);
  std::wstring curId = g_audio.DefaultId(eConsole);

  {
    FontFamily ff(L"Segoe UI");
    Font fName(&ff, (REAL)S(14), FontStyleRegular, UnitPixel);
    Font fKind(&ff, (REAL)S(11), FontStyleRegular, UnitPixel);

    for (int i = 0; i < (int)list.size(); i++) {
      RectF r((REAL)S(16), (REAL)RowTop(i), (REAL)(SW - S(32)), (REAL)S(56));
      bool isCur = (list[i]->id == curId);
      bool hov = (g_hoverRow == i);

      SolidBrush cb(hov ? C_CARD_HI : C_CARD);
      g.FillRectangle(&cb, r);

      // 左侧强调条
      if (isCur) {
        SolidBrush ac(C_OK);
        g.FillRectangle(&ac, r.X, r.Y, (REAL)S(3), r.Height);
      }

      SolidBrush brT(isCur ? C_TEXT : Color(255, 0xC2, 0xC9, 0xD1));
      SolidBrush brD(C_DIM);

      // 名称（截断过长）
      std::wstring nm = list[i]->name;
      if (nm.size() > 28) nm = nm.substr(0, 27) + L"…";
      g.DrawString(nm.c_str(), -1, &fName, PointF(r.X + S(16), r.Y + S(10)), &brT);
      g.DrawString(KindName(list[i]->kind), -1, &fKind,
                   PointF(r.X + S(16), r.Y + S(31)), &brD);

      // 右侧：当前标记
      if (isCur) {
        Pen pk(C_OK, 2.0f);
        pk.SetStartCap(LineCapRound); pk.SetEndCap(LineCapRound);
        REAL bx = r.X + r.Width - S(28), by = r.Y + S(28);
        g.DrawLine(&pk, bx - S(6), by, bx - S(1), by + S(5));
        g.DrawLine(&pk, bx - S(1), by + S(5), bx + S(7), by - S(6));
      }
    }
  }

  // ---- 自动切换 ----
  {
    RectF r = AutoRect();
    SolidBrush cb(C_CARD);
    g.FillRectangle(&cb, r);

    FontFamily ff(L"Segoe UI");
    Font fT(&ff, (REAL)S(14), FontStyleRegular, UnitPixel);
    Font fS(&ff, (REAL)S(11), FontStyleRegular, UnitPixel);
    SolidBrush brT(C_TEXT), brD(C_DIM);

    g.DrawString(L"自动切换", -1, &fT, PointF(r.X + S(16), r.Y + S(9)), &brT);
    g.DrawString(g_autoSwitch ? L"蓝牙 → EPOS → 声卡" : L"已暂停（手动模式）",
                 -1, &fS, PointF(r.X + S(16), r.Y + S(27)), &brD);

    // 开关
    REAL tw = (REAL)S(44), th = (REAL)S(24);
    RectF tr(r.X + r.Width - tw - S(14), r.Y + (r.Height - th) / 2, tw, th);
    SolidBrush tb(g_autoSwitch ? C_OK : C_TRACK);
    GraphicsPath pill;
    pill.AddArc(tr.X, tr.Y, th, th, 90, 180);
    pill.AddArc(tr.X + tw - th, tr.Y, th, th, 270, 180);
    pill.CloseFigure();
    g.FillPath(&tb, &pill);

    REAL kx = g_autoSwitch ? (tr.X + tw - th + S(2)) : (tr.X + S(2));
    SolidBrush kb(Color(255, 255, 255, 255));
    g.FillEllipse(&kb, kx, tr.Y + S(2), th - S(4), th - S(4));
  }
}

// ---- 双击缓冲绘制 ----
static void Repaint(HWND hwnd) {
  PAINTSTRUCT ps;
  HDC hdc = BeginPaint(hwnd, &ps);
  HDC mem = CreateCompatibleDC(hdc);
  HBITMAP bmp = CreateCompatibleBitmap(hdc, SW, SH);
  HGDIOBJ old = SelectObject(mem, bmp);
  {
    Graphics g(mem);
    // ★ 文字清晰度：这组参数是实测出来的（见 hint_compare.png，5 种组合横向对比）
    //   AntiAliasGridFit  : 把笔画对齐到像素网格，小字号才不发虚
    //   PixelOffsetModeNone: 关掉 GDI+ 默认的半像素偏移；默认 Half 会让整行字整体
    //                        落在半像素上 -> 所有笔画都糊一层灰边
    //   不用 ClearType：在内存 DC + 深色自绘背景上会产生明显彩色描边（放大能看到青/橙边）
    g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(PixelOffsetModeNone);
    if (g_newUi) eui2::DrawAll(g, g_app);
    else         DrawUI(g);
  }
  BitBlt(hdc, 0, 0, SW, SH, mem, 0, 0, SRCCOPY);
  SelectObject(mem, old);
  DeleteObject(bmp);
  DeleteDC(mem);
  EndPaint(hwnd, &ps);
}

// ---- 托盘图标：环形进度 + 中间数字 ----
static void DrawTrayGlyph(Graphics& g, int SZ) {
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.Clear(Color(0, 0, 0, 0));

    bool online = (g_probe == PROBE_ONLINE);
    int pct = (g_battery >= 0) ? g_battery : -1;
    // 有读数就上色（在线按电量区间，过期用琥珀）；完全没数据才用灰
    Color lc = online ? LevelColor(pct, true)
             : (pct >= 0 ? C_WARN : Color(255, 0x9A, 0xA3, 0xAD));

    REAL th = SZ / 7.0f;
    REAL r = (SZ - th) / 2.0f - SZ / 32.0f;
    REAL cx = SZ / 2.0f, cy = SZ / 2.0f;
    RectF rc(cx - r, cy - r, 2 * r, 2 * r);

    Pen track(Color(online ? 110 : 70, 255, 255, 255), th);
    g.DrawEllipse(&track, rc);

    if (pct > 0) {
      Pen prog(lc, th);
      prog.SetStartCap(LineCapRound);
      prog.SetEndCap(LineCapRound);
      g.DrawArc(&prog, rc, -90.0f, 360.0f * pct / 100.0f);
    }

    // 中心状态点：在线/有读数=实心，完全无数据=空心
    REAL dr = r * 0.42f;
    if (online || pct >= 0) {
      SolidBrush sb(lc);
      g.FillEllipse(&sb, cx - dr, cy - dr, dr * 2, dr * 2);
    } else {
      Pen dp(Color(160, 200, 205, 212), SZ / 16.0f);
      g.DrawEllipse(&dp, cx - dr, cy - dr, dr * 2, dr * 2);
    }
}

static HICON MakeTrayIcon() {
  const int SZ = 32;
  Bitmap bmp(SZ, SZ, PixelFormat32bppARGB);
  {
    Graphics g(&bmp);
    DrawTrayGlyph(g, SZ);
  }
  HICON ic = nullptr;
  bmp.GetHICON(&ic);
  return ic;
}

static int g_devCount = -1;

static void ResizeToFit(HWND hwnd) {
  int wantH;
  if (g_newUi) {
    wantH = S(BASE_H);
    if (SH == wantH) return;
  } else if (g_showFx) {
    wantH = S(BASE_H);
    if (SH == wantH) return;
  } else {
    std::vector<const Endpoint*> list;
    BuildSwitchList(list);
    int n = (int)list.size();
    if (n == g_devCount && SH != S(BASE_H)) return;
    g_devCount = n;
    wantH = S(292) + n * (S(56) + S(8)) + S(76);
  }
  SH = wantH;
  SetWindowPos(hwnd, nullptr, 0, 0, SW, SH, SWP_NOMOVE | SWP_NOZORDER);
  HRGN rgn = CreateRoundRectRgn(0, 0, SW + 1, SH + 1, S(14), S(14));
  SetWindowRgn(hwnd, rgn, TRUE);
}

static void UpdateTray() {
  DWORD t0 = GetTickCount();
  g_eps = g_audio.ActiveRender();
  g_epc = g_audio.ActiveCapture();
  DWORD t1 = GetTickCount();
  g_probe = ProbeEpos(&g_battery);
  if (g_simBattery >= 0) g_battery = g_simBattery;   // 调试用
  DWORD t2 = GetTickCount();
  if (g_hwnd) ResizeToFit(g_hwnd);
  DWORD t3 = GetTickCount();
  if (t3 - t0 > 200) {
    Log(L"UpdateTray slow: endpoints=%lu probe=%lu resize=%lu total=%lu",
        t1 - t0, t2 - t1, t3 - t2, t3 - t0);
  }

  DWORD t4 = GetTickCount();
  HICON ic = MakeTrayIcon();
  DWORD t5 = GetTickCount();
  NOTIFYICONDATAW nid = {};
  nid.cbSize = sizeof(nid);
  nid.hWnd = g_hwnd;
  nid.uID = ID_TRAY;
  nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
  nid.uCallbackMessage = WM_TRAYICON;
  nid.hIcon = ic;

  std::wstring tip = L"EPOS H3PRO\n";
  std::wstring curName = L"(无)";
  std::wstring curId = g_audio.DefaultId(eConsole);
  for (auto& e : g_eps) if (e.id == curId) curName = e.name;

  wchar_t b[64];
  if (g_battery >= 0) swprintf(b, 64, L"电量 %d%%", g_battery);
  else                                        wcscpy(b, L"电量未知");
  tip += b;
  tip += (g_probe == PROBE_ONLINE) ? L" · 已连接" : L" · 未连接";
  tip += L"\n输出: " + curName;
  wcsncpy(nid.szTip, tip.c_str(), 127);

  std::wstring curId2 = g_audio.DefaultId(eConsole);
  DWORD t6 = GetTickCount();
  static bool added = false;
  if (!added) { Shell_NotifyIconW(NIM_ADD, &nid); added = true; }
  else        Shell_NotifyIconW(NIM_MODIFY, &nid);
  DWORD t7 = GetTickCount();
  if (ic) DestroyIcon(ic);
  if (t7 - t0 > 200) {
    Log(L"UpdateTray detail: endpoints=%lu probe=%lu resize=%lu icon=%lu defid=%lu nid=%lu total=%lu",
        t1-t0, t2-t1, t3-t2, t5-t4, t6-t5, t7-t6, t7-t0);
  }
}

static void AutoDecide() {
  if (!g_autoSwitch) return;
  bool uncertain = false;
  const Endpoint* want = g_strategy.Pick(g_eps, g_probe, &uncertain);
  std::wstring cur = g_audio.DefaultId(eConsole);
  if (g_dryRun) return;
  if (uncertain) { Log(L"HOLD epos probe=unknown, keep current=[%ls]", cur.c_str()); return; }
  DWORD now = GetTickCount();
  if (now - g_lastSwitchTick < 900) return;
  if (!want) return;
  if (cur != want->id) {
    if (g_audio.SetDefault(want->id)) {
      g_lastSwitchTick = now;
      Log(L"SWITCH from=[%ls] to=[%ls]", cur.c_str(), want->id.c_str());
    }
  }
}

// ============================================================================
// 以下是官方 Gaming Suite 没有的能力（针对用户实际抱怨的三件事）
//   1) 低电量主动提醒 —— 官方只在关机前说一句 "Recharge headset, powering off"
//   2) 耳机回来自动切回 —— 官方遇到断连只会"认不到设备"，不提供任何自愈
//   3) 按前台应用自动切预设 —— 官方只有 4 个预设且必须手动点
// ============================================================================

static void ApplySoundToDevice2();
static void UpdateTray();

// ---------------- 托盘气泡提示 ----------------
// 注意：NIM_MODIFY 只在图标已注册时生效，所以没注册就退回 NIM_ADD。
static void TrayBalloon(const wchar_t* title, const wchar_t* text, DWORD icon = NIIF_INFO) {
  if (!g_hwnd) return;
  NOTIFYICONDATAW nid = {};
  nid.cbSize = sizeof(nid);
  nid.hWnd  = g_hwnd;
  nid.uID   = ID_TRAY;
  nid.uFlags = NIF_INFO;
  nid.dwInfoFlags = icon;
  wcsncpy(nid.szInfoTitle, title, 63);
  wcsncpy(nid.szInfo, text, 255);
  if (!Shell_NotifyIconW(NIM_MODIFY, &nid)) {
    nid.uFlags |= NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = MakeTrayIcon();
    Shell_NotifyIconW(NIM_ADD, &nid);
    if (nid.hIcon) DestroyIcon(nid.hIcon);
  }
}

// ---------------- 1) 低电量提醒 ----------------
static int  g_lowBattFired = 0;     // 已提醒到哪一档（0 = 本轮还没提醒过）

static void CheckBatteryAlert() {
  if (!g_battAlertOn || g_battery < 0) return;
  // 电量回升到 25% 以上视为换了新的一轮放电，重新允许提醒
  if (g_battery > 25) { g_lowBattFired = 0; return; }

  int th = (g_battery <= 10) ? 10 : (g_battery <= 20) ? 20 : 0;
  if (th == 0) return;
  if (g_lowBattFired != 0 && g_lowBattFired <= th) return;   // 这一档（或更低档）提醒过了
  g_lowBattFired = th;

  wchar_t title[64], body[192];
  const bool en = g_app.english;
  if (th <= 10) {
    wcscpy(title, en ? L"Battery critically low" : L"电量严重不足");
    swprintf(body, 192, en ? L"Headset battery %d%% — plug in now to avoid shutdown."
                           : L"耳机电量仅剩 %d%%，请立即充电，否则会自动关机。", g_battery);
  } else {
    wcscpy(title, en ? L"Battery low" : L"电量偏低");
    swprintf(body, 192, en ? L"Headset battery %d%% — consider charging."
                           : L"耳机电量 %d%%，建议尽快充电。", g_battery);
  }
  TrayBalloon(title, body, th <= 10 ? NIIF_WARNING : NIIF_INFO);
  MessageBeep(th <= 10 ? MB_ICONHAND : MB_ICONASTERISK);
  Log(L"alert: battery %d%% (threshold %d)", g_battery, th);
}

// ---------------- 2) 耳机断开后自动回切 ----------------
// 只在"掉线前 EPOS 恰好是默认播放设备"时才回切，
// 避免用户手动选了别的设备又被程序抢回去。
static bool g_eposWasDefault = false;   // 掉线那一刻 EPOS 是否仍是默认设备
static ProbeResult g_prevProbe = PROBE_UNKNOWN;

static void CheckAutoRestore() {
  bool wasOnline = (g_prevProbe == PROBE_ONLINE);
  bool nowOnline = (g_probe   == PROBE_ONLINE);

  if (wasOnline && !nowOnline) {
    // 刚掉线：看它掉线前是不是默认设备（是的话说明用户本来就在用它）
    std::wstring cur = g_audio.DefaultId(eConsole);
    g_eposWasDefault = false;
    for (auto& e : g_eps)
      if (e.kind == KIND_EPOS && e.id == cur) { g_eposWasDefault = true; break; }
    if (g_eposWasDefault)
      Log(L"watchdog: headset offline while it was the default output");
  } else if (!wasOnline && nowOnline) {
    // 刚回来
    if (g_restoreOn && g_eposWasDefault) {
      std::wstring cur = g_audio.DefaultId(eConsole);
      for (auto& e : g_eps) {
        if (e.kind != KIND_EPOS) continue;
        if (e.id == cur) break;
        if (g_audio.SetDefault(e.id)) {
          Log(L"watchdog: headset back -> restored default to [%ls]", e.name.c_str());
          TrayBalloon(g_app.english ? L"Headset reconnected" : L"耳机已重新连接",
                      g_app.english ? L"Switched audio output back to the headset."
                                    : L"已把音频输出切回耳机。", NIIF_INFO);
        }
        break;
      }
    }
    g_eposWasDefault = false;
  }
  g_prevProbe = g_probe;
}

// ---------------- 麦克风实时电平（输入测试用）----------------
// ★ 为什么不用 IAudioMeterInformation：
//   实测这台机器上三个采集端点的 GetPeakValue 全部返回 0（包括真实硬件），
//   因为驱动只在"有活动录音流"时才跑电平表。
//   所以这里自己开一条共享模式的采集流来读样本 —— 和语音软件做的事一样。
static DWORD g_micLoudAt = 0;
static DWORD g_micHoldAt = 0;
static DWORD g_micSilentAt = 0;   // 最近一次"有信号"的时刻

class MicMonitor {
public:
  ~MicMonitor() { Stop(); }

  void Start(const std::wstring& id) {
    if (m_run && id == m_id) return;
    Stop();
    m_id = id;
    m_run = true;
    m_peak = 0.0f;
    m_thread = std::thread([this] { Loop(); });
  }

  void Stop() {
    m_run = false;
    if (m_thread.joinable()) m_thread.join();
    m_id.clear();
    m_peak = 0.0f;
  }

  float TakePeak() { return m_peak.exchange(0.0f); }
  bool  Running() const { return m_run.load(); }

private:
  void Loop() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IMMDeviceEnumerator* en = nullptr;
    IMMDevice* dev = nullptr;
    IAudioClient* ac = nullptr;
    IAudioCaptureClient* cc = nullptr;
    WAVEFORMATEX* wf = nullptr;

    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                __uuidof(IMMDeviceEnumerator), (void**)&en)) || !en)
      goto done;
    if (m_id.empty() || FAILED(en->GetDevice(m_id.c_str(), &dev)) || !dev) goto done;
    if (FAILED(dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&ac)) || !ac)
      goto done;
    if (FAILED(ac->GetMixFormat(&wf)) || !wf) goto done;
    // 200ms 缓冲，避免 80ms 的 UI 节拍跟不上
    if (FAILED(ac->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, 2000000, 0, wf, nullptr)))
      goto done;
    if (FAILED(ac->GetService(__uuidof(IAudioCaptureClient), (void**)&cc)) || !cc) goto done;
    if (FAILED(ac->Start())) goto done;

    while (m_run) {
      UINT32 packet = 0;
      if (FAILED(cc->GetNextPacketSize(&packet))) break;
      float mx = 0.0f;
      while (packet) {
        BYTE* data = nullptr; UINT32 frames = 0; DWORD flags = 0;
        if (FAILED(cc->GetBuffer(&data, &frames, &flags, nullptr, nullptr))) break;
        if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT) && data &&
            wf->wBitsPerSample == 32) {
          float* f = (float*)data;
          UINT32 cnt = frames * wf->nChannels;
          for (UINT32 i = 0; i < cnt; i++) {
            float a = f[i] < 0 ? -f[i] : f[i];
            if (a > mx) mx = a;
          }
        }
        cc->ReleaseBuffer(frames);
        if (FAILED(cc->GetNextPacketSize(&packet))) break;
      }
      if (mx > m_peak) m_peak = mx;   // UI 侧读取后自行衰减
      ::Sleep(20);
    }

  done:
    if (ac) ac->Stop();
    if (cc) cc->Release();
    if (ac) ac->Release();
    if (wf) CoTaskMemFree(wf);
    if (dev) dev->Release();
    if (en) en->Release();
    CoUninitialize();
  }

  std::wstring m_id;
  std::thread  m_thread;
  std::atomic<bool>  m_run{false};
  std::atomic<float> m_peak{0.0f};
};

static MicMonitor g_micMon;

static void UpdateMicMeter() {
  // ★ 必须同时满足"在麦克风页"和"测试已开启"两个条件
  if (!g_newUi || g_app.page != eui2::PAGE_MIC || !g_app.micTestOn) return;

  // 优先测试耳机麦克风（EPOS），没有就退回系统默认
  static std::wstring s_mid;
  static DWORD s_refresh = 0;
  if (s_mid.empty() || GetTickCount() - s_refresh > 5000) {
    s_mid = g_audio.PreferredCaptureId();
    if (s_mid.empty()) s_mid = g_audio.DefaultCaptureId();
    s_refresh = GetTickCount();
    bool wasRunning = g_micMon.Running();
    g_micMon.Start(s_mid);
    if (!wasRunning) {
      g_app.micDevName = g_audio.EndpointName(s_mid);
      Log(L"mic test device: [%ls]", g_app.micDevName.c_str());
    }
  }

  // 取峰值后清零：这样读到的永远是"最近 80ms 内的最大值"
  float p = g_micMon.TakePeak();
  float db = (p > 0.0f) ? 20.0f * log10f(p) : -60.0f;
  if (db < -60.0f) db = -60.0f;
  if (db > 0.0f)   db = 0.0f;

  // 快起慢落：直接跟原始峰值会让数字疯跳
  if (db > g_app.micPeakDb) g_app.micPeakDb = db;
  else g_app.micPeakDb = g_app.micPeakDb * 0.55f + db * 0.45f;

  DWORD now = GetTickCount();
  if (db >= g_app.micPeakHoldDb) { g_app.micPeakHoldDb = db; g_micHoldAt = now; }
  else if (now - g_micHoldAt > 2500) g_app.micPeakHoldDb = db;   // 2.5s 后开始回落

  if (db > -45.0f) g_micLoudAt = now;
  g_app.micActive = (g_micLoudAt != 0 && now - g_micLoudAt < 1200);

  // "完全没有信号"和"信号很弱"要分开：前者通常是麦克风杆抬起（=静音）或已拆下
  if (db > -70.0f) g_micSilentAt = now;
  g_app.micSilentLong = (g_micSilentAt == 0 || now - g_micSilentAt > 3000);

  if (g_hwnd) InvalidateRect(g_hwnd, nullptr, FALSE);
}

// ---------------- ANC 开关状态：dongle HID 只读监听 ----------------
// 背景：官方管道（rt=1）不转发 ANC 状态，但 USB 抓包证实 dongle 的 vendor HID
// 接口（InputReportByteLength=107，reportId=1）会在**每次按 ANC 键时**主动上报
// 107 字节状态报告：offset17 = 0/1 开关位，offset11 = 联动值（0x99/0x8c）。
// 这里以共享模式打开同一 HID 设备（不干扰官方服务），只读监听。
// 限制：报告只在按键时发送，所以程序启动后需要用户按一次 ANC 才能同步状态。
static std::atomic<bool> g_ancRun{false};
static std::thread       g_ancThread;

static std::wstring FindAncHidPath() {
  auto lowcopy = [](std::wstring s) {
    for (auto& c : s) if (c >= L'A' && c <= L'Z') c = (wchar_t)(c - L'A' + L'a');
    return s;
  };
  GUID hidGuid; HidD_GetHidGuid(&hidGuid);
  HDEVINFO di = SetupDiGetClassDevsW(&hidGuid, nullptr, nullptr,
                                     DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
  if (di == INVALID_HANDLE_VALUE) return L"";
  SP_DEVICE_INTERFACE_DATA ifd = {};
  ifd.cbSize = sizeof(ifd);
  std::wstring found, fallback;
  for (DWORD i = 0; SetupDiEnumDeviceInterfaces(di, nullptr, &hidGuid, i, &ifd); i++) {
    DWORD need = 0;
    SetupDiGetDeviceInterfaceDetailW(di, &ifd, nullptr, 0, &need, nullptr);
    std::vector<BYTE> buf(need);
    auto* det = (SP_DEVICE_INTERFACE_DETAIL_DATA_W*)buf.data();
    det->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
    if (!SetupDiGetDeviceInterfaceDetailW(di, &ifd, det, need, nullptr, nullptr)) continue;
    std::wstring low = lowcopy(det->DevicePath);
    if (low.find(L"vid_1395") == std::wstring::npos ||
        low.find(L"pid_0303") == std::wstring::npos) continue;
    // col01 = dongle 的 vendor HID 接口（107 字节状态报告）
    if (low.find(L"col01") != std::wstring::npos) { found = det->DevicePath; break; }
    if (fallback.empty()) fallback = det->DevicePath;
  }
  SetupDiDestroyDeviceInfoList(di);
  return found.empty() ? fallback : found;
}

static void AncMonitorLoop() {
  while (g_ancRun) {
    std::wstring path = FindAncHidPath();
    if (path.empty()) { Sleep(3000); continue; }
    // 异步句柄：阻塞读 107 字节输入报告
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    if (h == INVALID_HANDLE_VALUE) { Sleep(3000); continue; }

    HIDP_CAPS caps = {};
    {
      PHIDP_PREPARSED_DATA pp = nullptr;
      if (HidD_GetPreparsedData(h, &pp)) {
        HidP_GetCaps(pp, &caps);
        HidD_FreePreparsedData(pp);
      }
    }
    if (caps.InputReportByteLength != 107) {   // 不是 vendor 状态接口就重试
      CloseHandle(h);
      Sleep(3000);
      continue;
    }

    Log(L"anc: monitoring %ls", path.c_str());
    std::vector<BYTE> buf(caps.InputReportByteLength);
    OVERLAPPED ov = {}; ov.hEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    while (g_ancRun) {
      ZeroMemory(buf.data(), buf.size());
      DWORD got = 0;
      ResetEvent(ov.hEvent);
      if (!ReadFile(h, buf.data(), (DWORD)buf.size(), &got, &ov)) {
        if (GetLastError() != ERROR_IO_PENDING) break;
        if (WaitForSingleObject(ov.hEvent, 1000) != WAIT_OBJECT_0) {
          CancelIo(h); continue;               // 1 秒没数据就回头检查 g_ancRun
        }
        if (!GetOverlappedResult(h, &ov, &got, FALSE)) break;
      }
      if (got >= 18 && buf[0] == 1) {
        int newState = buf[17] ? 1 : 0;
        int newLevel = buf[11];
        if (newState != g_app.ancState || newLevel != g_app.ancLevel) {
          g_app.ancState = newState;
          g_app.ancLevel = newLevel;
          Log(L"anc: state=%d level=0x%02x", newState, newLevel);
          if (g_hwnd) InvalidateRect(g_hwnd, nullptr, FALSE);
        }
      }
    }
    CloseHandle(ov.hEvent);
    CloseHandle(h);
    if (g_ancRun) Sleep(2000);   // 设备拔出后重试
  }
}

static void StartAncMonitor() {
  if (g_ancRun) return;
  g_ancRun = true;
  g_ancThread = std::thread(AncMonitorLoop);
}

static void StopAncMonitor() {
  g_ancRun = false;
  if (g_ancThread.joinable()) g_ancThread.join();
}

// ---------------- 3) 按前台应用自动切预设 ----------------
// 配置文件 profiles.txt（与 exe 同目录）：
//   cs2.exe = ESPORT
//   obs64.exe = MUSIC
// 只在"前台进程变化"时才查表，空闲时零开销。
struct AppRule { std::wstring exe; std::wstring key; };
static std::vector<AppRule> g_appRules;
static std::wstring g_fgExe;          // 上一次的前台进程名（小写）

static std::wstring wlower(std::wstring s) {
  for (auto& c : s) if (c >= L'A' && c <= L'Z') c = (wchar_t)(c - L'A' + L'a');
  return s;
}

static std::wstring ExeDirPath(const wchar_t* leaf) {
  wchar_t path[MAX_PATH];
  GetModuleFileNameW(nullptr, path, MAX_PATH);
  wchar_t* slash = wcsrchr(path, L'\\');
  if (slash) wcscpy(slash + 1, leaf);
  return path;
}

static void LoadAppRules() {
  g_appRules.clear();
  std::wstring p = ExeDirPath(L"profiles.txt");
  FILE* f = _wfopen(p.c_str(), L"r, ccs=UTF-8");
  if (!f) {
    // 首次运行：写一份带示例的模板，用户自己改
    FILE* o = _wfopen(p.c_str(), L"w, ccs=UTF-8");
    if (o) {
      fwprintf(o, L"; 按前台应用自动切换 EQ 预设\n");
      fwprintf(o, L"; 格式: <进程名小写> = <预设 KEY>\n");
      fwprintf(o, L"; 可用 KEY: MUSIC / MOVIE / ESPORT / FLAT / CUSTOM\n");
      fwprintf(o, L";\n; cs2.exe = ESPORT\n; valorant.exe = ESPORT\n; obs64.exe = MUSIC\n");
      fclose(o);
      Log(L"profiles.txt template created");
    }
    return;
  }
  wchar_t line[256];
  while (fgetws(line, 256, f)) {
    if (line[0] == L';' || line[0] == L'#' || line[0] == L'\n' || line[0] == 0) continue;
    wchar_t* eq = wcschr(line, L'=');
    if (!eq) continue;
    *eq = 0;
    std::wstring exe = wlower(line);
    std::wstring key = eq + 1;
    auto trim = [](std::wstring& s) {
      while (!s.empty() && (s.back() == L' ' || s.back() == L'\t' ||
                            s.back() == L'\r' || s.back() == L'\n')) s.pop_back();
      size_t i = 0; while (i < s.size() && (s[i] == L' ' || s[i] == L'\t')) i++;
      s = s.substr(i);
    };
    trim(exe); trim(key);
    if (!exe.empty() && !key.empty()) g_appRules.push_back({ exe, key });
  }
  fclose(f);
  Log(L"profiles.txt loaded: %d rule(s)", (int)g_appRules.size());
}

// 把内存规则写回 profiles.txt（界面化编辑的持久化）
static void SaveAppRules() {
  std::wstring p = ExeDirPath(L"profiles.txt");
  FILE* f = _wfopen(p.c_str(), L"w, ccs=UTF-8");
  if (!f) { Log(L"profiles.txt save failed"); return; }
  fwprintf(f, L"; 按前台应用自动切换 EQ 预设\n");
  fwprintf(f, L"; 格式: <进程名小写> = <预设 KEY>\n");
  for (const auto& r : g_appRules)
    fwprintf(f, L"%ls = %ls\n", r.exe.c_str(), r.key.c_str());
  fclose(f);
  Log(L"profiles.txt saved: %d rule(s)", (int)g_appRules.size());
}

// 内存规则 -> UI 显示副本（eui2::AppState 只依赖标准库，不反向包含 main 的类型）
static void SyncAppRulesView() {
  g_app.appRulesView.clear();
  for (const auto& r : g_appRules)
    g_app.appRulesView.push_back({ r.exe, r.key });
}

// 混音器页：枚举会话 -> UI 行（保留已有的静音记忆）
static void RefreshMix() {
  std::vector<eui2::AppState::MixRow> old = g_app.mix;
  g_app.mix.clear();
  for (const auto& a : g_audio.EnumAppVolumes()) {
    eui2::AppState::MixRow r;
    r.name = a.exe.empty() ? (L"pid " + std::to_wstring(a.pid)) : a.exe;
    r.vol  = a.vol < 0 ? 100 : a.vol;
    r.prev = r.vol;
    r.muted = a.muted;
    for (const auto& o : old)
      if (o.name == r.name) { r.prev = o.prev; if (o.muted) r.muted = true; break; }
    g_app.mix.push_back(r);
  }
}

// 按显示名（进程名）反查会话 pid
static DWORD MixerPidByName(const std::wstring& name) {
  for (const auto& a : g_audio.EnumAppVolumes()) {
    std::wstring n = a.exe.empty() ? (L"pid " + std::to_wstring(a.pid)) : a.exe;
    if (n == name) return a.pid;
  }
  return 0;
}

static std::wstring ForegroundExe() {
  HWND h = GetForegroundWindow();
  if (!h) return L"";
  DWORD pid = 0;
  GetWindowThreadProcessId(h, &pid);
  if (!pid || pid == GetCurrentProcessId()) return L"";
  HANDLE pr = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (!pr) return L"";
  wchar_t buf[MAX_PATH] = {0};
  DWORD n = MAX_PATH;
  std::wstring out;
  if (QueryFullProcessImageNameW(pr, 0, buf, &n)) {
    const wchar_t* s2 = wcsrchr(buf, L'\\');
    out = wlower(s2 ? s2 + 1 : buf);
  }
  CloseHandle(pr);
  return out;
}

static void CheckAppPreset() {
  if (!g_appPresetOn || g_appRules.empty()) return;
  std::wstring exe = ForegroundExe();
  if (exe == g_fgExe) return;          // 前台没变，什么都不做
  g_fgExe = exe;
  if (exe.empty()) return;

  for (const auto& r : g_appRules) {
    if (r.exe != exe) continue;
    for (size_t i = 0; i < g_app.presets.size(); i++) {
      if (g_app.presets[i].key != r.key) continue;
      if (g_app.currentPreset == (int)i) return;   // 已经是这个预设
      g_app.presets[i].ApplyTo(g_app.sound);
      g_app.currentPreset = (int)i;
      g_panel.sound = g_app.sound;
      ApplySoundToDevice2();
      Log(L"profile: %ls -> preset %ls", exe.c_str(), r.key.c_str());
      if (g_hwnd) InvalidateRect(g_hwnd, nullptr, FALSE);
      return;
    }
    Log(L"profile: %ls matched but preset [%ls] not found", exe.c_str(), r.key.c_str());
    return;
  }
}


static void ShowTrayMenu();
static void ApplySoundToDevice2();

// 托盘菜单命令统一入口（菜单用 TPM_RETURNCMD，不走 WM_COMMAND）
static void HandleMenuCmd(UINT id) {
  if (id == IDM_EXIT) { DestroyWindow(g_hwnd); return; }
  if (id == IDM_SHOW) {
    g_winVisible = !g_winVisible;
    ShowWindow(g_hwnd, g_winVisible ? SW_SHOW : SW_HIDE);
    if (g_winVisible) SetForegroundWindow(g_hwnd);
    UpdateTray();
    return;
  }
  if (id == IDM_AUTOSW) { g_autoSwitch = !g_autoSwitch; InvalidateRect(g_hwnd, nullptr, FALSE); return; }
  if (id == IDM_BATT_ALERT)  { g_battAlertOn = !g_battAlertOn; Log(L"tray: battery alert -> %d", (int)g_battAlertOn); return; }
  if (id == IDM_AUTO_RESTORE){ g_restoreOn = !g_restoreOn;   Log(L"tray: auto restore -> %d", (int)g_restoreOn); return; }
  if (id == IDM_APP_PRESET)  { g_appPresetOn = !g_appPresetOn; Log(L"tray: app preset -> %d", (int)g_appPresetOn);
                               g_fgExe.clear(); return; }
  if (id == IDM_RELOAD_RULES){ LoadAppRules(); return; }
  if (id == IDM_SETTINGS) {
    g_app.page = eui2::PAGE_SETTINGS;
    g_app.contentOffset = 0;
    g_winVisible = true;
    ShowWindow(g_hwnd, SW_SHOW);
    SetForegroundWindow(g_hwnd);
    InvalidateRect(g_hwnd, nullptr, FALSE);
    return;
  }

  // 预设
  if (id >= IDM_PRESET_BASE && id < IDM_PRESET_BASE + 32) {
    int idx = (int)(id - IDM_PRESET_BASE);
    if (idx >= 0 && idx < (int)g_app.presets.size()) {
      g_app.presets[idx].ApplyTo(g_app.sound);
      g_app.currentPreset = idx;
      g_panel.sound = g_app.sound;
      ApplySoundToDevice2();
      Log(L"tray: preset applied -> %ls", g_app.presets[idx].key.c_str());
      InvalidateRect(g_hwnd, nullptr, FALSE);
    }
    return;
  }

  // 音量
  if (id == IDM_MUTE_SPK) {
    std::wstring sid = g_audio.DefaultId(eConsole);
    if (!sid.empty()) {
      bool m = g_audio.IsMuted(sid);
      g_audio.SetMuted(sid, !m);
      g_app.speakerMuted = !m;
      InvalidateRect(g_hwnd, nullptr, FALSE);
    }
    return;
  }
  if (id == IDM_VOL_SPK_50 || id == IDM_VOL_SPK_100) {
    std::wstring sid = g_audio.DefaultId(eConsole);
    if (!sid.empty()) {
      float pct = (id == IDM_VOL_SPK_50) ? 0.5f : 1.0f;
      g_audio.SetVolumePct(sid, pct, false);
      g_app.speakerVol = (int)(pct * 100);
      InvalidateRect(g_hwnd, nullptr, FALSE);
    }
    return;
  }

  if (id >= IDM_PAGE_BASE && id < IDM_PAGE_BASE + 16) {
    g_app.page = eui2::Navs()[id - IDM_PAGE_BASE].page;
    g_app.scroll.offset = 0;
    g_app.contentOffset = 0;
    if (!g_winVisible) { g_winVisible = true; ShowWindow(g_hwnd, SW_SHOW); }
    InvalidateRect(g_hwnd, nullptr, FALSE);
    return;
  }
}

static void ShowTrayMenu() {
  POINT pt; GetCursorPos(&pt);
  // ★ 菜单文案跟随界面语言，不再中英双语
  const bool en = g_app.english;
  HMENU m = CreatePopupMenu();

  AppendMenuW(m, MF_STRING | (g_winVisible ? MF_CHECKED : 0), IDM_SHOW,
              en ? L"Show window" : L"显示窗口");
  // ★ 各项行为开关已挪到「设置」页，托盘只保留最常用的入口
  AppendMenuW(m, MF_STRING, IDM_SETTINGS, en ? L"Settings…" : L"设置…");
  AppendMenuW(m, MF_SEPARATOR, 0, nullptr);

  // ★ 预设快捷入口（替换原来的"页面"子菜单）
  {
    HMENU presetMenu = CreatePopupMenu();
    for (int i = 0; i < (int)g_app.presets.size() && i < 32; i++) {
      const eposfx::Preset& p = g_app.presets[i];
      AppendMenuW(presetMenu,
                  MF_STRING | (i == g_app.currentPreset ? MF_CHECKED : 0),
                  IDM_PRESET_BASE + i,
                  en ? p.en.c_str() : p.zh.c_str());
    }
    if (g_app.presets.empty())
      AppendMenuW(presetMenu, MF_STRING | MF_GRAYED, 0, en ? L"(none)" : L"（无）");
    AppendMenuW(presetMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(presetMenu, MF_STRING, IDM_RELOAD_RULES,
                en ? L"Reload profiles.txt" : L"重新加载 profiles.txt");
    AppendMenuW(m, MF_POPUP, (UINT_PTR)presetMenu, en ? L"Presets" : L"预设");
  }

  AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
  // 音量快捷
  {
    HMENU volMenu = CreatePopupMenu();
    AppendMenuW(volMenu, MF_STRING, IDM_MUTE_SPK, en ? L"Mute speakers" : L"扬声器静音");
    AppendMenuW(volMenu, MF_STRING, IDM_VOL_SPK_50, L"50%");
    AppendMenuW(volMenu, MF_STRING, IDM_VOL_SPK_100, L"100%");
    AppendMenuW(m, MF_POPUP, (UINT_PTR)volMenu, en ? L"Volume" : L"音量");
  }

  AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(m, MF_STRING, IDM_EXIT, en ? L"Exit" : L"退出");
  SetForegroundWindow(g_hwnd);
  // 用 TPM_RETURNCMD 直接拿选中项：避免依赖 WM_COMMAND（托盘菜单容易丢消息）
  UINT cmd = (UINT)TrackPopupMenu(m, TPM_RIGHTBUTTON | TPM_RETURNCMD, pt.x, pt.y, 0, g_hwnd, nullptr);
  DestroyMenu(m);
  if (cmd) HandleMenuCmd(cmd);
}

// 命中测试
// ---- 音效页交互 ----
static void FxOnClick(int id, int band, POINT p) {
  switch (id) {
    case epospanel::ID_LANG:
      g_panel.english = !g_panel.english;
      break;

    case epospanel::ID_SURROUND:
      g_panel.sound.surround3D = !g_panel.sound.surround3D;
      ApplySoundToDevice();
      break;

    case epospanel::ID_VOICE:
      g_panel.sound.voiceEnhance = !g_panel.sound.voiceEnhance;
      ApplySoundToDevice();
      break;

    case epospanel::ID_DIRECT:
      g_panel.sound.directSound = !g_panel.sound.directSound;
      ApplySoundToDevice();
      break;

    case epospanel::ID_SIDETONE: {
      double pct = epospanel::PctFromPoint(g_fxLayout.sideToneTrack, p);
      g_panel.sound.sideToneLevel = pct * 20.0;
      break;
    }
    case epospanel::ID_REVERB: {
      double pct = epospanel::PctFromPoint(g_fxLayout.reverbTrack, p);
      g_panel.sound.reverb = pct * 100.0;
      ApplySoundToDevice();
      break;
    }
    case epospanel::ID_GATE: {
      double pct = epospanel::PctFromPoint(g_fxLayout.gateTrack, p);
      g_panel.sound.noiseGate = pct * 100.0;
      ApplySoundToDevice();
      break;
    }

    case epospanel::ID_SAVE: {
      // 用当前曲线生成一个自定义预设，追加到存储
      int n = (int)eposfx::LoadCustomPresets().size() + 1;
      eposfx::Preset p;
      p.zh = L"自定义 " + std::to_wstring(n);
      p.en = L"Custom " + std::to_wstring(n);
      p.key = L"CUSTOM";
      for (int i = 0; i < 9; i++) {
        p.speakerEq[i] = g_panel.sound.speakerEq[i];
        p.micEq[i]     = g_panel.sound.micEq[i];
      }
      p.reverb = g_panel.sound.reverb;
      p.noiseGate = g_panel.sound.noiseGate;
      p.surround3D = g_panel.sound.surround3D;
      p.voiceEnhance = g_panel.sound.voiceEnhance;
      p.builtin = false;

      std::vector<eposfx::Preset> list = eposfx::LoadCustomPresets();
      list.push_back(p);
      eposfx::SaveCustomPresets(list);
      Log(L"sound: saved custom preset #%d", n);
      break;
    }

    case epospanel::ID_CAPTURE: {
      // 从官方 Gaming Suite 日志里抓它实际下发的 9 段曲线
      eposfx::SoundState cap;
      if (eposfx::CaptureFromLog(kGsLog, cap)) {
        for (int i = 0; i < 9; i++) {
          g_panel.sound.speakerEq[i] = cap.speakerEq[i];
          g_panel.sound.micEq[i]     = cap.micEq[i];
        }
        if (cap.reverb >= 0) g_panel.sound.reverb = cap.reverb;
        if (cap.noiseGate >= 0) g_panel.sound.noiseGate = cap.noiseGate;
        g_panel.currentPreset = -1;
        ApplySoundToDevice();
        Log(L"sound: captured curve from Gaming Suite log");
      } else {
        Log(L"sound: capture failed (no apoParams in log)");
        MessageBoxW(nullptr, L"未能从 Gaming Suite 日志中找到曲线记录。\n\n"
                            L"请先打开官方 Gaming Suite 并切换一次预设。",
                    L"EPOS Switch", MB_OK | MB_ICONINFORMATION);
      }
      break;
    }

    default:
      if (id >= epospanel::ID_PRESET_BASE && id < epospanel::ID_PRESET_BASE + 32) {
        int idx = id - epospanel::ID_PRESET_BASE;
        if (idx < (int)g_panel.presets.size()) {
          g_panel.presets[idx].ApplyTo(g_panel.sound);
          g_panel.currentPreset = idx;
          ApplySoundToDevice();
          Log(L"sound: preset applied -> %ls", g_panel.presets[idx].key.c_str());
        }
      }
      break;
  }
}

static int HitTest(POINT pt) {
  // 音效页：先判标签栏
  if (g_showFx) {
    for (int i = 0; i < 2; i++) {
      RectF r = TabRect(i);
      if (pt.x >= r.X && pt.x <= r.X + r.Width && pt.y >= r.Y && pt.y <= r.Y + r.Height)
        return 500 + i;      // 500=状态页 501=音效页
    }
    int band = -1;
    int id = epospanel::HitTest(g_fxLayout, pt, &band);
    if (id != epospanel::ID_NONE) { g_fxBand = band; return id; }
    return -1;
  }
  // 状态页：标签栏
  for (int i = 0; i < 2; i++) {
    RectF r = TabRect(i);
    if (pt.x >= r.X && pt.x <= r.X + r.Width && pt.y >= r.Y && pt.y <= r.Y + r.Height)
      return 500 + i;
  }
  RectF cr = CloseRect();
  if (pt.x >= cr.X && pt.x <= cr.X + cr.Width && pt.y >= cr.Y && pt.y <= cr.Y + cr.Height)
    return 100;
  RectF ar = AutoRect();
  if (pt.x >= ar.X && pt.x <= ar.X + ar.Width && pt.y >= ar.Y && pt.y <= ar.Y + ar.Height)
    return 200;
  std::vector<const Endpoint*> list;
  BuildSwitchList(list);
  for (int i = 0; i < (int)list.size(); i++) {
    int top = RowTop(i);
    if (pt.y >= top && pt.y <= top + S(56) && pt.x >= S(16) && pt.x <= SW - S(16))
      return i;
  }
  return -1;
}

static void SyncSoundFromDevice();

// ---------------- 新版界面交互 ----------------
// 滑块位置 -> 百分比（布局需与 eui2::DrawSound 一致）
static double SliderPct(int kind, POINT p) {
  eui2::Layout L = eui2::ComputeLayout(g_app);
  double rel = (double)(p.x - L.trackX) / (double)L.trackW;
  if (rel < 0) rel = 0;
  if (rel > 1) rel = 1;
  (void)kind;
  return rel;
}

static void ApplySoundToDevice2() {
  if (!g_pipe.Ready()) { Log(L"sound: pipe not ready"); return; }
  epospipe::Client::ApoParams ap;
  ap.speakerEq9 = g_app.sound.speakerEq;
  ap.micEq9     = g_app.sound.micEq;
  ap.reverbLevel    = g_app.sound.reverb;
  ap.noiseGateLevel = g_app.sound.noiseGate;
  ap.directSound    = g_app.sound.directSound ? 0 : -1;
  ap.micHpfEnabled  = -1;
  ap.micFrcEnabled  = -1;
  ap.micFrcPreset   = -1;
  ap.micNrEnabled   = g_app.sound.voiceEnhance ? -1 : 0;
  ap.micNrPreset    = -1;
  ap.micMetersEnabled = -1;
  ap.autoGainAdjust = -1;
  g_pipe.Request(epospipe::Client::BuildApoFull(ap));
  // 3D 声场（rt=10）与侧音（rt=6）走独立请求，不在 apoParams 里
  g_pipe.Request(epospipe::Client::BuildSurround3D(g_app.sound.surround3D));
  g_pipe.Request(epospipe::Client::BuildSideTone((int)(g_app.sound.sideToneLevel + 0.5)));
  g_panel.dirty = false;
}

static void NewUiOnClick(const eui2::HitInfo& h, POINT p) {
  const eui2::Metrics& m = eui2::M();
  const int dd = m.dpi;
  auto S = [dd](int v) { return MulDiv(v, dd, 96); };
  int navW = S(m.navW);
  int pad = S(m.pad);
  int x = navW + pad;
  int w = m.w - navW - pad * 2;

  // 语言下拉展开时，点其它任何地方都先收起（菜单的常规行为）
  if (g_app.langOpen &&
      !(h.id == eui2::CID_LANG_POP ||
        (h.id == eui2::CID_ACTION && h.action == eui2::ACT_LANG))) {
    g_app.langOpen = false;
    InvalidateRect(g_hwnd, nullptr, FALSE);
  }
  // 应用规则的预设下拉同理
  if (g_app.appPickOpen &&
      !(h.id == eui2::CID_APP_PICK ||
        (h.id == eui2::CID_ACTION && h.action == eui2::ACT_APP_PICKOPEN))) {
    g_app.appPickOpen = false;
    InvalidateRect(g_hwnd, nullptr, FALSE);
  }

  switch (h.id) {
  case eui2::CID_PAGE_BASE:
  case eui2::CID_PAGE_BASE + 1:
  case eui2::CID_PAGE_BASE + 2:
  case eui2::CID_PAGE_BASE + 3:
  case eui2::CID_PAGE_BASE + 4:
  case eui2::CID_PAGE_BASE + 5:
  case eui2::CID_PAGE_BASE + 6:
  case eui2::CID_PAGE_BASE + 7:
    g_app.page = eui2::Navs()[h.index].page;
    g_app.scroll.offset = 0;
    g_app.contentOffset = 0;
    // 进设备页立即刷新每行音量（否则头 2 秒没有音量条）
    if (g_app.page == eui2::PAGE_DEVICES) RefreshDevVolumes();
    if (g_app.page == eui2::PAGE_MIXER)   RefreshMix();
    break;

  // ---- 语言下拉项 / 设置页语言列表项 ----
  case eui2::CID_LANG_POP:
  case eui2::CID_SET_LANG_ROW:
    g_app.langOpen = false;
    SetLanguage(h.index == 1);
    break;

  // ---- 应用规则：选中预设 -> 落规则 ----
  case eui2::CID_APP_PICK:
    g_app.appPickOpen = false;
    if (h.index >= 0 && h.index < (int)g_app.presets.size() &&
        !g_app.appAddText.empty()) {
      AppRule r;
      r.exe = wlower(g_app.appAddText);
      r.key = g_app.presets[h.index].key;
      bool dup = false;
      for (auto& e : g_appRules) if (e.exe == r.exe) { e.key = r.key; dup = true; break; }
      if (!dup) g_appRules.push_back(r);
      SaveAppRules();
      SyncAppRulesView();
      Log(L"profiles: rule [%ls = %ls]", r.exe.c_str(), r.key.c_str());
    }
    g_app.appAddText.clear();
    break;

  // ---- 全局快捷键：录制 / 清除 ----
  case eui2::CID_HK_EDIT:
    HkBeginCapture(h.index);
    break;

  case eui2::CID_HK_CLEAR:
    if (h.index >= 0 && h.index < 3) {
      g_hkSlots[h.index].mods = 0;
      g_hkSlots[h.index].vk = 0;
      g_hkCapture = -1;
      RegisterAllHotkeys();
      SaveSettings(CurrentSettings());
      HkSyncView();
      Log(L"hotkeys: slot %d cleared", h.index);
    }
    break;

  // ---- 麦克风输入测试开关 ----
  case eui2::CID_MIC_TEST:
    g_app.micTestOn = !g_app.micTestOn;
    if (!g_app.micTestOn) {
      g_micMon.Stop();
      KillTimer(g_hwnd, 2);
      g_app.micPeakDb = -60.0f;
      g_app.micPeakHoldDb = -60.0f;
      g_app.micActive = false;
      g_app.micSilentLong = false;
    } else {
      SetTimer(g_hwnd, 2, 80, nullptr);
    }
    Log(L"mic: test %ls", g_app.micTestOn ? L"ON" : L"OFF");
    break;

  case eui2::CID_DEV_SELECT:
    // 在有多个设备时循环切换；只有一个则不动作
    if ((int)g_app.devices.size() > 1) {
      g_app.selectedDevice = (g_app.selectedDevice + 1) % (int)g_app.devices.size();
      SyncSoundFromDevice();
    }
    break;

  case eui2::CID_PRESET_BASE: {
    if (h.index < 0 || h.index >= (int)g_app.presets.size()) break;
    g_app.presets[h.index].ApplyTo(g_app.sound);
    g_app.currentPreset = h.index;
    g_panel.sound = g_app.sound;     // 同步旧面板状态
    ApplySoundToDevice2();
    Log(L"sound: preset applied -> %ls", g_app.presets[h.index].key.c_str());
    break;
  }

  case eui2::CID_EQ_BASE:
    // 拖动由 WM_MOUSEMOVE 处理，这里只标记
    g_eqDrag = h.index;
    break;

  case eui2::CID_SEG_BASE:
  case eui2::CID_SEG_BASE + 1:
  case eui2::CID_SEG_BASE + 2:
  case eui2::CID_SEG_BASE + 3: {
    int kind = h.id - eui2::CID_SEG_BASE;
    switch (kind) {
    case eui2::SEG_SURROUND:
      g_app.sound.surround3D = (h.index == 1);
      break;
    case eui2::SEG_VOICE:
      g_app.sound.voiceEnhance = (h.index > 0);
      break;
    case eui2::SEG_DIRECT:
      g_app.sound.directSound = (h.index == 1);
      break;
    case eui2::SEG_NR: {
      // 降噪档位是耳机端功能，需要 SECOM 协议（未实现）
      // 先在 UI 上记录选择，实际下发留给后续实现
      int lvl = h.index;   // 0=关 1=低 2=中 3=高
      Log(L"sound: noise reduction level requested = %d (not yet supported)", lvl);
      MessageBoxW(nullptr,
          g_app.english
            ? L"Noise reduction is a headset-side (DSP) feature.\n\n"
              L"It can be read from the device, but setting it requires EPOS's private "
              L"SECOM protocol, which is not implemented yet."
            : L"降噪是耳机端（DSP）功能。\n\n"
              L"当前可以读取状态，但设置需要 EPOS 私有的 SECOM 协议，尚未实现。",
          L"EPOS", MB_OK | MB_ICONINFORMATION);
      break;
    }
    }
    g_panel.sound = g_app.sound;
    ApplySoundToDevice2();
    break;
  }

  case eui2::CID_SLIDER_BASE:
  case eui2::CID_SLIDER_BASE + 1:
  case eui2::CID_SLIDER_BASE + 2: {
    // 计算滑块百分比并应用
    int kind = h.id - eui2::CID_SLIDER_BASE;
    double pct = SliderPct(kind, p);
    if (kind == 0)      g_app.sound.reverb = pct * 100.0;
    else if (kind == 1) g_app.sound.noiseGate = pct * 100.0;
    else                g_app.sound.sideToneLevel = pct * 20.0;
    g_panel.sound = g_app.sound;
    ApplySoundToDevice2();
    break;
  }

  case eui2::CID_ACTION:
    switch (h.action) {
    // ---- 语言下拉（左下角）：点一下展开/收起，真正的选择在 CID_LANG_POP ----
    case eui2::ACT_LANG: {
      g_app.langOpen = !g_app.langOpen;
      break;
    }
    // ---- 保存为自定义预设 ----
    case eui2::ACT_SAVE: {
      int n = (int)eposfx::LoadCustomPresets().size() + 1;
      eposfx::Preset np;
      np.zh  = L"自定义 " + std::to_wstring(n);
      np.en  = L"Custom " + std::to_wstring(n);
      np.key = L"CUSTOM";
      for (int i = 0; i < 9; i++) {
        np.speakerEq[i] = g_app.sound.speakerEq[i];
        np.micEq[i]     = g_app.sound.micEq[i];
      }
      np.reverb = g_app.sound.reverb;
      np.noiseGate = g_app.sound.noiseGate;
      np.surround3D = g_app.sound.surround3D;
      np.voiceEnhance = g_app.sound.voiceEnhance;
      np.builtin = false;
      std::vector<eposfx::Preset> list = eposfx::LoadCustomPresets();
      list.push_back(np);
      eposfx::SaveCustomPresets(list);
      g_presetReloadAt = 0;            // 立即重读
      g_app.presets = eposfx::AllPresets();
      g_presetReloadAt = GetTickCount();
      Log(L"sound: saved custom preset #%d", n);
      break;
    }
    // ---- 从官方 Gaming Suite 捕获当前曲线 ----
    case eui2::ACT_CAPTURE: {
      eposfx::SoundState cap;
      if (eposfx::CaptureFromLog(kGsLog, cap)) {
        for (int i = 0; i < 9; i++) {
          g_app.sound.speakerEq[i] = cap.speakerEq[i];
          g_app.sound.micEq[i]     = cap.micEq[i];
        }
        if (cap.reverb >= 0) g_app.sound.reverb = cap.reverb;
        if (cap.noiseGate >= 0) g_app.sound.noiseGate = cap.noiseGate;
        g_app.currentPreset = -1;
        g_panel.sound = g_app.sound;
        ApplySoundToDevice2();
        Log(L"sound: captured curve from Gaming Suite log");
      } else {
        MessageBoxW(nullptr,
            g_app.english
              ? L"No curve found in the Gaming Suite log.\n\nOpen the official Gaming Suite "
                L"and switch a preset once, then try again."
              : L"未在 Gaming Suite 日志中找到曲线记录。\n\n"
                L"请先打开官方 Gaming Suite 并切换一次预设，然后重试。",
            L"EPOS", MB_OK | MB_ICONINFORMATION);
      }
      break;
    }
    // ---- 输出设备页：点击卡片切换默认输出 ----
    case eui2::ACT_OUTPUT: {
      // 按当前标签页决定切播放还是录音设备
      // ★ index 是"可见行序号"，必须经 DevRows 换算（物理在前、虚拟在后）
      bool isCapture = (g_app.devTab == 1);
      std::vector<Endpoint>* list = isCapture ? g_app.captureEndpoints : g_app.endpoints;
      if (list) {
        eui2::Layout L = eui2::ComputeLayout(g_app);
        eui2::DevRows dr = eui2::DevLayout(g_app, L, isCapture);
        if (h.index >= 0 && h.index < (int)dr.idx.size()) {
          const Endpoint& ep = (*list)[dr.idx[h.index]];
          if (g_audio.SetDefault(ep.id)) {
            if (isCapture) {
              g_app.currentInputId = ep.id;
              Log(L"input: switched to %ls", ep.name.c_str());
            } else {
              g_autoSwitch = false;     // 手动切换 -> 暂停自动
              g_app.setAutoSwitch = false;
              SaveSettings(CurrentSettings());
              g_app.currentOutputId = ep.id;
              Log(L"output: switched to %ls", ep.name.c_str());
            }
            RefreshDevVolumes();
          }
        }
      }
      break;
    }

    // ---- 混音器：单应用静音/恢复 ----
    case eui2::ACT_MIX_MUTE: {
      if (h.index >= 0 && h.index < (int)g_app.mix.size()) {
        eui2::AppState::MixRow& r = g_app.mix[h.index];
        DWORD pid = MixerPidByName(r.name);
        if (pid) {
          bool nv = !r.muted;
          if (g_audio.SetAppMuteByPid(pid, nv)) {
            if (nv) r.prev = r.vol > 0 ? r.vol : 50;
            r.muted = nv;
            Log(L"mixer: %ls %ls", r.name.c_str(), nv ? L"muted" : L"unmuted");
          }
        }
      }
      break;
    }

    // ---- 应用规则：删除 ----
    case eui2::ACT_APP_DEL:
      if (h.index >= 0 && h.index < (int)g_appRules.size()) {
        Log(L"profiles: removed [%ls = %ls]",
            g_appRules[h.index].exe.c_str(), g_appRules[h.index].key.c_str());
        g_appRules.erase(g_appRules.begin() + h.index);
        SaveAppRules();
        SyncAppRulesView();
      }
      break;

    // ---- 应用规则：捕获前台程序 ----
    case eui2::ACT_APP_CAPTURE: {
      std::wstring exe = ForegroundExe();
      if (exe.empty()) {
        MessageBoxW(g_hwnd,
            g_app.english ? L"Could not identify the foreground window."
                          : L"没能识别前台窗口，换个程序再试。",
            L"EPOS", MB_OK | MB_ICONINFORMATION);
      } else {
        g_app.appAddText = exe;
        g_app.appAddMode = false;
        g_app.appPickOpen = false;
        Log(L"profiles: staged [%ls]", exe.c_str());
      }
      break;
    }

    // ---- 应用规则：进入手动输入（index=2 表示"点 chip 重新输入"）----
    case eui2::ACT_APP_MANUAL:
      g_app.appAddMode = true;
      g_app.appPickOpen = false;
      if (h.index != 2) g_app.appAddText.clear();   // 全新输入；点 chip 则保留
      if (g_hwnd) SetFocus(g_hwnd);
      break;

    // ---- 应用规则：展开/收起预设下拉 ----
    case eui2::ACT_APP_PICKOPEN:
      g_app.appPickOpen = !g_app.appPickOpen;
      break;

    // ---- 设备页：单端点静音 ----
    case eui2::ACT_DEV_MUTE: {
      bool isCapture = (g_app.devTab == 1);
      std::vector<Endpoint>* list = isCapture ? g_app.captureEndpoints : g_app.endpoints;
      if (list && h.index >= 0) {
        eui2::Layout L = eui2::ComputeLayout(g_app);
        eui2::DevRows dr = eui2::DevLayout(g_app, L, isCapture);
        if (h.index < (int)dr.idx.size() && h.index < (int)g_app.devMuted.size()) {
          const Endpoint& ep = (*list)[dr.idx[h.index]];
          bool nv = !g_app.devMuted[h.index];
          if (g_audio.SetMuted(ep.id, nv)) {
            g_app.devMuted[h.index] = nv;
            Log(L"volume: [%ls] mute -> %d", ep.name.c_str(), (int)nv);
          }
        }
      }
      break;
    }

    case eui2::ACT_DEV_TAB:
      if (g_app.devTab != h.index) {
        g_app.devTab = h.index;
        g_app.contentOffset = 0;
        RefreshDevVolumes();          // 换 tab 后行集合完全不同，立即刷新
        Log(L"devices: tab -> %ls", h.index == 1 ? L"recording" : L"playback");
      }
      break;

    case eui2::ACT_SET_LANG:
      SetLanguage(h.index == 1);
      Log(L"settings: language -> %ls", g_app.english ? L"en" : L"zh");
      break;

    case eui2::ACT_SET_TOGGLE: {
      bool* fields[7] = { &g_app.setAutostart, &g_app.setCloseToTray, &g_app.setAutoSwitch,
                          &g_app.setAutoRestore, &g_app.setBattAlert, &g_app.setAppPreset,
                          &g_app.setHotkeys };
      if (h.index >= 0 && h.index < 7) {
        bool nv = !*fields[h.index];
        *fields[h.index] = nv;
        switch (h.index) {
          case 0: {   // 开机启动
            bool ok = SetAutostart(nv);
            g_app.setAutostart = ok ? nv : AutostartEnabled();
            Log(L"settings: autostart -> %d (reg ok=%d)", (int)g_app.setAutostart, (int)ok);
            break;
          }
          case 2: g_autoSwitch   = nv; break;
          case 3: g_restoreOn    = nv; break;
          case 4: g_battAlertOn  = nv; break;
          case 5: g_appPresetOn  = nv; g_fgExe.clear(); break;
          case 6: {   // 全局快捷键总开关：关掉立刻把热键还给系统
            g_hkEnabled = nv;
            g_hkCapture = -1;
            RegisterAllHotkeys();
            HkSyncView();
            HkSay(g_app.english
                    ? (nv ? L"Hotkeys enabled" : L"All hotkeys released")
                    : (nv ? L"已启用全局快捷键" : L"已停用全部全局快捷键"));
            break;
          }
          default: break;
        }
        SaveSettings(CurrentSettings());
        Log(L"settings: toggle #%d -> %d", h.index, (int)nv);
      }
      break;
    }
    // ---- 静音 ----
    case eui2::ACT_MUTE_SPK: {
      std::wstring sid = g_audio.DefaultId(eConsole);
      if (!sid.empty()) {
        bool m = g_audio.IsMuted(sid);
        g_audio.SetMuted(sid, !m);
        g_app.speakerMuted = !m;
        Log(L"volume: speaker mute -> %d", (int)!m);
      }
      break;
    }
    case eui2::ACT_MUTE_MIC: {
      std::wstring mid = g_audio.DefaultCaptureId();
      if (!mid.empty()) {
        bool m = g_audio.IsMuted(mid);
        g_audio.SetMuted(mid, !m);
        g_app.micMuted = !m;
        Log(L"volume: mic mute -> %d", (int)!m);
      }
      break;
    }
    // ---- 麦克风处理开关（通过 APO 下发，与官方软件同一路径）----
    case eui2::ACT_MIC_PRESET: {
      const eui2::MicPreset* ps = eui2::MicPresets();
      int n = eui2::MicPresetCount();
      if (h.index >= 0 && h.index < n) {
        for (int i = 0; i < 9; i++) g_app.sound.micEq[i] = ps[h.index].eq[i];
        g_app.micPresetIdx = h.index;
        g_panel.sound = g_app.sound;
        ApplySoundToDevice2();
        Log(L"mic: preset -> %ls", ps[h.index].zh);
      }
      break;
    }

    case eui2::ACT_MIC_NR:
    case eui2::ACT_MIC_HPF:
    case eui2::ACT_MIC_FRC:
    case eui2::ACT_MIC_METERS: {
      if (!g_pipe.Ready()) {
        MessageBoxW(g_hwnd,
            g_app.english ? L"Service not connected.\nClose the official Gaming Suite first."
                          : L"服务未连接。\n请先关闭官方 Gaming Suite。",
            L"EPOS", MB_OK | MB_ICONINFORMATION);
        break;
      }
      epospipe::Client::ApoParams ap;
      // 只在被点的那个字段上写 0/1，其余保持 null（不改动）
      if (h.action == eui2::ACT_MIC_NR)     ap.micNrEnabled    = h.index;
      if (h.action == eui2::ACT_MIC_HPF)    ap.micHpfEnabled   = h.index;
      if (h.action == eui2::ACT_MIC_FRC)    ap.micFrcEnabled   = h.index;
      if (h.action == eui2::ACT_MIC_METERS) ap.micMetersEnabled = h.index;
      g_pipe.Request(epospipe::Client::BuildApoFull(ap));
      // 耳机不回读这几项，用本地记录值显示（否则界面会"点了开又跳回关"）
      {
        int slot = -1;
        if (h.action == eui2::ACT_MIC_NR)     slot = 0;
        if (h.action == eui2::ACT_MIC_HPF)    slot = 1;
        if (h.action == eui2::ACT_MIC_FRC)    slot = 2;
        if (h.action == eui2::ACT_MIC_METERS) slot = 3;
        if (slot >= 0) g_app.micOverride[slot] = h.index;
      }
      Log(L"mic: action=%d set to %d", h.action, h.index);
      break;
    }
    default:
      break;
    }
    break;
  }
}

// 把设备状态同步到音效面板（首次读到设备时用）
static void SyncSoundFromDevice() {
  if (g_app.selectedDevice < 0 || g_app.selectedDevice >= (int)g_app.devices.size()) return;
  const eposdev::DeviceInfo& d = g_app.devices[g_app.selectedDevice];
  for (int i = 0; i < 9; i++) {
    g_app.sound.speakerEq[i] = d.speakerEq[i];
    g_app.sound.micEq[i]     = d.micEq[i];
  }
  g_app.sound.reverb      = d.reverbLevel * 100.0;
  g_app.sound.noiseGate   = d.noiseGateLevel * 100.0;
  g_app.sound.directSound = d.directSoundEnabled;
  g_app.sound.voiceEnhance= d.micEqEnabled;
  g_app.sound.sideToneLevel = (d.sideToneGain >= 0) ? d.sideToneGain : 0;
  g_app.sound.clamp();
  g_panel.sound = g_app.sound;
}

// 轮询设备列表（requestType=1）
// ★ 注意：请求 rt=1 在服务侧要 4~6 秒才响应，**绝不能在 UI 线程同步等**，
//   否则窗口每 2 秒卡 4~6 秒（表现为"界面很卡/数据经常是空的"）。
//   这里只读后台线程已经拿到的缓存（g_pipe.Devices()），不做任何阻塞请求。
static void PollDeviceList() {
  if (!g_pipe.Ready()) return;
  DWORD now = GetTickCount();
  if (now - g_lastDevPoll < 1000) return;
  g_lastDevPoll = now;

  auto devs = g_pipe.Devices();
  if (devs.empty()) return;

  bool changed = (devs.size() != g_app.devices.size());
  if (!changed) {
    for (size_t i = 0; i < devs.size(); i++) {
      if (devs[i].serialNumber != g_app.devices[i].serialNumber ||
          devs[i].battery != g_app.devices[i].battery ||
          devs[i].noiseReductionLevel != g_app.devices[i].noiseReductionLevel ||
          devs[i].sideToneGain != g_app.devices[i].sideToneGain) { changed = true; break; }
    }
  }
  g_app.devices = devs;
  // 优先选耳机条目（deviceType=3）；没有耳机则选第一个
  int wantIdx = -1;
  for (size_t i = 0; i < devs.size(); i++) {
    if (devs[i].IsHeadset()) { wantIdx = (int)i; break; }
  }
  if (wantIdx < 0 && !devs.empty()) wantIdx = 0;

  if (wantIdx >= 0) {
    bool devChanged = (g_app.selectedDevice != wantIdx);
    g_app.selectedDevice = wantIdx;
    if (devChanged || changed) SyncSoundFromDevice();
  } else {
    g_app.selectedDevice = -1;
  }
  g_app.serviceOk = true;
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
  switch (msg) {
    case WM_PAINT: Repaint(hwnd); return 0;

    case WM_ERASEBKGND: return 1;

    case WM_MOUSEWHEEL: {
      if (!g_newUi) break;
      int delta = GET_WHEEL_DELTA_WPARAM(w);

      // 总览页：光标在"已连接设备"面板上时，滚轮滚面板内部列表
      if (g_app.page == eui2::PAGE_OVERVIEW) {
        POINT p = {GET_X_LPARAM(l), GET_Y_LPARAM(l)};
        ScreenToClient(hwnd, &p);
        eui2::Layout L = eui2::ComputeLayout(g_app);
        eui2::OvDevList od = eui2::OvDevLayout(g_app, L);
        if (od.maxScroll > 0 &&
            p.x >= od.view.X && p.x <= od.view.X + od.view.Width &&
            p.y >= od.view.Y - S(44) && p.y <= od.panel.Y + od.panel.Height) {
          g_app.ovDevScroll -= (float)delta / 4.0f;
          if (g_app.ovDevScroll > od.maxScroll) g_app.ovDevScroll = od.maxScroll;
          if (g_app.ovDevScroll < 0) g_app.ovDevScroll = 0;
          InvalidateRect(hwnd, nullptr, FALSE);
          return 0;
        }
      }

      // ★ contentHeight 现在以"内容坐标"计（与滚动无关），
      //   所以 maxOff 稳定，不会出现"滚到底又弹回去"。
      float maxOff = eui2::MaxScroll(g_app);
      if (maxOff > 0) {
        g_app.contentOffset -= (float)delta / 4.0f;
        if (g_app.contentOffset > maxOff) g_app.contentOffset = maxOff;
        if (g_app.contentOffset < 0) g_app.contentOffset = 0;
        InvalidateRect(hwnd, nullptr, FALSE);
      } else {
        g_app.contentOffset = 0;
      }
      return 0;
    }

    // ---- 顶栏空白处可拖动窗口（走系统原生拖动，比手动 SetWindowPos 稳）----
    case WM_NCHITTEST: {
      LRESULT r = DefWindowProcW(hwnd, msg, w, l);
      if (r != HTCLIENT) return r;
      POINT p = {GET_X_LPARAM(l), GET_Y_LPARAM(l)};
      ScreenToClient(hwnd, &p);
      const eui2::Metrics& mm = eui2::M();
      int topH = MulDiv(mm.topH, mm.dpi, 96);
      if (p.y < topH) {
        eui2::HitInfo hh = eui2::HitTest(g_app, p);
        // 顶栏上只有设备选择器和关闭按钮是控件，其余空白都可拖动
        if (hh.id == eui2::CID_NONE) return HTCAPTION;
        return HTCLIENT;
      }
      // ★ 内容区空白处也可拖动 —— 之前只有 60px 的顶栏能拖，用户在长页面
      //   滚到中段后习惯性抓着页面空白拖，会感觉"窗口拖不动了"。
      //   浮层（语言/预设下拉）打开时不劫持，让"点外部关闭浮层"继续生效。
      if (!g_app.langOpen && !g_app.appPickOpen) {
        eui2::HitInfo hh = eui2::HitTest(g_app, p);
        if (hh.id == eui2::CID_NONE) return HTCAPTION;
      }
      return HTCLIENT;
    }

    // ---- 拖动开始于 NC 区（HTCAPTION）时，先收起浮层 ----
    case WM_NCLBUTTONDOWN:
      if (g_app.langOpen || g_app.appPickOpen) {
        g_app.langOpen = false;
        g_app.appPickOpen = false;
        g_app.appPickHover = -1;
        InvalidateRect(hwnd, nullptr, FALSE);
      }
      break;   // 继续走 DefWindowProc 的移动循环

    // ---- 可点击元素显示手型光标（否则用户不知道哪里能点）----
    case WM_SETCURSOR: {
      if (LOWORD(l) == HTCLIENT && g_newUi) {
        POINT p; GetCursorPos(&p); ScreenToClient(hwnd, &p);
        eui2::HitInfo hh = eui2::HitTest(g_app, p);
        bool clickable = (hh.id != eui2::CID_NONE);
        SetCursor(LoadCursorW(nullptr, clickable ? IDC_HAND : IDC_ARROW));
        return TRUE;
      }
      if (LOWORD(l) == HTCLIENT && !g_newUi) {
        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
        return TRUE;
      }
      break;
    }

    // ---- 应用规则的进程名手动输入 ----
    case WM_CHAR: {
      if (!g_newUi || g_app.page != eui2::PAGE_APPS || !g_app.appAddMode) break;
      wchar_t c = (wchar_t)w;
      if (c == L'\r' || c == L'\n') {           // Enter：确认暂存
        if (!g_app.appAddText.empty()) g_app.appAddMode = false;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
      }
      if (c == 27) {                            // Esc：取消输入
        g_app.appAddMode = false;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
      }
      if (c == L'\b') {
        if (!g_app.appAddText.empty()) g_app.appAddText.pop_back();
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
      }
      // 进程名只会有 ASCII
      if (c >= 32 && c < 127 && g_app.appAddText.size() < 64) {
        wchar_t lc = (c >= L'A' && c <= L'Z') ? (wchar_t)(c - L'A' + L'a') : c;
        g_app.appAddText.push_back(lc);
        InvalidateRect(hwnd, nullptr, FALSE);
      }
      return 0;
    }

    // ---- 录制全局快捷键：窗口拿到键盘焦点后收组合键 ----
    case WM_KEYDOWN: {
      if (!g_newUi || g_hkCapture < 0) break;
      int i = g_hkCapture;
      UINT key = (UINT)w;

      if (key == VK_ESCAPE) { HkCancelCapture(); return 0; }
      if (key == VK_BACK || key == VK_DELETE) {        // 清除该项
        g_hkSlots[i].mods = 0;
        g_hkSlots[i].vk = 0;
        g_hkCapture = -1;
        RegisterAllHotkeys();
        SaveSettings(CurrentSettings());
        HkSyncView();
        HkSay(g_app.english ? L"Hotkey cleared" : L"已清除该快捷键");
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
      }
      // 只按了修饰键：继续等真正的键，不要当成结果
      if (key == VK_CONTROL || key == VK_LCONTROL || key == VK_RCONTROL ||
          key == VK_MENU || key == VK_LMENU || key == VK_RMENU ||
          key == VK_SHIFT || key == VK_LSHIFT || key == VK_RSHIFT ||
          key == VK_LWIN || key == VK_RWIN || key == VK_APPS) {
        return 0;
      }
      wchar_t kn[16];
      if (!HkKeyName(key, kn, 16)) {
        HkSay(g_app.english ? L"That key is not supported - use Ctrl/Alt + letter or F1-F12"
                            : L"这个键不支持，请用 Ctrl/Alt + 字母或 F1–F12");
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
      }
      UINT mods = 0;
      if (GetKeyState(VK_CONTROL) & 0x8000) mods |= MOD_CONTROL;
      if (GetKeyState(VK_MENU)    & 0x8000) mods |= MOD_ALT;
      if (GetKeyState(VK_SHIFT)   & 0x8000) mods |= MOD_SHIFT;
      if (!mods) {
        HkSay(g_app.english ? L"Add Ctrl, Alt or Shift - a plain key would be captured system-wide"
                            : L"请加上 Ctrl / Alt / Shift —— 单个键会被全局拦截");
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
      }
      // 和别的项重复：先清掉旧的，避免注册失败
      for (int k = 0; k < 3; k++) {
        if (k != i && g_hkSlots[k].mods == mods && g_hkSlots[k].vk == key) {
          g_hkSlots[k].mods = 0;
          g_hkSlots[k].vk = 0;
        }
      }
      g_hkSlots[i].mods = mods;
      g_hkSlots[i].vk = key;
      g_hkCapture = -1;
      RegisterAllHotkeys();
      SaveSettings(CurrentSettings());
      HkSyncView();
      Log(L"hotkeys: slot %d -> %ls", i, HkFormat(mods, key).c_str());
      InvalidateRect(hwnd, nullptr, FALSE);
      return 0;
    }

    case WM_MOUSEMOVE: {
      POINT p = {GET_X_LPARAM(l), GET_Y_LPARAM(l)};
      // ---- 新版界面 ----
      if (g_newUi) {
        // 音量拖动
        if (g_volDrag >= 0) {
          eui2::Layout L = eui2::ComputeLayout(g_app);
          int tx = (g_volDrag == 0) ? L.volTrackX : L.trackX;
          int tw = (g_volDrag == 0) ? L.volTrackW : L.trackW;
          double pct = (tw > 0) ? (double)(p.x - tx) / (double)tw : 0.0;
          if (pct < 0) pct = 0;
          if (pct > 1) pct = 1;
          if (g_volDrag == 0) {
            std::wstring sid = g_audio.DefaultId(eConsole);
            if (!sid.empty()) g_audio.SetVolumePct(sid, (float)pct, false);
            g_app.speakerVol = (int)(pct * 100 + 0.5);
          } else {
            std::wstring mid = g_audio.DefaultCaptureId();
            if (!mid.empty()) g_audio.SetVolumePct(mid, (float)pct, true);
            g_app.micVol = (int)(pct * 100 + 0.5);
          }
          InvalidateRect(hwnd, nullptr, FALSE);
          return 0;
        }
        // 设备页：拖动某一行端点的音量条
        if (g_devVolDrag >= 0) {
          std::vector<Endpoint>* list = g_devVolCap ? g_app.captureEndpoints : g_app.endpoints;
          eui2::Layout L = eui2::ComputeLayout(g_app);
          eui2::DevRows dr = eui2::DevLayout(g_app, L, g_devVolCap);
          if (list && g_devVolDrag < (int)dr.idx.size() &&
              g_devVolDrag < (int)g_app.devVol.size()) {
            const Endpoint& ep = (*list)[dr.idx[g_devVolDrag]];
            const RectF& tr = dr.track[g_devVolDrag];
            double pct = (tr.Width > 0) ? (double)(p.x - tr.X) / (double)tr.Width : 0.0;
            if (pct < 0) pct = 0;
            if (pct > 1) pct = 1;
            g_audio.SetVolumePct(ep.id, (float)pct, g_devVolCap);
            g_app.devVol[g_devVolDrag] = (int)(pct * 100 + 0.5);
          }
          InvalidateRect(hwnd, nullptr, FALSE);
          return 0;
        }
        // 混音器：拖动某行应用的音量条
        if (g_mixVolDrag >= 0) {
          if (g_mixVolDrag < (int)g_app.mix.size()) {
            eui2::Layout L = eui2::ComputeLayout(g_app);
            int mx = L.contentX + L.contentW - S(12) - S(26);
            int tx = mx - S(8) - S(40) - S(8) - S(110);
            DWORD pid = MixerPidByName(g_app.mix[g_mixVolDrag].name);
            if (pid) {
              double pct = (double)(p.x - tx) / (double)S(110);
              if (pct < 0) pct = 0;
              if (pct > 1) pct = 1;
              g_audio.SetAppVolumeByPid(pid, (float)pct);
              g_app.mix[g_mixVolDrag].vol = (int)(pct * 100 + 0.5);
              if (g_app.mix[g_mixVolDrag].vol > 0) g_app.mix[g_mixVolDrag].muted = false;
            }
          }
          InvalidateRect(hwnd, nullptr, FALSE);
          return 0;
        }
        // ★ 拖动坐标必须来自 ComputeLayout()，与绘制完全一致
        if (g_eqDrag >= 0 && (g_app.page == eui2::PAGE_MIC || g_app.page == eui2::PAGE_SOUND)) {
          const eui2::Metrics& m = eui2::M();
          const int dd2 = m.dpi;
          auto S = [dd2](int v) { return MulDiv(v, dd2, 96); };
          eui2::Layout L = eui2::ComputeLayout(g_app);

          bool isMic = (g_app.page == eui2::PAGE_MIC);
          int eqTop = isMic ? L.micEqY : L.eqY;
          int eqH   = isMic ? L.micEqH : L.eqH;
          REAL top  = (REAL)(eqTop + S(isMic ? 20 : 22));
          REAL h    = (REAL)(eqH - S(isMic ? 52 : 56));
          REAL mid  = top + h / 2;
          double frac = (double)(mid - p.y) / (double)(h / 2);
          if (frac >  1.0) frac =  1.0;
          if (frac < -1.0) frac = -1.0;
          if (isMic) {
            g_app.sound.micEq[g_eqDrag] = frac * 6.0;
          } else {
            g_app.sound.speakerEq[g_eqDrag] = frac * 6.0;
            g_app.currentPreset = -1;
          }
          g_panel.sound = g_app.sound;
          InvalidateRect(hwnd, nullptr, FALSE);
          return 0;
        }
        eui2::HitInfo h = eui2::HitTest(g_app, p);
        if (h.id != g_app.hoverId || h.index != g_app.hoverIndex ||
            h.action != g_app.actionHint) {
          g_app.hoverId = h.id;
          g_app.hoverIndex = h.index;
          g_app.actionHint = h.action;
          InvalidateRect(hwnd, nullptr, FALSE);
        }
        // ★ 新版界面也要 TrackMouseEvent，否则鼠标移出窗口后 hover 永远停在那
        {
          TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, hwnd, 0};
          TrackMouseEvent(&tme);
        }
        return 0;
      }
      // 音效页拖动 EQ 柱 / 滑块
      if (g_panel.dragging && g_showFx) {
        if (g_panel.dragTarget == epospanel::ID_SPEAKER_EQ && g_fxBand >= 0) {
          g_panel.sound.speakerEq[g_fxBand] =
              epospanel::DbFromPoint(g_fxLayout.speakerCurve, p, g_dpi);
          g_panel.currentPreset = -1;
        } else if (g_panel.dragTarget == epospanel::ID_MIC_EQ && g_fxBand >= 0) {
          g_panel.sound.micEq[g_fxBand] =
              epospanel::DbFromPoint(g_fxLayout.micCurve, p, g_dpi);
          g_panel.currentPreset = -1;
        } else if (g_panel.dragTarget == epospanel::ID_SIDETONE) {
          double pct = epospanel::PctFromPoint(g_fxLayout.sideToneTrack, p);
          g_panel.sound.sideToneLevel = pct * 20.0;
        } else if (g_panel.dragTarget == epospanel::ID_REVERB) {
          double pct = epospanel::PctFromPoint(g_fxLayout.reverbTrack, p);
          g_panel.sound.reverb = pct * 100.0;
        } else if (g_panel.dragTarget == epospanel::ID_GATE) {
          double pct = epospanel::PctFromPoint(g_fxLayout.gateTrack, p);
          g_panel.sound.noiseGate = pct * 100.0;
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
      }
      if (g_dragging) {
        POINT cur; GetCursorPos(&cur);
        SetWindowPos(hwnd, nullptr, cur.x - g_dragOffset.x, cur.y - g_dragOffset.y,
                     0, 0, SWP_NOSIZE | SWP_NOZORDER);
        return 0;
      }
      int ht = HitTest(p);
      int newRow = (ht >= 0 && ht < 100) ? ht : -1;
      int newBtn = (ht == 100) ? 100 : (ht == 200 ? 200 : -1);
      if (newRow != g_hoverRow || newBtn != g_hoverBtn) {
        g_hoverRow = newRow; g_hoverBtn = newBtn;
        InvalidateRect(hwnd, nullptr, FALSE);
      }
      // 设置手型光标
      TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, hwnd, 0};
      TrackMouseEvent(&tme);
      return 0;
    }

    case WM_MOUSELEAVE:
      g_hoverRow = -1; g_hoverBtn = -1;
      // ★ 新版界面的 hover 状态也要清，否则"鼠标移走了还高亮着"
      if (g_newUi) {
        g_app.hoverId = -1;
        g_app.hoverIndex = -1;
        g_app.actionHint = eui2::ACT_NONE;
      }
      InvalidateRect(hwnd, nullptr, FALSE);
      return 0;

    case WM_LBUTTONDOWN: {
      POINT p = {GET_X_LPARAM(l), GET_Y_LPARAM(l)};
      // ---- 新版界面 ----
      if (g_newUi) {
        eui2::HitInfo h = eui2::HitTest(g_app, p);
        // ★ 关闭按钮：收进托盘（真正退出在托盘菜单里）
        if (h.id == eui2::CID_CLOSE) {
          if (!g_app.setCloseToTray) { DestroyWindow(hwnd); return 0; }  // 设置里选了"直接退出"
          g_winVisible = false;
          ShowWindow(hwnd, SW_HIDE);
          static bool s_toldOnce = false;
          if (!s_toldOnce) {
            s_toldOnce = true;
            TrayBalloon(g_app.english ? L"Still running" : L"仍在后台运行",
                        g_app.english
                          ? L"Closed to the tray. Click the tray icon to reopen, or right-click to exit."
                          : L"已收进系统托盘。单击托盘图标可重新打开，右键菜单可退出。",
                        NIIF_INFO);
          }
          return 0;
        }
        if (h.id == eui2::CID_EQ_BASE || h.id == eui2::CID_EQ_MIC_BASE) {
          g_eqDrag = h.index;
          SetCapture(hwnd);
          return 0;
        }
        if (h.id == eui2::CID_DEVVOL) {
          // 设备页：单端点音量条，立即响应点击位置
          g_devVolDrag = h.index;
          g_devVolCap = (g_app.devTab == 1);
          SetCapture(hwnd);
          {
            std::vector<Endpoint>* list = g_devVolCap ? g_app.captureEndpoints : g_app.endpoints;
            eui2::Layout L = eui2::ComputeLayout(g_app);
            eui2::DevRows dr = eui2::DevLayout(g_app, L, g_devVolCap);
            if (list && g_devVolDrag < (int)dr.idx.size() &&
                g_devVolDrag < (int)g_app.devVol.size()) {
              const Endpoint& ep = (*list)[dr.idx[g_devVolDrag]];
              const RectF& tr = dr.track[g_devVolDrag];
              double pct = (tr.Width > 0) ? (double)(p.x - tr.X) / (double)tr.Width : 0.0;
              if (pct < 0) pct = 0;
              if (pct > 1) pct = 1;
              g_audio.SetVolumePct(ep.id, (float)pct, g_devVolCap);
              g_app.devVol[g_devVolDrag] = (int)(pct * 100 + 0.5);
              if (g_app.devVol[g_devVolDrag] > 0 && g_app.devMuted.size() > (size_t)g_devVolDrag &&
                  g_app.devMuted[g_devVolDrag]) {
                g_audio.SetMuted(ep.id, false);        // 拖音量自动取消静音
                g_app.devMuted[g_devVolDrag] = false;
              }
            }
          }
          InvalidateRect(hwnd, nullptr, FALSE);
          return 0;
        }
        if (h.id == eui2::CID_MIXVOL) {
          // 混音器：单应用音量条，立即响应
          g_mixVolDrag = h.index;
          SetCapture(hwnd);
          if (g_mixVolDrag < (int)g_app.mix.size()) {
            eui2::Layout L = eui2::ComputeLayout(g_app);
            int mx = L.contentX + L.contentW - S(12) - S(26);
            int tx = mx - S(8) - S(40) - S(8) - S(110);
            DWORD pid = MixerPidByName(g_app.mix[g_mixVolDrag].name);
            if (pid) {
              double pct = (double)(p.x - tx) / (double)S(110);
              if (pct < 0) pct = 0;
              if (pct > 1) pct = 1;
              g_audio.SetAppVolumeByPid(pid, (float)pct);
              g_app.mix[g_mixVolDrag].vol = (int)(pct * 100 + 0.5);
              if (g_app.mix[g_mixVolDrag].vol > 0) g_app.mix[g_mixVolDrag].muted = false;
            }
          }
          InvalidateRect(hwnd, nullptr, FALSE);
          return 0;
        }
        if (h.id == eui2::CID_VOL_SPEAKER || h.id == eui2::CID_VOL_MIC) {
          g_volDrag = (h.id == eui2::CID_VOL_SPEAKER) ? 0 : 1;
          SetCapture(hwnd);
          // 立即响应点击位置
          {
            eui2::Layout L = eui2::ComputeLayout(g_app);
            int tx = (g_volDrag == 0) ? L.volTrackX : L.trackX;
            int tw = (g_volDrag == 0) ? L.volTrackW : L.trackW;
            double pct = (tw > 0) ? (double)(p.x - tx) / (double)tw : 0.0;
            if (pct < 0) pct = 0;
            if (pct > 1) pct = 1;
            if (g_volDrag == 0) {
              std::wstring sid = g_audio.DefaultId(eConsole);
              if (!sid.empty()) g_audio.SetVolumePct(sid, (float)pct, false);
              g_app.speakerVol = (int)(pct * 100 + 0.5);
            } else {
              std::wstring mid = g_audio.DefaultCaptureId();
              if (!mid.empty()) g_audio.SetVolumePct(mid, (float)pct, true);
              g_app.micVol = (int)(pct * 100 + 0.5);
            }
            InvalidateRect(hwnd, nullptr, FALSE);
          }
          return 0;
        }
        NewUiOnClick(h, p);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
      }
      int ht = HitTest(p);

      // ---- 音效页 ----
      if (g_showFx) {
        if (ht == 500) { g_showFx = false; InvalidateRect(hwnd, nullptr, FALSE); return 0; }
        if (ht == 501) { g_showFx = true;  InvalidateRect(hwnd, nullptr, FALSE); return 0; }
        if (ht == epospanel::ID_NONE) {
          // 点空白区：结束拖动
          if (g_panel.dragging) {
            g_panel.dragging = false;
            ApplySoundToDevice();
          }
          return 0;
        }
        // 需要持续拖动的控件
        if (ht == epospanel::ID_SPEAKER_EQ || ht == epospanel::ID_MIC_EQ ||
            ht == epospanel::ID_SIDETONE || ht == epospanel::ID_REVERB ||
            ht == epospanel::ID_GATE) {
          g_panel.dragging = true;
          g_panel.dragTarget = ht;
          SetCapture(hwnd);
          // 立即应用一次初值
          FxOnClick(ht, g_fxBand, p);
          return 0;
        }
        FxOnClick(ht, g_fxBand, p);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
      }

      if (ht == 500) { g_showFx = false; InvalidateRect(hwnd, nullptr, FALSE); return 0; }
      if (ht == 501) { g_showFx = true;  InvalidateRect(hwnd, nullptr, FALSE); return 0; }

      if (ht == 100) {           // 关闭 → 隐藏到托盘
        ShowWindow(hwnd, SW_HIDE);
        g_winVisible = false;
        return 0;
      }
      if (ht == 200) {           // 自动切换开关
        g_autoSwitch = !g_autoSwitch;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
      }
      if (ht >= 0 && ht < 100) { // 切换到该设备
        std::vector<const Endpoint*> list;
        BuildSwitchList(list);
        if (ht < (int)list.size()) {
          g_audio.SetDefault(list[ht]->id);
          g_autoSwitch = false;   // 手动切换 → 暂停自动
          UpdateTray();
          InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
      }
      // 拖动
      if (p.y < S(60)) {
        RECT rc; GetWindowRect(hwnd, &rc);
        POINT cur; GetCursorPos(&cur);
        g_dragOffset.x = cur.x - rc.left;
        g_dragOffset.y = cur.y - rc.top;
        g_dragging = true;
        SetCapture(hwnd);
      }
      return 0;
    }

    case WM_LBUTTONUP:
      if (g_newUi) {
        if (g_eqDrag >= 0) {
          g_eqDrag = -1;
          ReleaseCapture();
          ApplySoundToDevice2();
          InvalidateRect(hwnd, nullptr, FALSE);
        }
        if (g_volDrag >= 0) {
          g_volDrag = -1;
          ReleaseCapture();
          InvalidateRect(hwnd, nullptr, FALSE);
        }
        if (g_devVolDrag >= 0) {
          g_devVolDrag = -1;
          ReleaseCapture();
          InvalidateRect(hwnd, nullptr, FALSE);
        }
        if (g_mixVolDrag >= 0) {
          g_mixVolDrag = -1;
          ReleaseCapture();
          InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
      }
      if (g_panel.dragging) {
        g_panel.dragging = false;
        g_panel.dragTarget = epospanel::ID_NONE;
        ReleaseCapture();
        ApplySoundToDevice();      // 拖动结束才真正下发
      }
      if (g_dragging) { g_dragging = false; ReleaseCapture(); }
      return 0;

    case WM_TRAYICON:
      if (LOWORD(l) == WM_LBUTTONUP) {
        g_winVisible = !g_winVisible;
        ShowWindow(hwnd, g_winVisible ? SW_SHOW : SW_HIDE);
        if (g_winVisible) SetForegroundWindow(hwnd);
      } else if (LOWORD(l) == WM_RBUTTONUP) {
        ShowTrayMenu();
      }
      return 0;

    case WM_COMMAND: {
      int id = LOWORD(w);
      if (id == IDM_FX) {
        g_showFx = true;
        g_winVisible = true;
        ShowWindow(hwnd, SW_SHOW);
        SetForegroundWindow(hwnd);
        UpdateTray();
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
      }
      HandleMenuCmd((UINT)id);
      return 0;
    }

    case WM_HOTKEY: {
      switch (w) {
      case HK_CYCLE_OUT: {
        // 在可见的播放设备之间循环切换
        if (g_eps.empty()) break;
        std::wstring cur = g_audio.DefaultId(eConsole);
        std::vector<const Endpoint*> vis;
        for (auto& e : g_eps) if (e.kind != KIND_EXCLUDED) vis.push_back(&e);
        if (vis.empty()) break;
        size_t i = 0;
        for (size_t k = 0; k < vis.size(); k++) if (vis[k]->id == cur) { i = (k + 1) % vis.size(); break; }
        if (g_audio.SetDefault(vis[i]->id)) {
          g_app.currentOutputId = vis[i]->id;
          Log(L"hotkey: output -> %ls", vis[i]->name.c_str());
          TrayBalloon(g_app.english ? L"Output device" : L"播放设备",
                      vis[i]->name.c_str(), NIIF_INFO);
        }
        break;
      }
      case HK_MUTE: {
        std::wstring sid = g_audio.DefaultId(eConsole);
        if (!sid.empty()) {
          bool m = g_audio.IsMuted(sid);
          g_audio.SetMuted(sid, !m);
          TrayBalloon(g_app.english ? L"Mute" : L"静音",
                      m ? (g_app.english ? L"Unmuted" : L"已取消静音")
                        : (g_app.english ? L"Muted" : L"已静音"), NIIF_INFO);
        }
        break;
      }
      case HK_CYCLE_EQ: {
        if (g_app.presets.empty()) break;
        int n = (int)g_app.presets.size();
        int next = (g_app.currentPreset + 1) % n;
        g_app.presets[next].ApplyTo(g_app.sound);
        g_app.currentPreset = next;
        g_panel.sound = g_app.sound;
        ApplySoundToDevice2();
        Log(L"hotkey: preset -> %ls", g_app.presets[next].key.c_str());
        TrayBalloon(g_app.english ? L"EQ preset" : L"EQ 预设",
                    (g_app.english ? g_app.presets[next].en : g_app.presets[next].zh).c_str(),
                    NIIF_INFO);
        break;
      }
      default: break;
      }
      if (g_hwnd) InvalidateRect(g_hwnd, nullptr, FALSE);
      return 0;
    }

    case WM_APP_BOOT: {
      DWORD tB = GetTickCount();
      LoadAppRules();
      SyncAppRulesView();
      StartAncMonitor();     // ANC 开关 HID 只读监听（按键时上报）
      // 全局热键（可关闭 / 可改键；被别的软件占用会失败，只记日志不报错）
      RegisterAllHotkeys();
      Log(L"hotkeys: enabled=%d out=[%ls] mute=[%ls] eq=[%ls] -> ok=%d/%d/%d",
          (int)g_hkEnabled,
          HkFormat(g_hkSlots[0].mods, g_hkSlots[0].vk).c_str(),
          HkFormat(g_hkSlots[1].mods, g_hkSlots[1].vk).c_str(),
          HkFormat(g_hkSlots[2].mods, g_hkSlots[2].vk).c_str(),
          (int)g_hkSlots[0].ok, (int)g_hkSlots[1].ok, (int)g_hkSlots[2].ok);
      UpdateTray();
      Log(L"[BOOT +%lu ms] deferred UpdateTray took %lu ms", GetTickCount() - g_tBoot, GetTickCount() - tB);
      InvalidateRect(hwnd, nullptr, FALSE);
      return 0;
    }

    case WM_TIMER: {
      // 定时器 2：麦克风页的实时电平（80ms，仅在"测试已开启"时运行）
      if (w == 2) { UpdateMicMeter(); return 0; }
      DWORD tt0 = GetTickCount();
      UpdateTray();
      DWORD tt1 = GetTickCount();
      AutoDecide();
      // ★ 超越官方的三项主动能力
      CheckBatteryAlert();     // 低电量提醒（20% / 10% 各一次）
      CheckAutoRestore();      // 耳机回来自动切回输出
      CheckAppPreset();        // 按前台应用切预设
      DWORD tt2 = GetTickCount();
      if (g_newUi) {
        // ★ 只有"用户在麦克风页主动开了测试"才跑电平定时器。
        //   之前只要在麦克风页就一直开着采集流 —— 等于一直在录，占 CPU / 占设备。
        bool wantMeter = (g_app.page == eui2::PAGE_MIC && g_app.micTestOn);
        if (wantMeter) {
          SetTimer(hwnd, 2, 80, nullptr);
        } else {
          KillTimer(hwnd, 2);
          if (g_micMon.Running()) g_micMon.Stop();   // 别一直占着麦克风
          if (!g_app.micTestOn) {
            // 关掉测试后把电平归零，避免残留一根"假信号"
            g_app.micPeakDb = -60.0f;
            g_app.micPeakHoldDb = -60.0f;
            g_app.micActive = false;
            g_app.micSilentLong = false;
          }
        }
        PollDeviceList();
        g_app.pipeReady = g_pipe.Ready();
        // 设备页可见时刷新每行音量/静音（其它页面零开销）
        if (g_app.page == eui2::PAGE_DEVICES) RefreshDevVolumes();
        // 混音器页：刷新每应用音量
        if (g_app.page == eui2::PAGE_MIXER) RefreshMix();
        // 快捷键状态同步给 UI（3 个短字符串，可忽略开销）
        HkSyncView();
        g_app.hkMsg = (g_hkMsg.empty() || GetTickCount() - g_hkMsgAt > 5000) ? L"" : g_hkMsg;
        // ★ 预设列表读取要扫目录 + 解析文件，实测能到 5 秒。
        //   不要每 2 秒重读：改为 20 秒一次；保存/捕获后把 g_presetReloadAt 清零立即重读。
        if (g_presetReloadAt == 0 || GetTickCount() - g_presetReloadAt > 20000) {
          g_app.presets = eposfx::AllPresets();
          g_presetReloadAt = GetTickCount();
        }
        g_app.endpoints = &g_eps;
        g_app.captureEndpoints = &g_epc;
        g_app.currentOutputId = g_audio.DefaultId(eConsole);
        g_app.currentInputId  = g_audio.DefaultId(eConsole, true);
        // 音量：从 Windows 端点读（拖动中不要覆盖，否则滑块会跳）
        if (g_volDrag < 0) {
          std::wstring sid = g_audio.DefaultId(eConsole);
          std::wstring mid = g_audio.DefaultCaptureId();
          if (!sid.empty()) {
            g_app.speakerVol = g_audio.GetVolumePct(sid, false);
            g_app.speakerMuted = g_audio.IsMuted(sid);
          }
          if (!mid.empty()) {
            g_app.micVol = g_audio.GetVolumePct(mid, true);
            g_app.micMuted = g_audio.IsMuted(mid);
          }
        }
      }
      DWORD tt3 = GetTickCount();
      if (tt3 - tt0 > 150) {
        Log(L"WM_TIMER slow: tray=%lu auto=%lu ui=%lu total=%lu",
            tt1 - tt0, tt2 - tt1, tt3 - tt2, tt3 - tt0);
      }
      InvalidateRect(hwnd, nullptr, FALSE);
      return 0;
    }

    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(hwnd, msg, w, l);
}

// 未处理异常记录：把异常码和出错地址写进日志，便于定位崩溃
static LONG WINAPI CrashFilter(EXCEPTION_POINTERS* ep) {
  if (ep && ep->ExceptionRecord) {
    Log(L"FATAL: exception 0x%08lX at %p",
        (unsigned long)ep->ExceptionRecord->ExceptionCode,
        ep->ExceptionRecord->ExceptionAddress);
  }
  return EXCEPTION_EXECUTE_HANDLER;
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR lpCmdLine, int) {
  g_inst = inst;
  g_tBoot = GetTickCount();
  SetUnhandledExceptionFilter(CrashFilter);

  // --probe-usbpcap : 列出当前存在的 USBPcapN 控制设备（不需要 COM，最先处理）
  if (lpCmdLine && wcsstr(lpCmdLine, L"--probe-usbpcap")) {
    int cnt = 0;
    // 不要用 ccs=UTF-8：会写入 BOM，cmd 的 for /f 会把 BOM 当成内容的一部分
    FILE* f = _wfopen(L"usbpcap_probe.txt", L"w");
    for (int n = 1; n <= 10; n++) {
      wchar_t path[64];
      swprintf(path, 64, L"\\\\.\\USBPcap%d", n);
      HANDLE h = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                             OPEN_EXISTING, 0, nullptr);
      if (h != INVALID_HANDLE_VALUE) {
        if (f) fwprintf(f, L"%d\n", n);
        cnt++;
        CloseHandle(h);
      }
    }
    if (f) fclose(f);
    return cnt;     // 退出码 = 找到的数量
  }
  g_dryRun = (lpCmdLine && wcsstr(lpCmdLine, L"--dry-run") != nullptr);
  // 调试开关：--sim-battery=15 用于验证低电量提醒，不影响真实读数路径
  if (lpCmdLine) {
    const wchar_t* sb = wcsstr(lpCmdLine, L"--sim-battery=");
    if (sb) g_simBattery = _wtoi(sb + 14);
    const wchar_t* mb = wcsstr(lpCmdLine, L"--mic=");
    if (mb) { g_micNameFilter = mb + 6; }
  }
  CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  Log(L"[BOOT +%lu ms] CoInitializeEx done", GetTickCount() - g_tBoot);

  if (!g_audio.Init()) {
    Log(L"FATAL Core Audio init failed");
    if (!g_dryRun) MessageBoxW(nullptr, L"Core Audio 初始化失败", L"eposwitch", MB_ICONERROR);
    CoUninitialize();
    return 1;
  }
  Log(L"[BOOT +%lu ms] CoreAudio Init done", GetTickCount() - g_tBoot);

  if (lpCmdLine && wcsstr(lpCmdLine, L"--dump-icon")) {
    GdiplusStartupInput gsi2;
    GdiplusStartup(&g_gdiToken, &gsi2, nullptr);
    g_eps = g_audio.ActiveRender();
    g_probe = ProbeEpos(&g_battery);
    {
      Bitmap big(128, 128, PixelFormat32bppARGB);
      { Graphics g(&big); DrawTrayGlyph(g, 128); }
      CLSID pngClsid;
      UINT num = 0, size = 0;
      GetImageEncodersSize(&num, &size);
      if (size) {
        auto* info = (ImageCodecInfo*)malloc(size);
        GetImageEncoders(num, size, info);
        for (UINT i = 0; i < num; i++)
          if (wcscmp(info[i].MimeType, L"image/png") == 0) { pngClsid = info[i].Clsid; break; }
        big.Save(L"icon_preview.png", &pngClsid, nullptr);
        free(info);
      }
    }
    Log(L"icon dumped: probe=%ls pct=%d", ProbeName(g_probe), g_battery);
    g_audio.Shutdown();
    GdiplusShutdown(g_gdiToken);
    CoUninitialize();
    return 0;
  }

  if (g_dryRun) {
    Log(L"=== DRY RUN ===");
    g_eps = g_audio.ActiveRender();
    g_probe = ProbeEpos(&g_battery);
    Log(L"probe=%ls battery=%d%% age=%.1fs", ProbeName(g_probe), g_battery, g_batteryAge);
    for (auto& e : g_eps)
      Log(L"  kind=%-6ls state=%lu name=[%ls]", KindName(e.kind), (unsigned long)e.state, e.name.c_str());
    g_audio.Shutdown();
    CoUninitialize();
    return 0;
  }

  GdiplusStartupInput gsi;
  GdiplusStartup(&g_gdiToken, &gsi, nullptr);
  Log(L"[BOOT +%lu ms] GdiplusStartup done, font family = [%ls]",
      GetTickCount() - g_tBoot, eui::FFName());

  // DPI
  HDC sdc = GetDC(nullptr);
  g_dpi = GetDeviceCaps(sdc, LOGPIXELSX);
  ReleaseDC(nullptr, sdc);
  SW = S(BASE_W); SH = S(BASE_H);

  // ★ 窗口尺寸必须与 eui2::Metrics 一致，否则 UI 坐标全错
  {
    const eui2::Metrics& mm = eui2::M();
    if (mm.w != BASE_W || mm.h != BASE_H) {
      Log(L"ERROR: Metrics(%d x %d) != window(%d x %d) - UI will misalign",
          mm.w, mm.h, BASE_W, BASE_H);
      MessageBoxW(nullptr, L"UI Metrics mismatch:\nMetrics and window size must match.",
                  L"EPOS", MB_OK | MB_ICONERROR);
    }
  }

  WNDCLASSEXW wc = {};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = WndProc;
  wc.hInstance = inst;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.lpszClassName = L"EpoSwitchUI";
  // 应用图标（app.rc 里的资源 1）。缺资源时不影响运行。
  wc.hIcon   = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR);
  wc.hIconSm = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
  RegisterClassExW(&wc);

  int x = GetSystemMetrics(SM_CXSCREEN) - SW - S(40);
  int y = S(80);
  g_hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
                           wc.lpszClassName, L"eposwitch",
                           WS_POPUP, x, y, SW, SH,
                           nullptr, nullptr, inst, nullptr);
  if (!g_hwnd) return 1;
  if (wc.hIcon)   SendMessageW(g_hwnd, WM_SETICON, ICON_BIG,   (LPARAM)wc.hIcon);
  if (wc.hIconSm) SendMessageW(g_hwnd, WM_SETICON, ICON_SMALL, (LPARAM)wc.hIconSm);
  Log(L"[BOOT +%lu ms] CreateWindow done", GetTickCount() - g_tBoot);

  // 圆角
  HRGN rgn = CreateRoundRectRgn(0, 0, SW + 1, SH + 1, S(14), S(14));
  SetWindowRgn(g_hwnd, rgn, TRUE);

  // 恢复全部设置（语言 + 各开关 + 开机启动状态）
  { AppSettings a = LoadSettings(); ApplySettings(a); }

  // EPOS 管道：GSUIPipe 只能有一个 server，官方 UI 开着时我们无法接管
  // ★ 先显示窗口，再做慢初始化。
  //   之前是"连管道 + UpdateTray（HID 枚举）"全做完才 ShowWindow，
  //   实测窗口要 4.1 秒才可见、8 秒才能交互。
  ShowWindow(g_hwnd, SW_SHOW);
  UpdateWindow(g_hwnd);
  Log(L"[BOOT +%lu ms] first paint done (window visible)", GetTickCount() - g_tBoot);

  DWORD tInit = GetTickCount();
  g_pipeReady = g_pipe.Start();
  if (g_pipeReady) {
    Log(L"EPOS pipe connected (GSAgentPipe + GSUIPipe server) in %lu ms", GetTickCount() - tInit);
    g_pipeRun = true;
    g_pipeThread = std::thread([] {
      // 预热：服务第一次写响应时客户端尚未 accept 完成，必然丢失
      std::string warm;
      g_pipe.Request(epospipe::Client::BuildDeviceQuery(), &warm, 4000, "\"messageType\":\"22\"");
      // 后台循环：UI 线程只读这里的缓存，绝不阻塞。
      // rt=1（完整设备列表）服务端要 4~6 秒才响应，所以单独一条线连续跑；
      // rt=2（电量）响应快，用它做节拍。
      while (g_pipeRun) {
        std::string resp1;
        g_pipe.Request(epospipe::Client::BuildDeviceListQuery(), &resp1, 9000, "\"DeviceList\"");
        std::string resp2;
        g_pipe.Request(epospipe::Client::BuildDeviceQuery(), &resp2, 4000, "\"messageType\":\"22\"");
        Sleep(1500);
      }
    });
  } else {
    Log(L"EPOS pipe unavailable (err=%lu) - Gaming Suite UI may be running", GetLastError());
  }

  g_evt = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  g_notify = new NotifyClient(g_evt);
  DWORD tCb = GetTickCount();
  g_audio.en->RegisterEndpointNotificationCallback(g_notify);
  Log(L"[BOOT +%lu ms] RegisterEndpointNotify took %lu ms",
      GetTickCount() - g_tBoot, GetTickCount() - tCb);

  // ★ 托盘图标 / 设备探测放在消息循环之后（WM_APP_BOOT），
  //   这样窗口从第一帧起就能响应鼠标和重绘，不会"看着在但点不动"。
  SetTimer(g_hwnd, 1, 2000, nullptr);
  PostMessageW(g_hwnd, WM_APP_BOOT, 0, 0);
  Log(L"[BOOT +%lu ms] entering message loop (deferred init posted)", GetTickCount() - g_tBoot);

  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0)) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }

  { NOTIFYICONDATAW n = {}; n.cbSize = sizeof(n); n.hWnd = g_hwnd; n.uID = ID_TRAY;
    Shell_NotifyIconW(NIM_DELETE, &n); }
  g_pipeRun = false;
  if (g_pipeThread.joinable()) g_pipeThread.join();
  g_pipe.Stop();
  StopAncMonitor();
  if (g_notify && g_audio.en) g_audio.en->UnregisterEndpointNotificationCallback(g_notify);
  g_audio.Shutdown();
  GdiplusShutdown(g_gdiToken);
  CoUninitialize();
  return 0;
}
