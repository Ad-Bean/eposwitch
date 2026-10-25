// EPOS Gaming Suite 命名管道客户端
//   发送请求 -> \\.\pipe\GSAgentPipe  (服务是 server，UI 是 client)
//   接收响应 <- \\.\pipe\GSUIPipe     (UI 是 server，服务是 client；我们当 server)
#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <thread>
#include <mutex>
#include <deque>
#include <bitset>
#include <atomic>
#include <vector>
#include <sstream>
#include <cstring>
#include <cstdio>
#include "eposdev.h"

// advapi32: ConvertStringSecurityDescriptorToSecurityDescriptorW
extern "C" BOOL WINAPI ConvertStringSecurityDescriptorToSecurityDescriptorW(
    LPCWSTR StringSecurityDescriptor, DWORD StringSDRevision,
    PSECURITY_DESCRIPTOR* SecurityDescriptor, DWORD* SecurityDescriptorSize);

namespace epospipe {

// 设备身份（EPOS 耳机本体，Dongle 是 PID 771）
inline const char* kVid = "5013";
inline const char* kPid = "257";
inline const char* kSerial = "A003480225325145";

class Client {
public:
    ~Client() { Stop(); }

    // 返回 true 表示管道链路可用
    bool Start() {
        // ---- 1) 建 GSUIPipe 服务端（服务会来连我们）----
        // 必须用 byte 模式 + 显式 ACL：服务以 LocalService 身份运行，默认 ACL 不含它
        // (error 5 = ACCESS_DENIED 就是漏了这一步)
        PSECURITY_DESCRIPTOR sd = nullptr;
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
                L"D:(A;;GA;;;WD)(A;;GA;;;SY)(A;;GA;;;BA)", 1, &sd, nullptr)) {
            return false;
        }
        SECURITY_ATTRIBUTES sa = {};
        sa.nLength = sizeof(sa);
        sa.lpSecurityDescriptor = sd;
        sa.bInheritHandle = FALSE;

        m_ui = ::CreateNamedPipeW(L"\\\\.\\pipe\\GSUIPipe",
                                  PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                                  PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                                  PIPE_UNLIMITED_INSTANCES, 65536, 65536, 0, &sa);
        ::LocalFree(sd);
        if (m_ui == INVALID_HANDLE_VALUE) return false;

        // ---- 2) 连 GSAgentPipe（服务端的管道）----
        m_agent = ::CreateFileW(L"\\\\.\\pipe\\GSAgentPipe", GENERIC_READ | GENERIC_WRITE,
                                0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (m_agent == INVALID_HANDLE_VALUE) {
            ::CloseHandle(m_ui); m_ui = INVALID_HANDLE_VALUE;
            return false;
        }

        m_stop = false;
        m_thread = std::thread([this] { ReaderLoop(); });
        return true;
    }

    void Stop() {
        m_stop = true;
        if (m_thread.joinable()) m_thread.join();
        if (m_agent != INVALID_HANDLE_VALUE) { ::CloseHandle(m_agent); m_agent = INVALID_HANDLE_VALUE; }
        if (m_ui != INVALID_HANDLE_VALUE)    { ::CloseHandle(m_ui);    m_ui = INVALID_HANDLE_VALUE; }
    }

    bool Ready() const { return m_agent != INVALID_HANDLE_VALUE; }

