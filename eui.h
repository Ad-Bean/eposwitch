// 新版 UI 基础库：主题、分段控件、卡片、滚动容器、导航栏
// 参考 SteelSeries GG / Logitech G Hub / Razer Synapse 的布局逻辑。
#pragma once
#include <windows.h>
#include <gdiplus.h>
#include <string>
#include <vector>
#include <cmath>
#include <algorithm>

using namespace Gdiplus;

namespace eui {

// ---------------- 主题 ----------------
struct Theme {
    Color bg          {255, 0x14, 0x16, 0x1A};
    Color sidebar     {255, 0x1A, 0x1D, 0x22};
    Color surface     {255, 0x20, 0x24, 0x2A};
    Color surfaceHi   {255, 0x2A, 0x2F, 0x37};
    Color border      {255, 0x2E, 0x33, 0x3B};
    Color borderHi    {255, 0x3C, 0x43, 0x4D};
    Color text        {255, 0xE8, 0xEC, 0xF1};
    Color textDim     {255, 0x8C, 0x95, 0xA1};
    Color textFaint   {255, 0x5C, 0x64, 0x6E};
    Color accent      {255, 0x4C, 0x9B, 0xE8};
    Color accentSoft  {255, 0x24, 0x33, 0x45};
    Color success     {255, 0x3D, 0xC1, 0x7E};
    Color warn        {255, 0xE0, 0xA4, 0x58};
    Color danger      {255, 0xD4, 0x69, 0x5F};
    Color dangerSoft  {255, 0x4A, 0x25, 0x26};   // 危险色的低饱和底色（静音按钮激活态）
    Color purple      {255, 0x9B, 0x7A, 0xE0};
    Color teal        {255, 0x3D, 0xB8, 0xC4};
};

inline const Theme& T() { static Theme t; return t; }

// ---------------- 字体 ----------------
// 选型依据（实测，见 font_compare.png / bold_test.exe）：
//   Segoe UI Variable + 中文 -> 中文走字体链接回退到雅黑，中英字重明显不一致（丑）
//   MiSans                   -> 中英一致性最好，但只有 Regular，加粗仅 +33%（合成不足，标题立不住）
//   Noto Sans SC             -> 中英同族、真加粗（+53%），小字号笔画干净 -> 首选
// 逐级回退，保证在没装 Noto 的机器上也拿得到合理字体。
inline FontFamily& FF() {
    static FontFamily* s_pick = nullptr;
    if (!s_pick) {
        static const wchar_t* kCands[] = {
            L"Noto Sans SC",
            L"MiSans",
            L"Segoe UI Variable Text",
            L"Microsoft YaHei UI",
            L"Segoe UI",
        };
        for (const wchar_t* n : kCands) {
            FontFamily* f = new FontFamily(n);
            if (f->GetLastStatus() == Ok) { s_pick = f; break; }
            delete f;
        }
        if (!s_pick) s_pick = new FontFamily(L"Segoe UI");
    }
    return *s_pick;
}

// 当前选中的字族名（启动时打一次日志，便于用户反馈问题时定位）
inline const wchar_t* FFName() {
    static std::wstring s_name;
    if (s_name.empty()) {
        WCHAR buf[LF_FACESIZE] = {0};
        if (FF().GetFamilyName(buf) == Ok) s_name = buf;
    }
    return s_name.c_str();
}

inline Font MakeF(int px, bool bold = false) {
    // 最小可读字号：低于 11px 在深色底上笔画会散，这里兜一下
    if (px < 11) px = 11;
    return Font(&FF(), (REAL)px, bold ? FontStyleBold : FontStyleRegular, UnitPixel);
}

// ---------------- 圆角矩形填充 ----------------
inline void FillRound(Graphics& g, const RectF& r, REAL radius, const Color& c) {
    if (r.Width <= 0 || r.Height <= 0) return;
    GraphicsPath p;
    REAL d = radius * 2;
    p.AddArc(r.X, r.Y, d, d, 180, 90);
    p.AddArc(r.X + r.Width - d, r.Y, d, d, 270, 90);
    p.AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0, 90);
    p.AddArc(r.X, r.Y + r.Height - d, d, d, 90, 90);
    p.CloseFigure();
    SolidBrush b(c);
    g.FillPath(&b, &p);
}

