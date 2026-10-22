// 音效控制面板（EQ / 3D 声场 / 侧音 / 混响 / 噪声门）
// 独立模块，绘制与命中测试都在这里，供 main.cpp 调用。
#pragma once
#include <windows.h>
#include <gdiplus.h>
#include <string>
#include <vector>
#include <cmath>
#include "eposfx.h"

using namespace Gdiplus;

namespace epospanel {

// ---- 配色（与主窗口一致）----
static const Color C_BG      (255, 0x15, 0x17, 0x1B);
static const Color C_CARD    (255, 0x20, 0x23, 0x29);
static const Color C_CARD_HI (255, 0x2B, 0x2F, 0x37);
static const Color C_TEXT    (255, 0xE9, 0xED, 0xF1);
static const Color C_DIM     (255, 0x8A, 0x93, 0x9D);
static const Color C_TRACK   (255, 0x2A, 0x2E, 0x35);
static const Color C_OK      (255, 0x3D, 0xC1, 0x7E);
static const Color C_WARN    (255, 0xE0, 0xA4, 0x58);
static const Color C_BLUE    (255, 0x46, 0xA8, 0xE0);
static const Color C_PURPLE  (255, 0x9B, 0x7A, 0xE0);

// ---- 控件 ID ----
enum {
    ID_NONE = -1,
    ID_PRESET_BASE = 1000,   // 1000..1015  预设按钮
    ID_SPEAKER_EQ  = 1100,   // 扬声器 EQ 9 段（拖动）
    ID_MIC_EQ      = 1200,   // 麦克风 EQ 9 段（拖动）
    ID_SURROUND    = 1300,   // 3D 声场开关
    ID_SIDETONE    = 1400,   // 侧音滑块
    ID_REVERB      = 1500,   // 混响滑块
    ID_GATE        = 1600,   // 噪声门滑块
    ID_VOICE       = 1700,   // 语音增强开关
    ID_DIRECT      = 1800,   // 直达声开关
    ID_CAPTURE     = 1900,   // 从官方捕获曲线
    ID_LANG        = 2000,   // 切换中/英文
    ID_SAVE        = 2100,   // 保存为自定义预设
    ID_DELETE      = 2200,   // 删除自定义预设
};

// ---- 面板状态 ----
struct PanelState {
    eposfx::SoundState sound;
    std::vector<eposfx::Preset> presets;
    int  currentPreset = -1;      // 当前选中的预设索引，-1 = 自定义
    bool english = false;          // 界面语言
    bool dragging = false;         // 正在拖动滑块
    int  dragTarget = ID_NONE;     // 正在拖动的控件
    int  dragBand = -1;            // 正在拖动的频段
    bool dirty = false;            // 有未下发的改动
    bool ready = false;            // 管道可用
};

// 界面文案（中英双语）
inline const wchar_t* T(const PanelState& st, const wchar_t* zh, const wchar_t* en) {
    return st.english ? en : zh;
}

// ---- 布局 ----
struct Layout {
    int sw = 0, sh = 0;
    int dpi = 96;
    int S(int v) const { return MulDiv(v, dpi, 96); }