    // 发送一条 JSON 消息；resp 非空则等待并返回一条响应。
    //
    // ★ expectResp：期望的响应特征（子串）。服务端的响应是异步、可能乱序到达的
    //   （例如 rt=1 的 DeviceList 响应有时会迟到 6 秒以上，被后发的 rt=2 请求抢先消费）。
    //   传了 expectResp 就只取队列里**含该特征**的响应；不匹配的留在队列里等后续请求。
    //   传 nullptr 表示"取队列里第一条"（用于 rt=2 这类响应内容不确定的请求）。
    bool Request(const std::string& json, std::string* resp = nullptr, int timeoutMs = 3000,
                 const char* expectResp = nullptr) {
        if (m_agent == INVALID_HANDLE_VALUE) return false;
        DWORD written = 0;
        if (!::WriteFile(m_agent, json.data(), (DWORD)json.size(), &written, nullptr) || !written)
            return false;
        if (!resp) return true;

        const DWORD deadline = ::GetTickCount() + (DWORD)timeoutMs;
        while (::GetTickCount() < deadline) {
            // ★ 锁只包住"查队列"这一步，绝不能在持锁期间 Sleep。
            //   之前的写法把 Sleep(60) 放在 lock_guard 作用域内，等于后台线程
            //   几乎 100% 时间占着 m_mu；UI 线程调 HeadsetPresent() 抢锁会被饿死，
            //   实测卡满整个 4000ms 超时（窗口启动后 5 秒无响应）。
            {
                std::lock_guard<std::mutex> g(m_mu);
                if (!m_queue.empty()) {
                    if (!expectResp) {
                        *resp = m_queue.front();
                        m_queue.pop_front();
                        return true;
                    }
                    // 找到匹配项；前面的不匹配项保留在队列里
                    for (size_t i = 0; i < m_queue.size(); i++) {
                        if (m_queue[i].find(expectResp) != std::string::npos) {
                            *resp = m_queue[i];
                            m_queue.erase(m_queue.begin() + i);
                            return true;
                        }
                    }
                }
            }
            ::Sleep(15);
        }
        return false;
    }

    // 最近一次电量（messageType 22 的响应里解析 value）
    int LastBattery() const { return m_battery.load(); }

    // 只从队列里取一条消息（不发任何请求）。reverse/ 下的探针工具用。
    bool Poll(std::string* out) {
        std::lock_guard<std::mutex> g(m_mu);
        if (m_queue.empty()) return false;
        *out = m_queue.front();
        m_queue.pop_front();
        return true;
    }
    // 最近一次完整设备列表（requestType=1 的响应）
    std::vector<eposdev::DeviceInfo> Devices() const {
        std::lock_guard<std::mutex> g(m_mu);
        return m_devices;
    }
    // 按序列号找设备
    bool FindDevice(const std::wstring& serial, eposdev::DeviceInfo& out) const {
        std::lock_guard<std::mutex> g(m_mu);
        for (const auto& d : m_devices)
            if (d.serialNumber == serial) { out = d; return true; }
        return false;
    }
    bool AnyDevice() const {
        std::lock_guard<std::mutex> g(m_mu);
        return !m_devices.empty();
    }

    // ★ 耳机本体是否在线（最可靠的判据）
    // rt=1 的 DeviceList 里：
    //   deviceType=3 → 耳机本体（带电量/固件/降噪）
    //   deviceType=5 → 接收器 dongle（只有固件版本）
    // 只有 dongle 而没有 headset 时，说明 2.4G 无线链路断了（耳机没和 dongle 配对/掉线）。
    // ★ 无锁读取：ReaderLoop 解析设备列表时同时更新这两个原子标志，
    //   UI 线程查询时不会因为等锁而卡住（哪怕解析线程正忙）。
    bool HeadsetPresent() const { return m_headsetPresent.load(); }
    bool DonglePresent()  const { return m_donglePresent.load(); }

    // 解析出 JSON 里的整数 "value":NN
    static int ExtractInt(const std::string& s, const char* key) {
        std::string pat = std::string("\"") + key + "\":";
        size_t p = s.find(pat);
        if (p == std::string::npos) return -1;
        p += pat.size();
        bool neg = false;
        if (p < s.size() && s[p] == '-') { neg = true; p++; }
        if (p >= s.size() || s[p] < '0' || s[p] > '9') return -1;
        int v = 0;
        while (p < s.size() && s[p] >= '0' && s[p] <= '9') { v = v * 10 + (s[p] - '0'); p++; }
        return neg ? -v : v;
    }

    // ---------------- 构造请求 ----------------
    static std::string DeviceSel() {
        std::ostringstream o;
        o << "\"VID\":\"" << kVid << "\",\"PID\":\"" << kPid
          << "\",\"SerialNumber\":\"" << kSerial << "\"";
        return o.str();
    }

    // requestType=2：查询已连接设备 / 电量
    static std::string BuildDeviceQuery() {
        std::ostringstream o;
        o << "[{\"requestType\":\"2\",\"SelectedDevice\":[{"
          << DeviceSel() << ",\"changedValue\":null}]}]";
        return o.str();
    }