inline void StrokeRound(Graphics& g, const RectF& r, REAL radius, const Color& c, REAL w = 1.0f) {
    if (r.Width <= 0 || r.Height <= 0) return;
    GraphicsPath p;
    REAL d = radius * 2;
    p.AddArc(r.X, r.Y, d, d, 180, 90);
    p.AddArc(r.X + r.Width - d, r.Y, d, d, 270, 90);
    p.AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0, 90);
    p.AddArc(r.X, r.Y + r.Height - d, d, d, 90, 90);
    p.CloseFigure();
    Pen pen(c, w);
    g.DrawPath(&pen, &p);
}

// ---------------- 文字 ----------------
// ★ 居中不要"MeasureString 再算偏移" —— 那样会和 LineAlignment 叠加，导致文字偏移。
//   直接把 RectF 交给 DrawString + StringFormat，由 GDI+ 负责居中。
inline void TextCenter(Graphics& g, const RectF& r, const wchar_t* s,
                       const Font& f, const Color& c) {
    StringFormat sf;
    sf.SetAlignment(StringAlignmentCenter);
    sf.SetLineAlignment(StringAlignmentCenter);
    sf.SetFormatFlags(StringFormatFlagsNoWrap);
    sf.SetTrimming(StringTrimmingEllipsisCharacter);
    SolidBrush b(c);
    g.DrawString(s, -1, &f, r, &sf, &b);
}

inline void TextLeft(Graphics& g, REAL x, REAL y, const wchar_t* s,
                     const Font& f, const Color& c) {
    StringFormat sf;
    sf.SetAlignment(StringAlignmentNear);
    sf.SetLineAlignment(StringAlignmentNear);
    sf.SetFormatFlags(StringFormatFlagsNoWrap);
    SolidBrush b(c);
    g.DrawString(s, -1, &f, PointF(x, y), &sf, &b);
}

// 在一条带内垂直居中、水平靠左（比 TextLeft 更稳，不依赖字体 ascent）
inline void TextLeftVC(Graphics& g, const RectF& band, const wchar_t* s,
                       const Font& f, const Color& c, REAL padX = 0) {
    StringFormat sf;
    sf.SetAlignment(StringAlignmentNear);
    sf.SetLineAlignment(StringAlignmentCenter);
    sf.SetFormatFlags(StringFormatFlagsNoWrap);
    sf.SetTrimming(StringTrimmingEllipsisCharacter);
    SolidBrush b(c);
    RectF r(band.X + padX, band.Y, band.Width - padX, band.Height);
    g.DrawString(s, -1, &f, r, &sf, &b);
}

// 在一条带内垂直居中、水平靠右
inline void TextRightVC(Graphics& g, const RectF& band, const wchar_t* s,
                        const Font& f, const Color& c, REAL padX = 0) {
    StringFormat sf;
    sf.SetAlignment(StringAlignmentFar);
    sf.SetLineAlignment(StringAlignmentCenter);
    sf.SetFormatFlags(StringFormatFlagsNoWrap);
    sf.SetTrimming(StringTrimmingEllipsisCharacter);
    SolidBrush b(c);
    RectF r(band.X, band.Y, band.Width - padX, band.Height);
    g.DrawString(s, -1, &f, r, &sf, &b);
}

// ---------------- 矢量图标（替代"EP"/"BT"/"PC"这种文字占位）----------------
enum IconId {
    ICON_NONE = 0,
    ICON_HEADSET,   // 耳机
    ICON_SPEAKER,   // 扬声器
    ICON_MIC,       // 麦克风
    ICON_BT,        // 蓝牙
    ICON_CHIP,      // 主板声卡
    ICON_MUSIC,     // 其它/音乐
    ICON_VOLUME,    // 音量
    ICON_MUTE,      // 静音
    ICON_EQ,        // 均衡器
    ICON_PRESET,    // 预设
    ICON_LANG,      // 语言
    ICON_BATTERY,   // 电量
    ICON_CHEVRON_DOWN, // 下拉箭头
    ICON_CLOSE,       // 关闭（X）
    ICON_SLIDERS      // 设置（三条带滑块的横线）
};

