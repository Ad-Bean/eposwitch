// 音量 OSD（右上/右下角浮动提示）
//
// 为什么需要它：
//   耳机自带的音量轮在 dongle 模式下不发可读的 HID 报告（报告被 hidconsumer
//   驱动先读先得抢走，见技术笔记第二十轮），所以"读 HID"这条路走不通。
//   但实测发现音量轮确实会改 Windows 主音量 —— 那就换个方向：
//   直接挂 IAudioEndpointVolume::RegisterControlChangeNotify，
//   只要系统音量变了就弹一条提示。这样任何耳机/音箱、任何调音量方式
//   （音量轮 / 键盘媒体键 / 面板滑块 / 系统音量合成器）都能覆盖。
//
// 渲染：WS_EX_LAYERED 窗口 + UpdateLayeredWindow（逐像素 alpha），
//       GDI+ 画圆角卡片，和主界面同一套配色。
#pragma once
#include <windows.h>
#include <gdiplus.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <string>
#include <cwchar>

namespace volosd {

using namespace Gdiplus;

inline HWND  g_wnd = nullptr;
inline UINT  g_dpi = 96;
inline int   g_lastVol = -1;
inline bool  g_lastMute = false;
inline std::wstring g_devName;

// ---- 小工具 ----
inline int VS(int v) { return MulDiv(v, (int)g_dpi, 96); }

inline float VSF(float v) { return v * (float)g_dpi / 96.0f; }

inline void RoundedPath(GraphicsPath& p, RectF r, float rad) {
  float d = rad * 2.0f;
  if (d > r.Width)  d = r.Width;
  if (d > r.Height) d = r.Height;
  p.Reset();
  p.AddArc(r.X, r.Y, d, d, 180.0f, 90.0f);
  p.AddArc(r.X + r.Width - d, r.Y, d, d, 270.0f, 90.0f);
  p.AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0.0f, 90.0f);
  p.AddArc(r.X, r.Y + r.Height - d, d, d, 90.0f, 90.0f);
  p.CloseFigure();
}

// 喇叭图标（几何画，不依赖字体里的 emoji）
inline void DrawSpeaker(Graphics& g, float cx, float cy, float sz, bool mute, Color col) {
  GraphicsPath p;
  float w = sz, h = sz;
  float x = cx - w * 0.5f, y = cy - h * 0.5f;
  // 箱体（左侧小方块）+ 喇叭口（右侧梯形）
  p.AddLine(x + w * 0.06f, y + h * 0.34f, x + w * 0.30f, y + h * 0.34f);
  p.AddLine(x + w * 0.30f, y + h * 0.34f, x + w * 0.60f, y + h * 0.10f);
  p.AddLine(x + w * 0.60f, y + h * 0.10f, x + w * 0.60f, y + h * 0.90f);
  p.AddLine(x + w * 0.60f, y + h * 0.90f, x + w * 0.30f, y + h * 0.66f);
  p.AddLine(x + w * 0.30f, y + h * 0.66f, x + w * 0.06f, y + h * 0.66f);
  p.CloseFigure();
  SolidBrush b(col);
  g.FillPath(&b, &p);

  if (mute) {
    Pen slash(col, VSF(2.0f));
    slash.SetStartCap(LineCapRound);
    slash.SetEndCap(LineCapRound);
    g.DrawLine(&slash, x + w * 0.62f, y + h * 0.20f, x + w * 0.95f, y + h * 0.80f);
  } else {
    Pen pen(col, VSF(1.9f));
    pen.SetStartCap(LineCapRound);
    pen.SetEndCap(LineCapRound);
    g.DrawArc(&pen, x + w * 0.48f, y + h * 0.24f, w * 0.34f, h * 0.52f, -62.0f, 124.0f);
    g.DrawArc(&pen, x + w * 0.42f, y + h * 0.08f, w * 0.62f, h * 0.84f, -58.0f, 116.0f);
  }
}