    // requestType=1：查询完整设备状态列表（含降噪、侧音、固件版本等 42 个字段）
    // 这是官方 UI 拿全部设备信息的唯一入口。
    static std::string BuildDeviceListQuery() {
        std::ostringstream o;
        o << "[{\"requestType\":\"1\",\"SelectedDevice\":[{"
          << DeviceSel() << ",\"changedValue\":null}]}]";
        return o.str();
    }

    // requestType=13：设置 APO 参数
    // speakerEq: 9 个 dB（-6..+6）；micEq: 9 个 dB 或空
    static std::string BuildApoSet(const double* speakerEq9, const double* micEq9,
                                   bool directSound, double reverbLevel, double noiseGateLevel,
                                   bool stereo3d) {
        std::ostringstream o;
        o << "[{\"requestType\":\"13\",\"SelectedDevice\":[{"
          << DeviceSel() << ",\"DeviceType\":\"BT\",\"apoParams\":{";
        o << "\"directSoundEnabled\":" << (directSound ? "0" : "null") << ",";
        o << "\"micMetersEnabled\":null,";
        o << "\"micNrPreset\":null,";
        o << "\"reverbLevel\":" << (reverbLevel < 0 ? std::string("null") : Fmt(reverbLevel)) << ",";
        o << "\"noiseGateLevel\":" << (noiseGateLevel < 0 ? std::string("null") : Fmt(noiseGateLevel)) << ",";
        o << "\"speakerEqLevels\":" << (speakerEq9 ? FmtArr9(speakerEq9) : std::string("null")) << ",";
        o << "\"micEqLevels\":" << (micEq9 ? FmtArr9(micEq9) : std::string("null")) << ",";
        o << "\"micHpfEnabled\":null,\"micFrcPreset\":null,\"micFrcEnabled\":null,";
        o << "\"micNrEnabled\":null,\"enableVoiceActivityMonitor\":null,";
        o << "\"enableVoicePeakMonitor\":null,\"resetVoiceActivityMonitor\":null,";
        o << "\"enableAutomaticGainAdjustment\":null}}]}]";
        (void)stereo3d;
        return o.str();
    }

    // ---------------- 完整 APO 参数集（全部字段可独立置 null 表示"不修改"） ----------------
    // 这是官方 requestType=13 的 apoParams 全字段，字段名与官方 JSON 完全一致。
    struct ApoParams {
        const double* speakerEq9 = nullptr;   // 扬声器 9 段 dB
        const double* micEq9     = nullptr;   // 麦克风 9 段 dB
        double reverbLevel       = -1;       // 0..100
        double noiseGateLevel    = -1;       // 0..100
        int    directSound       = -1;       // 0/1
        int    micHpfEnabled     = -1;       // 0/1
        int    micFrcEnabled     = -1;       // 0/1
        int    micFrcPreset      = -1;       // 0..N
        int    micNrEnabled      = -1;       // 0/1
        int    micNrPreset       = -1;       // 0..N
        int    micMetersEnabled  = -1;       // 0/1
        int    autoGainAdjust    = -1;       // 0/1
    };