// 在 r 内绘制矢量图标；c 为线条/填充色
inline void DrawIcon(Graphics& g, const RectF& r, IconId id, const Color& c) {
    SolidBrush b(c);
    // 笔宽随图标尺寸自适应，否则 16~18px 时线条糊成一坨。
    // 下限提到 1.35px：1.0px 的线在深色底上会被抗锯齿打散成灰色，看着像没画完。
    REAL lw = r.Width * 0.105f;
    if (lw < 1.35f) lw = 1.35f;
    if (lw > 2.2f) lw = 2.2f;
    Pen pen(c, lw);
    pen.SetStartCap(LineCapRound);
    pen.SetEndCap(LineCapRound);
    pen.SetLineJoin(LineJoinRound);
    REAL cx = r.X + r.Width / 2, cy = r.Y + r.Height / 2;
    REAL w = r.Width, h = r.Height;

    switch (id) {
    case ICON_SLIDERS: {
        // 三条横线 + 错落的滑块圆点（现代设置图标）
        REAL yy[3] = { cy - h * 0.28f, cy, cy + h * 0.28f };
        REAL kx[3] = { cx - w * 0.16f, cx + w * 0.18f, cx - w * 0.22f };
        for (int i = 0; i < 3; i++) {
            Pen ln(c, lw * 0.95f);
            ln.SetStartCap(LineCapRound);
            ln.SetEndCap(LineCapRound);
            g.DrawLine(&ln, r.X + w * 0.14f, yy[i], r.X + w * 0.86f, yy[i]);
            g.FillEllipse(&b, kx[i] - w * 0.09f, yy[i] - w * 0.09f,
                          w * 0.18f, w * 0.18f);
        }
        break;
    }
    case ICON_CLOSE: {
        g.DrawLine(&pen, cx - w * 0.28f, cy - h * 0.28f, cx + w * 0.28f, cy + h * 0.28f);
        g.DrawLine(&pen, cx + w * 0.28f, cy - h * 0.28f, cx - w * 0.28f, cy + h * 0.28f);
        break;
    }
    case ICON_CHEVRON_DOWN: {
        // 两笔 V 形，落在视觉中心偏下一点（和文字基线对齐更自然）
        GraphicsPath p;
        p.AddLine(cx - w * 0.30f, cy - h * 0.10f, cx, cy + h * 0.18f);
        p.AddLine(cx, cy + h * 0.18f, cx + w * 0.30f, cy - h * 0.10f);
        g.DrawPath(&pen, &p);
        break;
    }
    case ICON_HEADSET: {
        // 头梁弧 + 两侧耳罩（耳罩收窄，避免 18px 时糊成一块）
        GraphicsPath p;
        p.AddArc(r.X + w * 0.14f, r.Y + h * 0.16f, w * 0.72f, h * 0.68f, 180, 180);
        g.DrawPath(&pen, &p);
        REAL ew = w * 0.17f, eh = h * 0.30f;
        FillRound(g, RectF(r.X + w * 0.05f, cy - eh * 0.28f, ew, eh), ew * 0.40f, c);
        FillRound(g, RectF(r.X + w * 0.95f - ew, cy - eh * 0.28f, ew, eh), ew * 0.40f, c);
        break;
    }
    case ICON_SPEAKER: {
        // 梯形箱体 + 两个喇叭
        GraphicsPath p;
        REAL x0 = r.X + w * 0.22f, x1 = r.X + w * 0.78f;
        p.AddLine(x0, r.Y + h * 0.12f, x1, r.Y + h * 0.12f);
        p.AddLine(x1, r.Y + h * 0.12f, x1 - w * 0.06f, r.Y + h * 0.88f);
        p.AddLine(x1 - w * 0.06f, r.Y + h * 0.88f, x0 + w * 0.06f, r.Y + h * 0.88f);
        p.CloseFigure();
        g.DrawPath(&pen, &p);
        g.FillEllipse(&b, cx - w * 0.14f, r.Y + h * 0.22f, w * 0.28f, h * 0.20f);
        g.FillEllipse(&b, cx - w * 0.18f, r.Y + h * 0.54f, w * 0.36f, h * 0.28f);
        break;
    }
    case ICON_MIC: {
        // 胶囊 + 支架
        FillRound(g, RectF(cx - w * 0.17f, r.Y + h * 0.10f, w * 0.34f, h * 0.48f),
                  w * 0.17f, c);
        GraphicsPath p;
        p.AddArc(cx - w * 0.30f, r.Y + h * 0.32f, w * 0.60f, h * 0.46f, 0, 180);
        g.DrawPath(&pen, &p);
        g.DrawLine(&pen, cx, r.Y + h * 0.78f, cx, r.Y + h * 0.92f);
        break;
    }
    case ICON_BT: {
        // 蓝牙符号
        REAL x = cx, top = r.Y + h * 0.10f, bot = r.Y + h * 0.90f;
        g.DrawLine(&pen, x, top, x, bot);
        g.DrawLine(&pen, x, top, x + w * 0.24f, top + h * 0.26f);
        g.DrawLine(&pen, x + w * 0.24f, top + h * 0.26f, x - w * 0.22f, cy);
        g.DrawLine(&pen, x - w * 0.22f, cy, x + w * 0.24f, bot - h * 0.26f);
        g.DrawLine(&pen, x + w * 0.24f, bot - h * 0.26f, x, bot);
        break;
    }
    case ICON_CHIP: {
        RectF body(r.X + w * 0.24f, r.Y + h * 0.24f, w * 0.52f, h * 0.52f);
        g.DrawRectangle(&pen, body);
        for (int i = 0; i < 3; i++) {
            REAL yy = r.Y + h * (0.32f + i * 0.18f);
            g.DrawLine(&pen, r.X + w * 0.10f, yy, body.X, yy);
            g.DrawLine(&pen, body.X + body.Width, yy, r.X + w * 0.90f, yy);
        }
        break;
    }
    case ICON_MUSIC: {
        // 音符
        g.DrawLine(&pen, r.X + w * 0.62f, r.Y + h * 0.20f, r.X + w * 0.62f, r.Y + h * 0.72f);
        g.DrawLine(&pen, r.X + w * 0.62f, r.Y + h * 0.20f, r.X + w * 0.82f, r.Y + h * 0.28f);
        g.FillEllipse(&b, r.X + w * 0.40f, r.Y + h * 0.64f, w * 0.26f, h * 0.20f);
        break;
    }
    case ICON_VOLUME: {
        // 喇叭 + 声波
        GraphicsPath p;
        p.AddLine(r.X + w * 0.12f, cy - h * 0.14f, r.X + w * 0.32f, cy - h * 0.14f);
        p.AddLine(r.X + w * 0.32f, cy - h * 0.14f, r.X + w * 0.54f, cy - h * 0.34f);
        p.AddLine(r.X + w * 0.54f, cy - h * 0.34f, r.X + w * 0.54f, cy + h * 0.34f);
        p.AddLine(r.X + w * 0.54f, cy + h * 0.34f, r.X + w * 0.32f, cy + h * 0.14f);
        p.AddLine(r.X + w * 0.32f, cy + h * 0.14f, r.X + w * 0.12f, cy + h * 0.14f);
        p.CloseFigure();
        g.FillPath(&b, &p);
        g.DrawArc(&pen, r.X + w * 0.46f, cy - h * 0.26f, w * 0.30f, h * 0.52f, -55, 110);
        g.DrawArc(&pen, r.X + w * 0.44f, cy - h * 0.42f, w * 0.50f, h * 0.84f, -50, 100);
        break;
    }
    case ICON_MUTE: {
        GraphicsPath p;
        p.AddLine(r.X + w * 0.10f, cy - h * 0.13f, r.X + w * 0.28f, cy - h * 0.13f);
        p.AddLine(r.X + w * 0.28f, cy - h * 0.13f, r.X + w * 0.46f, cy - h * 0.32f);
        p.AddLine(r.X + w * 0.46f, cy - h * 0.32f, r.X + w * 0.46f, cy + h * 0.32f);
        p.AddLine(r.X + w * 0.46f, cy + h * 0.32f, r.X + w * 0.28f, cy + h * 0.13f);
        p.AddLine(r.X + w * 0.28f, cy + h * 0.13f, r.X + w * 0.10f, cy + h * 0.13f);
        p.CloseFigure();
        g.FillPath(&b, &p);
        g.DrawLine(&pen, r.X + w * 0.58f, cy - h * 0.22f, r.X + w * 0.86f, cy + h * 0.22f);
        g.DrawLine(&pen, r.X + w * 0.86f, cy - h * 0.22f, r.X + w * 0.58f, cy + h * 0.22f);
        break;
    }
    case ICON_EQ: {
        // 三根竖条（等高线）
        REAL xs[3] = { r.X + w * 0.26f, r.X + w * 0.50f, r.X + w * 0.74f };
        REAL hs[3] = { 0.44f, 0.70f, 0.34f };
        Pen thick(c, (REAL)(r.Width * 0.130f < 1.4f ? 1.4f : r.Width * 0.130f));
        thick.SetStartCap(LineCapRound);
        thick.SetEndCap(LineCapRound);
        for (int i = 0; i < 3; i++) {
            g.DrawLine(&thick, xs[i], cy - h * hs[i] / 2, xs[i], cy + h * hs[i] / 2);
        }
        break;
    }
    case ICON_PRESET: {
        // 卡片网格
        REAL gx = w * 0.26f;
        g.DrawRectangle(&pen, RectF(r.X + w * 0.14f, r.Y + h * 0.14f, gx, gx));
        g.DrawRectangle(&pen, RectF(r.X + w * 0.60f, r.Y + h * 0.14f, gx, gx));
        g.DrawRectangle(&pen, RectF(r.X + w * 0.14f, r.Y + h * 0.60f, gx, gx));
        g.DrawRectangle(&pen, RectF(r.X + w * 0.60f, r.Y + h * 0.60f, gx, gx));
        break;
    }
    case ICON_LANG: {
        // 地球：圆 + 经纬线
        Pen p2(c, lw * 0.9f);
        g.DrawEllipse(&p2, r.X + w * 0.14f, r.Y + h * 0.14f, w * 0.72f, h * 0.72f);
        g.DrawLine(&p2, r.X + w * 0.14f, cy, r.X + w * 0.86f, cy);
        g.DrawEllipse(&p2, cx - w * 0.18f, r.Y + h * 0.14f, w * 0.36f, h * 0.72f);
        break;
    }
    case ICON_BATTERY: {
        Pen p2(c, lw);
        g.DrawRectangle(&p2, RectF(r.X + w * 0.08f, r.Y + h * 0.28f, w * 0.72f, h * 0.44f));
        SolidBrush bb(c);
        g.FillRectangle(&bb, r.X + w * 0.84f, r.Y + h * 0.40f, w * 0.08f, h * 0.20f);
        g.FillRectangle(&bb, r.X + w * 0.16f, r.Y + h * 0.36f, w * 0.34f, h * 0.28f);
        break;
    }
    default:
        break;
    }
}