    RectF presetRow;
    RectF speakerCurve;   // 扬声器 EQ 区域
    RectF micCurve;       // 麦克风 EQ 区域
    RectF surroundSw;
    RectF voiceSw;
    RectF directSw;
    RectF sideToneTrack;
    RectF reverbTrack;
    RectF gateTrack;
    RectF captureBtn;
    RectF saveBtn;
    RectF langBtn;
    std::vector<RectF> presetBtns;
};

// 计算布局
inline Layout ComputeLayout(int sw, int sh, int dpi, const PanelState& st) {
    Layout L;
    L.sw = sw; L.sh = sh; L.dpi = dpi;
    const int d = dpi;
    auto SC = [d](int v) { return MulDiv(v, d, 96); };
    int x = SC(16), w = sw - SC(32);
    int y = SC(12);

    // 顶部：预设标题行 + 语言按钮
    L.langBtn = RectF((REAL)(sw - SC(16) - SC(64)), (REAL)y, (REAL)SC(64), (REAL)SC(26));
    y += SC(34);

    // 预设按钮行（横排，最多 4 个 + 自定义）
    L.presetBtns.clear();
    int n = (int)st.presets.size();
    if (n > 5) n = 5;
    int bw = (w - SC(8) * (n - 1)) / (n > 0 ? n : 1);
    for (int i = 0; i < n; i++) {
      L.presetBtns.push_back(RectF((REAL)(x + i * (bw + SC(8))), (REAL)y, (REAL)bw, (REAL)SC(32)));
    }
    y += SC(42);

    // 扬声器 EQ
    L.speakerCurve = RectF((REAL)x, (REAL)y, (REAL)w, (REAL)SC(118));
    y += SC(128);

    // 麦克风 EQ
    L.micCurve = RectF((REAL)x, (REAL)y, (REAL)w, (REAL)SC(92));
    y += SC(102);

    // 开关行：3D 声场 / 语音增强 / 直达声
    int sw3 = (w - SC(12)) / 3;
    L.surroundSw = RectF((REAL)x, (REAL)y, (REAL)sw3, (REAL)SC(50));
    L.voiceSw    = RectF((REAL)(x + sw3 + SC(6)), (REAL)y, (REAL)sw3, (REAL)SC(50));
    L.directSw   = RectF((REAL)(x + 2 * (sw3 + SC(6))), (REAL)y, (REAL)sw3, (REAL)SC(50));
    y += SC(60);

    // 滑块：侧音 / 混响 / 噪声门
    L.sideToneTrack = RectF((REAL)(x + SC(70)), (REAL)y, (REAL)(w - SC(126)), (REAL)SC(22));
    y += SC(32);
    L.reverbTrack   = RectF((REAL)(x + SC(70)), (REAL)y, (REAL)(w - SC(126)), (REAL)SC(22));
    y += SC(32);
    L.gateTrack     = RectF((REAL)(x + SC(70)), (REAL)y, (REAL)(w - SC(126)), (REAL)SC(22));
    y += SC(40);

    // 底部两个按钮：保存 / 捕获
    int halfW = (w - SC(8)) / 2;
    L.saveBtn = RectF((REAL)x, (REAL)y, (REAL)halfW, (REAL)SC(34));
    L.captureBtn = RectF((REAL)(x + halfW + SC(8)), (REAL)y, (REAL)halfW, (REAL)SC(34));
    return L;
}

// ---- 绘制工具 ----
inline void DrawCard(Graphics& g, const RectF& r, bool hi) {
    SolidBrush b(hi ? C_CARD_HI : C_CARD);
    g.FillRectangle(&b, r);
}

inline void DrawSwitch(Graphics& g, const RectF& r, bool on, int dpi) {
    const int dd = dpi;
    auto S = [dd](int v) { return MulDiv(v, dd, 96); };
    REAL tw = (REAL)S(32), th = (REAL)S(18);
    RectF tr(r.X + r.Width - tw - (REAL)S(8), r.Y + r.Height - th - (REAL)S(8), tw, th);
    SolidBrush tb(on ? C_OK : C_TRACK);
    GraphicsPath pill;
    pill.AddArc(tr.X, tr.Y, th, th, 90, 180);
    pill.AddArc(tr.X + tw - th, tr.Y, th, th, 270, 180);
    pill.CloseFigure();
    g.FillPath(&tb, &pill);
    REAL kx = on ? (tr.X + tw - th + (REAL)S(2)) : (tr.X + (REAL)S(2));
    SolidBrush kb(Color(255, 255, 255, 255));
    g.FillEllipse(&kb, kx, tr.Y + (REAL)S(2), th - (REAL)S(4), th - (REAL)S(4));
}

// 水平滑块（轨道 + 填充 + 滑块头 + 数值）
inline void DrawSlider(Graphics& g, const RectF& track, double pct,
                       const wchar_t* label, const wchar_t* valueText, int dpi) {
    const int dd = dpi;
    auto S = [dd](int v) { return MulDiv(v, dd, 96); };
    FontFamily ff(L"Segoe UI");
    Font fL(&ff, (REAL)S(12), FontStyleRegular, UnitPixel);
    Font fV(&ff, (REAL)S(12), FontStyleBold, UnitPixel);
    SolidBrush brD(C_DIM), brT(C_TEXT);

    // 轨道中线
    REAL cy = track.Y + track.Height / 2;
    Pen trk(C_TRACK, (REAL)S(4));
    trk.SetStartCap(LineCapRound); trk.SetEndCap(LineCapRound);
    g.DrawLine(&trk, track.X, cy, track.X + track.Width, cy);

    // 填充
    if (pct > 0) {
        Pen fill(C_BLUE, (REAL)S(4));
        fill.SetStartCap(LineCapRound); fill.SetEndCap(LineCapRound);
        g.DrawLine(&fill, track.X, cy, track.X + (REAL)(track.Width * pct), cy);
    }
    // 滑块头
    REAL hx = track.X + (REAL)(track.Width * pct);
    SolidBrush hb(Color(255, 255, 255, 255));
    g.FillEllipse(&hb, hx - (REAL)S(6), cy - (REAL)S(6), (REAL)S(12), (REAL)S(12));

    if (label) g.DrawString(label, -1, &fL, PointF(track.X - (REAL)S(66), cy - (REAL)S(8)), &brD);
    if (valueText) {
        // 数值画在轨道右侧的固定空位（不与滑块头重叠）
        REAL vx = track.X + track.Width + (REAL)S(8);
        StringFormat sf; sf.SetAlignment(StringAlignmentNear); sf.SetLineAlignment(StringAlignmentCenter);
        g.DrawString(valueText, -1, &fV, PointF(vx, cy - (REAL)S(9)), &sf, &brT);
    }
}

// EQ 曲线图（可拖动的竖条）
inline void DrawEqCurve(Graphics& g, const RectF& area, const double* eq, int dpi,
                         const wchar_t* title, Color barColor, int activeBand) {
    const int dd = dpi;
    auto S = [dd](int v) { return MulDiv(v, dd, 96); };
    FontFamily ff(L"Segoe UI");
    Font fT(&ff, (REAL)S(11), FontStyleBold, UnitPixel);
    SolidBrush brD(C_DIM);

    g.DrawString(title, -1, &fT, PointF(area.X, area.Y), &brD);

    REAL top = area.Y + (REAL)S(20);
    REAL h = area.Height - (REAL)S(38);
    REAL mid = top + h / 2;

    // 中线
    Pen midp(C_TRACK, 1.0f);
    g.DrawLine(&midp, area.X, mid, area.X + area.Width, mid);

    // 上下限刻度
    Font fS(&ff, (REAL)S(9), FontStyleRegular, UnitPixel);
    SolidBrush brS(Color(255, 0x5A, 0x62, 0x6C));
    StringFormat sf; sf.SetAlignment(StringAlignmentFar); sf.SetLineAlignment(StringAlignmentCenter);
    g.DrawString(L"+6", -1, &fS, RectF(area.X - (REAL)S(2), top - (REAL)S(12), (REAL)S(22), (REAL)S(16)), &sf, &brS);
    g.DrawString(L"0",  -1, &fS, RectF(area.X - (REAL)S(2), mid - (REAL)S(8),  (REAL)S(22), (REAL)S(16)), &sf, &brS);
    g.DrawString(L"-6", -1, &fS, RectF(area.X - (REAL)S(2), top + h - (REAL)S(4), (REAL)S(22), (REAL)S(16)), &sf, &brS);

    // 9 根柱
    REAL plotX = area.X + (REAL)S(26);
    REAL plotW = area.Width - (REAL)S(30);
    REAL bandW = plotW / 9.0f;
    for (int i = 0; i < 9; i++) {
        REAL cx = plotX + bandW * (i + 0.5f);
        double db = eq[i];
        REAL bh = (REAL)(std::fabs(db) / 6.0 * (h / 2 - (REAL)S(4)));
        REAL bx = cx - bandW * 0.28f;
        REAL bw2 = bandW * 0.56f;
        if (bh < (REAL)S(1)) bh = (REAL)S(1);

        Color bc = barColor;
        if (i == activeBand) bc = Color(255, 255, 255, 255);
        SolidBrush bb(bc);
        if (db >= 0) {
            g.FillRectangle(&bb, bx, mid - bh, bw2, bh);
        } else {
            g.FillRectangle(&bb, bx, mid, bw2, bh);
        }
        // 0 dB 时柱高只有 1px 几乎看不见 -> 补一条基线小方块
        if (bh < (REAL)S(3)) {
            SolidBrush nb(bc);
            g.FillRectangle(&nb, bx, mid - (REAL)S(1), bw2, (REAL)S(2));
        }
        // 频率标签
        static const wchar_t* kFreq[9] = {
            L"32", L"64", L"125", L"250", L"500", L"1K", L"2K", L"4K", L"8K"
        };
        Font fF(&ff, (REAL)S(9), FontStyleRegular, UnitPixel);
        StringFormat cf; cf.SetAlignment(StringAlignmentCenter);
        g.DrawString(kFreq[i], -1, &fF,
                     RectF(cx - bandW / 2, top + h + (REAL)S(6), bandW, (REAL)S(14)), &cf, &brS);
    }
}

// ---- 主绘制 ----
inline void Draw(Graphics& g, const PanelState& st, const Layout& L) {
    int dpi = L.dpi;
    const int d2 = dpi;
    auto S = [d2](int v) { return MulDiv(v, d2, 96); };
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

    FontFamily ff(L"Segoe UI");
    Font fT(&ff, (REAL)S(12), FontStyleBold, UnitPixel);
    Font fSm(&ff, (REAL)S(11), FontStyleRegular, UnitPixel);
    SolidBrush brT(C_TEXT), brD(C_DIM), brW(Color(255, 255, 255, 255));

    // 语言切换按钮
    {
        DrawCard(g, L.langBtn, false);
        const wchar_t* lt = st.english ? L"中文" : L"English";
        StringFormat sf; sf.SetAlignment(StringAlignmentCenter); sf.SetLineAlignment(StringAlignmentCenter);
        RectF lb; g.MeasureString(lt, -1, &fSm, L.langBtn, &sf, &lb);
        PointF lo(L.langBtn.X + (L.langBtn.Width - lb.Width) / 2.0f,
                  L.langBtn.Y + (L.langBtn.Height - lb.Height) / 2.0f);
        g.DrawString(lt, -1, &fSm, lo, (Brush*)&brD);
    }

    // 预设按钮
    for (size_t i = 0; i < L.presetBtns.size(); i++) {
        const RectF& b = L.presetBtns[i];
        bool sel = ((int)i == st.currentPreset);
        DrawCard(g, b, sel);
        if (sel) {
            SolidBrush ac(C_BLUE);
            g.FillRectangle(&ac, b.X, b.Y, (REAL)S(3), b.Height);
        }
        StringFormat sf; sf.SetAlignment(StringAlignmentCenter); sf.SetLineAlignment(StringAlignmentCenter);
        const eposfx::Preset& p = st.presets[i];
        const wchar_t* nm = st.english ? p.en.c_str() : p.zh.c_str();
        // GDI+ 的 DrawString 无 RectF 重载：用 PointF + 居中偏移
        REAL tw = (REAL)b.Width * 0.5f;
        REAL th = (REAL)b.Height * 0.5f;
        StringFormat csf; csf.SetAlignment(StringAlignmentCenter); csf.SetLineAlignment(StringAlignmentCenter);
        RectF tb(b.X, b.Y, b.Width, b.Height);
        // 用 MeasureString 精确居中
        RectF bounds;
        g.MeasureString(nm, -1, &fT, tb, &csf, &bounds);
        PointF org(b.X + (b.Width - bounds.Width) / 2.0f,
                   b.Y + (b.Height - bounds.Height) / 2.0f);
        g.DrawString(nm, -1, &fT, org, sel ? (Brush*)&brW : (Brush*)&brD);
    }

    // EQ 曲线
    DrawEqCurve(g, L.speakerCurve, st.sound.speakerEq, dpi,
                T(st, L"扬声器均衡器", L"Speaker EQ"), C_BLUE,
                st.dragTarget == ID_SPEAKER_EQ ? st.dragBand : -1);
    DrawEqCurve(g, L.micCurve, st.sound.micEq, dpi,
                T(st, L"麦克风均衡器", L"Microphone EQ"), C_PURPLE,
                st.dragTarget == ID_MIC_EQ ? st.dragBand : -1);

    // 三个开关
    struct { const RectF* r; bool on; const wchar_t* zh; const wchar_t* en; } sws[3] = {
        { &L.surroundSw, st.sound.surround3D,   L"3D 声场",   L"3D Surround" },
        { &L.voiceSw,    st.sound.voiceEnhance, L"语音增强", L"Voice Enhance" },
        { &L.directSw,   st.sound.directSound,  L"直达声",   L"Direct Sound" },
    };
    for (auto& s : sws) {
        DrawCard(g, *s.r, false);
        const wchar_t* lbl = T(st, s.zh, s.en);
        // 英文标签较长 -> 自动换行显示在开关上方，避免与开关重叠
        StringFormat sf;
        sf.SetAlignment(StringAlignmentNear);
        sf.SetLineAlignment(StringAlignmentNear);
        RectF lr(s.r->X + (REAL)S(9), s.r->Y + (REAL)S(7),
                 s.r->Width - (REAL)S(18), s.r->Height - (REAL)S(28));
        g.DrawString(lbl, -1, &fSm, lr, &sf, (Brush*)&brT);
        DrawSwitch(g, *s.r, s.on, dpi);
    }

    // 滑块
    wchar_t buf[32];
    swprintf(buf, 32, L"%d%%", (int)st.sound.noiseGate);
    DrawSlider(g, L.gateTrack, st.sound.noiseGate / 100.0,
               T(st, L"噪声门", L"Gate"), buf, dpi);

    swprintf(buf, 32, L"%d%%", (int)st.sound.reverb);
    DrawSlider(g, L.reverbTrack, st.sound.reverb / 100.0,
               T(st, L"混响", L"Reverb"), buf, dpi);

    swprintf(buf, 32, L"%d", (int)st.sound.sideToneLevel);
    DrawSlider(g, L.sideToneTrack, std::min(1.0, st.sound.sideToneLevel / 20.0),
               T(st, L"侧音", L"Sidetone"), buf, dpi);

    // 保存为自定义预设
    DrawCard(g, L.saveBtn, false);
    const wchar_t* sav = T(st, L"保存为自定义预设", L"Save as custom preset");
    StringFormat ssf; ssf.SetAlignment(StringAlignmentCenter); ssf.SetLineAlignment(StringAlignmentCenter);
    RectF sb; g.MeasureString(sav, -1, &fSm, L.saveBtn, &ssf, &sb);
    PointF so(L.saveBtn.X + (L.saveBtn.Width - sb.Width) / 2.0f,
              L.saveBtn.Y + (L.saveBtn.Height - sb.Height) / 2.0f);
    g.DrawString(sav, -1, &fSm, so, (Brush*)&brD);

    // 捕获按钮
    DrawCard(g, L.captureBtn, false);
    const wchar_t* cap = T(st, L"从官方 Gaming Suite 捕获当前曲线",
                            L"Capture current curve from Gaming Suite");
    StringFormat sf; sf.SetAlignment(StringAlignmentCenter); sf.SetLineAlignment(StringAlignmentCenter);
    RectF cb; g.MeasureString(cap, -1, &fSm, L.captureBtn, &sf, &cb);
    PointF co(L.captureBtn.X + (L.captureBtn.Width - cb.Width) / 2.0f,
              L.captureBtn.Y + (L.captureBtn.Height - cb.Height) / 2.0f);
    g.DrawString(cap, -1, &fSm, co, (Brush*)&brD);
}

// ---- 命中测试 ----
inline bool InRect(const RectF& r, POINT p) {
    return p.x >= r.X && p.x <= r.X + r.Width && p.y >= r.Y && p.y <= r.Y + r.Height;
}

// 返回控件 ID；bandIndex 输出频段序号
inline int HitTest(const Layout& L, POINT p, int* bandIndex = nullptr) {
    if (bandIndex) *bandIndex = -1;
    if (InRect(L.langBtn, p)) return ID_LANG;
    for (size_t i = 0; i < L.presetBtns.size(); i++)
        if (InRect(L.presetBtns[i], p)) return ID_PRESET_BASE + (int)i;
    if (InRect(L.surroundSw, p)) return ID_SURROUND;
    if (InRect(L.voiceSw, p))    return ID_VOICE;
    if (InRect(L.directSw, p))   return ID_DIRECT;
    if (InRect(L.sideToneTrack, p)) return ID_SIDETONE;
    if (InRect(L.reverbTrack, p))   return ID_REVERB;
    if (InRect(L.gateTrack, p))     return ID_GATE;
    if (InRect(L.captureBtn, p))   return ID_CAPTURE;
    if (InRect(L.saveBtn, p))      return ID_SAVE;

    // EQ 柱：返回 ID + 频段
    auto hitCurve = [&](const RectF& area, int id) {
        if (!InRect(RectF(area.X, area.Y + (REAL)L.S(16), area.Width, area.Height - (REAL)L.S(16)), p))
            return false;
        REAL plotX = area.X + (REAL)L.S(26);
        REAL plotW = area.Width - (REAL)L.S(30);
        REAL bandW = plotW / 9.0f;
        int idx = (int)((p.x - plotX) / bandW);
        if (idx < 0) idx = 0;
        if (idx > 8) idx = 8;
        if (bandIndex) *bandIndex = idx;
        return true;
    };
    int b = -1;
    if (hitCurve(L.speakerCurve, ID_SPEAKER_EQ)) return ID_SPEAKER_EQ;
    if (hitCurve(L.micCurve, ID_MIC_EQ))         return ID_MIC_EQ;
    (void)b;
    return ID_NONE;
}

// ---- 拖动计算 ----
// 返回新的 dB 值（-6..+6）
inline double DbFromPoint(const RectF& area, POINT p, int dpi) {
    REAL top = area.Y + (REAL)MulDiv(20, dpi, 96);
    REAL h = area.Height - (REAL)MulDiv(34, dpi, 96);
    REAL mid = top + h / 2;
    double frac = (double)(mid - p.y) / (double)(h / 2);
    if (frac >  1.0) frac =  1.0;
    if (frac < -1.0) frac = -1.0;
    return frac * 6.0;
}

// 滑块百分比
inline double PctFromPoint(const RectF& track, POINT p) {
    REAL rel = p.x - track.X;
    double pct = (double)rel / (double)track.Width;
    if (pct < 0) pct = 0;
    if (pct > 1) pct = 1;
    return pct;
}

}  // namespace epospanel