    static std::string BuildApoFull(const ApoParams& p) {
        std::ostringstream o;
        o << "[{\"requestType\":\"13\",\"SelectedDevice\":[{"
          << DeviceSel() << ",\"DeviceType\":\"BT\",\"apoParams\":{";
        o << "\"directSoundEnabled\":" << (p.directSound      < 0 ? std::string("null") : std::to_string(p.directSound)) << ",";
        o << "\"micMetersEnabled\":"  << (p.micMetersEnabled  < 0 ? std::string("null") : std::to_string(p.micMetersEnabled)) << ",";
        o << "\"micNrPreset\":"       << (p.micNrPreset       < 0 ? std::string("null") : std::to_string(p.micNrPreset)) << ",";
        o << "\"reverbLevel\":"       << (p.reverbLevel       < 0 ? std::string("null") : Fmt(p.reverbLevel)) << ",";
        o << "\"noiseGateLevel\":"    << (p.noiseGateLevel    < 0 ? std::string("null") : Fmt(p.noiseGateLevel)) << ",";
        o << "\"speakerEqLevels\":"   << (p.speakerEq9 ? FmtArr9(p.speakerEq9) : std::string("null")) << ",";
        o << "\"micEqLevels\":"       << (p.micEq9     ? FmtArr9(p.micEq9)     : std::string("null")) << ",";
        o << "\"micHpfEnabled\":"     << (p.micHpfEnabled     < 0 ? std::string("null") : std::to_string(p.micHpfEnabled)) << ",";
        o << "\"micFrcPreset\":"      << (p.micFrcPreset      < 0 ? std::string("null") : std::to_string(p.micFrcPreset)) << ",";
        o << "\"micFrcEnabled\":"     << (p.micFrcEnabled     < 0 ? std::string("null") : std::to_string(p.micFrcEnabled)) << ",";
        o << "\"micNrEnabled\":"      << (p.micNrEnabled      < 0 ? std::string("null") : std::to_string(p.micNrEnabled)) << ",";
        o << "\"enableVoiceActivityMonitor\":null,\"enableVoicePeakMonitor\":null,";
        o << "\"resetVoiceActivityMonitor\":null,";
        o << "\"enableAutomaticGainAdjustment\":"
          << (p.autoGainAdjust < 0 ? std::string("null") : std::to_string(p.autoGainAdjust));
        o << "}}]}]";
        return o.str();
    }

    // ---------------- 侧音 / 音量 / 3D 声场（独立请求，非 apoParams） ----------------
    // 这些走 changedValue，字段名来自官方 UI 服务层：
    //   sideToneValue / SpeakerVolume / MicVolume / currentSurroundButtonAudio3D
    static std::string BuildSideTone(int level) {
        std::ostringstream o;
        o << "[{\"requestType\":\"6\",\"SelectedDevice\":[{"
          << DeviceSel() << ",\"changedValue\":{\"sideToneValue\":"
          << (level < 0 ? std::string("null") : std::to_string(level)) << "}}]}]";
        return o.str();
    }

    static std::string BuildSpeakerVolume(int level) {
        std::ostringstream o;
        o << "[{\"requestType\":\"15\",\"SelectedDevice\":[{"
          << DeviceSel() << ",\"changedValue\":{\"SpeakerVolume\":"
          << (level < 0 ? std::string("null") : std::to_string(level)) << "}}]}]";
        return o.str();
    }

    static std::string BuildMicVolume(int level) {
        std::ostringstream o;
        o << "[{\"requestType\":\"17\",\"SelectedDevice\":[{"
          << DeviceSel() << ",\"changedValue\":{\"MicVolume\":"
          << (level < 0 ? std::string("null") : std::to_string(level)) << "}}]}]";
        return o.str();
    }

    // 3D 声场（环绕声）开关
    static std::string BuildSurround3D(bool on) {
        std::ostringstream o;
        o << "[{\"requestType\":\"10\",\"SelectedDevice\":[{"
          << DeviceSel() << ",\"changedValue\":{\"currentSurroundButtonAudio3D\":"
          << (on ? "1" : "0") << "}}]}]";
        return o.str();
    }

    // XML 滑块值 -> dB
    static double SliderToDb(int slider) { return slider * 6.0 / 127.0; }
    static double SliderToGate(int slider) { return slider * 100.0 / 255.0; }

private:
    static std::string Fmt(double v) {
        char b[40];
        ::snprintf(b, sizeof(b), "%.6f", v);
        return std::string(b);
    }
    static std::string FmtArr9(const double* a) {
        std::string s = "[";
        for (int i = 0; i < 9; i++) { if (i) s += ","; s += Fmt(a[i]); }
        return s + "]";
    }