// 圆形/圆角图标底座 + 图标
inline void IconBadge(Graphics& g, const RectF& r, IconId id,
                      const Color& bgCol, const Color& fgCol) {
    FillRound(g, r, r.Width / 2, bgCol);
    REAL inset = r.Width * 0.26f;
    DrawIcon(g, RectF(r.X + inset, r.Y + inset, r.Width - inset * 2, r.Height - inset * 2),
             id, fgCol);
}

// ---------------- 状态点 ----------------
inline void Dot(Graphics& g, REAL cx, REAL cy, REAL r, const Color& c) {
    SolidBrush b(c);
    g.FillEllipse(&b, cx - r, cy - r, r * 2, r * 2);
}

// ---------------- 分段控件（替代 switch）----------------
// items: 每个选项的文本；返回命中索引，未命中 -1
struct SegItem {
    std::wstring text;
    bool enabled = true;
};

inline int SegHit(const RectF& r, POINT p) {
    if (p.x < r.X || p.x > r.X + r.Width || p.y < r.Y || p.y > r.Y + r.Height) return -1;
    return 0;   // 细分在调用方处理
}

// 绘制分段控件；返回每个选项的矩形
// selected = -1 表示"不选中任何项"（用于状态未知时避免误导）
inline std::vector<RectF> DrawSeg(Graphics& g, const RectF& r, const std::vector<SegItem>& items,
                                  int selected, const Font& f) {
    std::vector<RectF> out;
    if (items.empty()) return out;
    FillRound(g, r, (REAL)r.Height / 2.0f, T().surface);
    int n = (int)items.size();
    REAL gap = 2;
    REAL segW = (r.Width - gap * (n - 1)) / n;
    for (int i = 0; i < n; i++) {
        RectF seg(r.X + i * (segW + gap), r.Y, segW, r.Height);
        out.push_back(seg);
        bool sel = (i == selected);
        if (sel) {
            // 选中段用实心浅色填充，不再描边——描边会和轨道外框叠成"双框"，很脏
            FillRound(g, seg, (REAL)r.Height / 2.0f, T().surfaceHi);
        }
        Color tc = !items[i].enabled ? T().textFaint
                 : sel ? T().text : T().textDim;
        TextCenter(g, seg, items[i].text.c_str(), f, tc);
    }
    return out;
}