// ---- 绘制 ----
inline void Paint(int vol, bool mute) {
  if (!g_wnd) return;
  const int W = VS(380), H = VS(88);

  HDC sdc = GetDC(nullptr);
  HDC mdc = CreateCompatibleDC(sdc);
  BITMAPINFO bi = {};
  bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bi.bmiHeader.biWidth = W;
  bi.bmiHeader.biHeight = -H;          // top-down
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;
  void* bits = nullptr;
  HBITMAP dib = CreateDIBSection(mdc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
  if (!dib) { DeleteDC(mdc); ReleaseDC(nullptr, sdc); return; }
  HGDIOBJ old = SelectObject(mdc, dib);

  {
    Graphics g(mdc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(PixelOffsetModeHalf);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
    g.Clear(Color(0, 0, 0, 0));

    // 卡片
    GraphicsPath card;
    RoundedPath(card, RectF(0.5f, 0.5f, (float)W - 1, (float)H - 1), VSF(14.0f));
    SolidBrush bg(Color(244, 26, 29, 34));
    g.FillPath(&bg, &card);
    Pen edge(Color(255, 52, 58, 70), VSF(1.0f));
    g.DrawPath(&edge, &card);

    // 字体（Noto Sans SC 与主界面一致，缺失时退回雅黑）
    FontFamily* ff = new FontFamily(L"Noto Sans SC");
    if (!ff->IsAvailable()) { delete ff; ff = new FontFamily(L"Microsoft YaHei UI"); }

    const Color cText(255, 236, 239, 244);
    const Color cDim(255, 170, 180, 192);
    const Color cTrack(255, 44, 49, 60);
    const Color cBlue(255, 58, 134, 255);
    const Color cMute(255, 120, 128, 140);

    // 设备名
    {
      Font fName(ff, VSF(12.5f), FontStyleRegular, UnitPixel);
      StringFormat sf;
      sf.SetTrimming(StringTrimmingEllipsisCharacter);
      sf.SetFormatFlags(StringFormatFlagsNoWrap);
      RectF r(VSF(62), VSF(13), VSF(232), VSF(20));
      SolidBrush b(cDim);
      g.DrawString(g_devName.c_str(), -1, &fName, r, &sf, &b);
    }

    // 百分比
    {
      Font fPct(ff, VSF(19.0f), FontStyleBold, UnitPixel);
      StringFormat sf;
      sf.SetAlignment(StringAlignmentFar);
      sf.SetFormatFlags(StringFormatFlagsNoWrap);
      wchar_t buf[32];
      if (mute) wcscpy(buf, L"静音");
      else swprintf(buf, 32, L"%d%%", vol);
      Font fPctSmall(ff, VSF(14.0f), FontStyleBold, UnitPixel);
      Font& fUse = mute ? fPctSmall : fPct;
      RectF r(VSF(280), mute ? VSF(15) : VSF(9), VSF(80), VSF(26));
      SolidBrush b(mute ? cMute : cText);
      g.DrawString(buf, -1, &fUse, r, &sf, &b);
    }

    // 喇叭图标
    DrawSpeaker(g, VSF(38.0f), VSF(50.0f), VSF(24.0f), mute, mute ? cMute : cDim);

    // 进度条
    {
      float bx = VSF(62.0f), by = VSF(50.0f);
      float bw = (float)W - bx - VSF(20.0f), bh = VSF(7.0f);
      GraphicsPath tr;
      RoundedPath(tr, RectF(bx, by, bw, bh), bh * 0.5f);
      SolidBrush tb(cTrack);
      g.FillPath(&tb, &tr);

      // 静音时也保留真实比例（灰显），画成满格会让人误以为是 100%
      float ratio = (vol < 0 ? 0.0f : vol / 100.0f);
      if (ratio > 0.0f) {
        float fw = bw * ratio;
        if (fw < bh) fw = bh;               // 极小音量也保留一个圆头
        GraphicsPath fp;
        RoundedPath(fp, RectF(bx, by, fw, bh), bh * 0.5f);
        SolidBrush fb(mute ? cMute : cBlue);
        g.FillPath(&fb, &fp);
      }
    }
    delete ff;
  }

  // 定位：光标所在显示器的工作区右下角
  POINT cur = {0, 0};
  GetCursorPos(&cur);
  HMONITOR mon = MonitorFromPoint(cur, MONITOR_DEFAULTTONEAREST);
  MONITORINFO mi = {sizeof(mi)};
  GetMonitorInfoW(mon, &mi);
  int px = mi.rcWork.right - W - VS(24);
  int py = mi.rcWork.bottom - H - VS(24);

  POINT dst = {px, py}, src = {0, 0};
  SIZE sz = {W, H};
  BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
  UpdateLayeredWindow(g_wnd, sdc, &dst, &sz, mdc, &src, 0, &bf, ULW_ALPHA);

  SelectObject(mdc, old);
  DeleteObject(dib);
  DeleteDC(mdc);
  ReleaseDC(nullptr, sdc);
}

inline LRESULT CALLBACK OsdProc(HWND h, UINT m, WPARAM w, LPARAM l) {
  if (m == WM_TIMER && w == 1) {
    KillTimer(h, 1);
    ShowWindow(h, SW_HIDE);
    return 0;
  }
  return DefWindowProcW(h, m, w, l);
}

inline void EnsureWindow() {
  if (g_wnd) return;
  HDC sdc = GetDC(nullptr);
  g_dpi = (UINT)GetDeviceCaps(sdc, LOGPIXELSY);
  ReleaseDC(nullptr, sdc);

  WNDCLASSEXW wc = {sizeof(wc)};
  wc.lpfnWndProc = OsdProc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = L"EposwitchVolOsd";
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  RegisterClassExW(&wc);   // 重复注册会失败，忽略即可

  g_wnd = CreateWindowExW(
      WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE |
          WS_EX_TRANSPARENT,
      L"EposwitchVolOsd", L"", WS_POPUP,
      0, 0, VS(380), VS(88), nullptr, nullptr, wc.hInstance, nullptr);
}

// 设备名瘦身：Windows 的端点名常见形式是 "Speakers (EPOS H3PRO Dongle)"，
// 通用前缀占了大半宽度、信息量为零 —— 只保留括号里的真名。
inline std::wstring ShortDevName(const std::wstring& n) {
  size_t lp = n.find(L'('), rp = n.rfind(L')');
  if (lp == std::wstring::npos || rp == std::wstring::npos || rp <= lp + 1) return n;
  std::wstring inner = n.substr(lp + 1, rp - lp - 1);
  std::wstring outer = n.substr(0, lp);
  while (!outer.empty() && (outer.back() == L' ' || outer.back() == L'\t')) outer.pop_back();
  static const wchar_t* kGeneric[] = {
      L"Speakers", L"Speaker", L"Headphones", L"Headphone",
      L"扬声器", L"耳机", L"喇叭"};
  for (auto* g : kGeneric)
    if (_wcsicmp(outer.c_str(), g) == 0) return inner;
  return n;
}

// 显示一条音量提示（2 秒后自动隐藏；连续变化会重置计时）
inline void Show(int vol, bool mute, const std::wstring& devName) {
  EnsureWindow();
  if (!g_wnd) return;
  if (!devName.empty()) g_devName = ShortDevName(devName);
  g_lastVol = vol;
  g_lastMute = mute;
  Paint(vol, mute);
  SetWindowPos(g_wnd, HWND_TOPMOST, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
  ShowWindow(g_wnd, SW_SHOWNOACTIVATE);
  // 分层窗口必须重新贴一次，否则偶发闪一下全透明
  SetTimer(g_wnd, 1, 2000, nullptr);
}

// ---- 音量变化回调 ----
inline HWND g_notifyTarget = nullptr;
inline const UINT WM_VOLOSD_MSG = WM_APP + 3;

class VolNotify : public IAudioEndpointVolumeCallback {
 public:
  LONG ref = 1;
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
    if (!ppv) return E_POINTER;
    if (riid == __uuidof(IUnknown) || riid == __uuidof(IAudioEndpointVolumeCallback)) {
      *ppv = static_cast<IAudioEndpointVolumeCallback*>(this);
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
  // 注意：这个回调在音频系统线程上，不能直接画窗口 —— 投递给主窗口
  HRESULT STDMETHODCALLTYPE OnNotify(PAUDIO_VOLUME_NOTIFICATION_DATA d) override {
    if (d && g_notifyTarget) {
      int v = (int)(d->fMasterVolume * 100.0f + 0.5f);
      PostMessageW(g_notifyTarget, WM_VOLOSD_MSG, (WPARAM)v,
                   (LPARAM)(d->bMuted ? 1 : 0));
    }
    return S_OK;
  }
};

inline IAudioEndpointVolume* g_epVol = nullptr;
inline VolNotify* g_volNotify = nullptr;

// 挂到当前默认输出端点；默认设备变了要重新挂
// 返回 true = 回调注册成功
inline bool Attach(HWND notifyTarget) {
  g_notifyTarget = notifyTarget;
  if (g_epVol && g_volNotify) {
    g_epVol->UnregisterControlChangeNotify(g_volNotify);
    g_volNotify->Release();
    g_volNotify = nullptr;
    g_epVol->Release();
    g_epVol = nullptr;
  }
  IMMDeviceEnumerator* en = nullptr;
  if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                              __uuidof(IMMDeviceEnumerator), (void**)&en)) || !en)
    return false;
  IMMDevice* dev = nullptr;
  if (SUCCEEDED(en->GetDefaultAudioEndpoint(eRender, eConsole, &dev)) && dev) {
    if (SUCCEEDED(dev->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr,
                                (void**)&g_epVol)) && g_epVol) {
      g_volNotify = new VolNotify();
      if (FAILED(g_epVol->RegisterControlChangeNotify(g_volNotify))) {
        g_volNotify->Release();
        g_volNotify = nullptr;
      }
    }
    dev->Release();
  }
  en->Release();
  return g_volNotify != nullptr;
}

inline void Detach() {
  if (g_epVol && g_volNotify) {
    g_epVol->UnregisterControlChangeNotify(g_volNotify);
    g_volNotify->Release();
  }
  g_volNotify = nullptr;
  if (g_epVol) g_epVol->Release();
  g_epVol = nullptr;
}

}  // namespace volosd