    void ReaderLoop() {
        // 服务每次写响应都会新建一个客户端连接，所以要循环 accept
        std::vector<char> buf(65536);
        HANDLE evt = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
        OVERLAPPED ov = {};
        ov.hEvent = evt;

        while (!m_stop) {
            BOOL cn = ::ConnectNamedPipe(m_ui, nullptr);
            if (!cn && ::GetLastError() != ERROR_PIPE_CONNECTED) {
                ::Sleep(120);
                continue;
            }
            // 已建立连接，读到断开为止
            while (!m_stop) {
                ::ResetEvent(evt);
                DWORD n = 0;
                BOOL ok = ::ReadFile(m_ui, buf.data(), (DWORD)buf.size(), &n, &ov);
                if (!ok) {
                    DWORD err = ::GetLastError();
                    if (err == ERROR_IO_PENDING) {
                        if (::WaitForSingleObject(evt, 200) == WAIT_TIMEOUT) {
                            ::CancelIoEx(m_ui, &ov);
                            continue;
                        }
                        ok = ::GetOverlappedResult(m_ui, &ov, &n, TRUE);
                    }
                    if (!ok) {
                        if (::GetLastError() == ERROR_BROKEN_PIPE) break;
                        ::Sleep(60);
                        continue;
                    }
                }
                if (!n) continue;
                std::string msg(buf.data(), n);
                // ★ 顺序很重要：先解析出状态，再入队。
                //   反过来的话，Request() 从队列取到消息就返回，
                //   而 m_devices 可能还没更新 → 调用方读到空列表（竞态）。
                if (msg.find("\"messageType\":\"22\"") != std::string::npos) {
                    int v = ExtractInt(msg, "value");
                    if (v >= 0) m_battery = v;
                }
                // 完整设备列表（requestType=1 / 9 的响应）
                if (msg.find("\"DeviceList\"") != std::string::npos) {
                    auto list = eposdev::ParseDeviceList(msg);
                    if (!list.empty()) {
                        bool hs = false, dg = false;
                        for (const auto& d : list) {
                            if (d.deviceType == 3) hs = true;
                            if (d.deviceType == 5) dg = true;
                        }
                        // ★ 防抖：耳机刚还在、这份快照却没有耳机 → 大概率是服务在耳机
                        //   连接/切换过程中的瞬时快照（实测会在 6 秒轮询里来回翻转，
                        //   表现为左侧导航页鬼畜闪烁）。10 秒内的"降级快照"直接丢弃。
                        const DWORD now = ::GetTickCount();
                        if (!hs && m_headsetPresent.load() &&
                            (now - m_lastHeadsetTick) < 10000) {
                            // 丢弃这份快照，保留上一份（含耳机）
                        } else {
                            if (hs) m_lastHeadsetTick = now;
                            m_headsetPresent = hs;
                            m_donglePresent  = dg;
                            std::lock_guard<std::mutex> g(m_mu);
                            m_devices = list;
                        }
                    }
                }
                // ★ 记下未见过的 messageType：智能按钮之类的事件如果走管道，
                //   会以新 messageType 出现（只记一次，避免刷屏）。
                {
                    size_t p = msg.find("\"messageType\":\"");
                    if (p != std::string::npos) {
                        int mt = atoi(msg.c_str() + p + 15);
                        if (mt > 0 && !m_seenTypes.test(mt % 64)) {
                            m_seenTypes.set(mt % 64);
                            extern void PipeNewTypeLog(int);
                            PipeNewTypeLog(mt);
                        }
                    }
                }
                {
                    std::lock_guard<std::mutex> g(m_mu);
                    if (m_queue.size() > 32) m_queue.pop_front();
                    m_queue.push_back(msg);
                }
            }
            ::DisconnectNamedPipe(m_ui);
        }
        if (evt) ::CloseHandle(evt);
    }

    HANDLE m_ui = INVALID_HANDLE_VALUE;
    HANDLE m_agent = INVALID_HANDLE_VALUE;
    std::thread m_thread;
    mutable std::mutex m_mu;
    std::deque<std::string> m_queue;
    std::vector<eposdev::DeviceInfo> m_devices;
    std::atomic<int> m_battery{-1};
    std::atomic<bool> m_headsetPresent{false};
    std::atomic<bool> m_donglePresent{false};
    std::atomic<bool> m_stop{false};
    DWORD m_lastHeadsetTick = 0;      // 最近一次"含耳机"的设备列表时间（防抖用）
    std::bitset<64> m_seenTypes;      // 已见过的 messageType（避免重复打日志）
};

}  // namespace epospipe