// ---------------- 卡片 ----------------
inline void Card(Graphics& g, const RectF& r, bool hover = false, bool selected = false,
                  bool accentBar = false) {
    FillRound(g, r, (REAL)8, hover ? T().surfaceHi : T().surface);
    if (selected)
        FillRound(g, r, (REAL)8, T().accentSoft);
    StrokeRound(g, r, (REAL)8, selected ? T().accent : T().border, selected ? 1.6f : 1.0f);
    if (accentBar) {
        RectF bar(r.X, r.Y + (REAL)8, (REAL)3, r.Height - (REAL)16);
        FillRound(g, bar, (REAL)1.5f, T().accent);
    }
}

// ---------------- 滑块（细条 + 圆形滑块头）----------------
inline void Slider(Graphics& g, const RectF& track, double pct,
                   const Font& f, const wchar_t* valueText, bool enabled = true) {
    REAL cy = track.Y + track.Height / 2;
    Color trk = enabled ? T().border : T().border;
    Pen p0(trk, (REAL)4);
    p0.SetStartCap(LineCapRound); p0.SetEndCap(LineCapRound);
    g.DrawLine(&p0, track.X, cy, track.X + track.Width, cy);
    if (pct > 0) {
        Pen p1(enabled ? T().accent : T().borderHi, (REAL)4);
        p1.SetStartCap(LineCapRound); p1.SetEndCap(LineCapRound);
        g.DrawLine(&p1, track.X, cy, track.X + (REAL)(track.Width * pct), cy);
    }
    REAL hx = track.X + (REAL)(track.Width * pct);
    SolidBrush hb(T().bg);
    g.FillEllipse(&hb, hx - (REAL)7, cy - (REAL)7, (REAL)14, (REAL)14);
    Pen hp(enabled ? T().accent : T().borderHi, (REAL)2);
    g.DrawEllipse(&hp, hx - (REAL)6, cy - (REAL)6, (REAL)12, (REAL)12);
    if (valueText) {
        // 数值右对齐到轨道右端 + 10 的位置（用 Far 对齐保证列整齐）
        StringFormat sf;
        sf.SetAlignment(StringAlignmentFar);
        sf.SetLineAlignment(StringAlignmentCenter);
        SolidBrush b(T().text);
        g.DrawString(valueText, -1, &f,
                     RectF(track.X + track.Width + (REAL)10, cy - (REAL)10, (REAL)40, (REAL)20),
                     &sf, (Brush*)&b);
    }
}

// ---------------- 滚动容器 ----------------
class ScrollArea {
public:
    float offset = 0;      // 当前滚动位置
    float contentH = 0;    // 内容总高
    float viewH = 0;       // 视口高
    bool  dragging = false;
    int   dragStartY = 0;
    float dragStartOffset = 0;

    void Update(float content, float view) {
        contentH = content;
        viewH = view;
        float maxOff = std::max(0.0f, contentH - viewH);
        if (offset > maxOff) offset = maxOff;
        if (offset < 0) offset = 0;
    }
    float MaxOffset() const { return std::max(0.0f, contentH - viewH); }
    bool CanScroll() const { return MaxOffset() > 1.0f; }

    void OnMouseMove(int y) {
        if (!dragging) return;
        float dy = (float)(y - dragStartY);
        offset = dragStartOffset - dy;
        float maxOff = MaxOffset();
        if (offset > maxOff) offset = maxOff;
        if (offset < 0) offset = 0;
    }
    void OnMouseDown(int y) { dragging = true; dragStartY = y; dragStartOffset = offset; }
    void OnMouseUp() { dragging = false; }
    void OnWheel(int delta) {
        offset -= delta * 0.6f;
        float maxOff = MaxOffset();
        if (offset > maxOff) offset = maxOff;
        if (offset < 0) offset = 0;
    }
};

// ---------------- 导航项 ----------------
struct NavItem {
    const wchar_t* zh;
    const wchar_t* en;
    int id;
};

}  // namespace eui
