// 新版界面绘制：导航栏 / 顶栏 / 各页面内容
//
// ★ 绘制与命中测试共用 eui2::ComputeLayout()，杜绝坐标错位。
#pragma once
#include "eui2.h"

namespace eui2 {

using namespace eui;   // 引入主题与绘制工具

// ---------------- 通用小部件 ----------------

// 区块标题
inline void SectionTitle(Graphics& g, REAL x, REAL y, const wchar_t* s, const Font& f) {
    TextLeft(g, x, y, s, f, eui::T().text);
}

// 左标签 + 右控件 的一行基线
inline REAL RowLabelY(int rowTop, int rowH) {
    return (REAL)(rowTop + rowH / 2);
}

// 画一条"标签 : 值"的键值行
inline void KeyValueRow(Graphics& g, REAL x, REAL y, REAL w, REAL h,
                        const wchar_t* label, const std::wstring& value,
                        const Font& fl, const Font& fv, bool alt) {
    const Theme& t = eui::T();
    RectF r(x, y, w, h);
    if (alt) { SolidBrush b(t.surface); g.FillRectangle(&b, r); }
    TextLeft(g, r.X + (REAL)12, r.Y + (REAL)(h / 2 - 9), label, fl, t.textDim);
    StringFormat sf; sf.SetAlignment(StringAlignmentFar); sf.SetLineAlignment(StringAlignmentNear);
    SolidBrush b(t.text);
    RectF vr(r.X + r.Width * 0.45f, r.Y + (REAL)(h / 2 - 9), r.Width * 0.55f - (REAL)12, (REAL)20);
    g.DrawString(value.c_str(), -1, &fv, vr, &sf, (Brush*)&b);
}

// ---------------- 侧边栏 ----------------
inline void DrawSidebar(Graphics& g, AppState& st) {
    const Metrics& m = M();
    const int dd = m.dpi;
    auto S = [dd](int v) { return MulDiv(v, dd, 96); };
    int navW = S(m.navW);
    const Theme& t = eui::T();
    Layout L = ComputeLayout(st);

    SolidBrush bg(t.sidebar);
    g.FillRectangle(&bg, 0, 0, navW, m.h);
    Pen sep(t.border, 1.0f);
    g.DrawLine(&sep, navW, 0, navW, m.h);

    // 应用标识（通用音频面板，不再绑定 EPOS 品牌）
    RectF logo((REAL)S(16), (REAL)S(16), (REAL)S(28), (REAL)S(28));
    FillRound(g, logo, (REAL)9, t.accent);
    Font fLogo = MakeF(S(13), true);
    TextCenter(g, logo, L"♪", fLogo, Color(255, 255, 255, 255));
    Font fApp = MakeF(S(13), true);
    TextLeft(g, S(52), S(22), st.T(L"音频控制台", L"Audio Console"), fApp, t.text);

    // 导航项（动态：EPOS 专属页只在设备在线时出现）
    int y = L.navTop;
    Font fNav = MakeF(S(13));
    int navN = NavVisibleCount(st);
    for (int i = 0; i < navN; i++) {
        const NavDef& nv = NavAt(st, i);
        RectF r((REAL)S(10), (REAL)y, (REAL)(navW - S(20)), (REAL)L.navItemBoxH);
        bool sel = (st.page == nv.page);
        bool hov = (st.hoverId == CID_PAGE_BASE + i);
        if (sel) {
            FillRound(g, r, (REAL)8, t.accentSoft);
            RectF ind(r.X, r.Y + (REAL)10, (REAL)3, r.Height - (REAL)20);
            FillRound(g, ind, (REAL)1.5f, t.accent);
        } else if (hov) {
            // ★ 悬停底色要比卡片更深一档（surface 和 sidebar 太接近，看不出来）
            FillRound(g, r, (REAL)8, t.surfaceHi);
            RectF ind(r.X, r.Y + (REAL)10, (REAL)3, r.Height - (REAL)20);
            FillRound(g, ind, (REAL)1.5f, t.borderHi);
        }
        // 矢量图标（图标 id 跟着 NavDef 走，不会再有数组越界）
        RectF ic(r.X + (REAL)S(11), r.Y + (REAL)((L.navItemBoxH - S(18)) / 2),
                 (REAL)S(18), (REAL)S(18));
        DrawIcon(g, ic, (IconId)nv.icon, sel ? t.accent : (hov ? t.text : t.textDim));
        TextLeftVC(g, RectF(r.X, r.Y, r.Width - (REAL)S(8), r.Height),
                   st.T(nv.zh, nv.en), fNav,
                   sel ? t.text : (hov ? t.text : t.textDim),
                   (REAL)S(38));
        y += L.navItemH;
    }

    // ---- 底部一行：左"服务状态"，右语言图标按钮 ----
    RectF foot((REAL)L.footX, (REAL)L.footY, (REAL)L.footW, (REAL)L.footH);

    // ---- 左下角信息行：EPOS 在线显示耳机，否则显示系统默认输出 ----
    // （顶栏不再放设备选择器 —— 通用面板不该被某款耳机占用顶部空间）
    {
        std::wstring line;
        bool epos = st.eposOnline && !st.devices.empty();
        const eposdev::DeviceInfo* d = nullptr;
        if (epos) {
            int idx = (st.selectedDevice >= 0 && st.selectedDevice < (int)st.devices.size())
                        ? st.selectedDevice : 0;
            d = &st.devices[idx];
        }
        if (d) {
            std::wstring nm = d->deviceDisplayName.empty() ? L"EPOS" : d->deviceDisplayName;
            line = nm + ((d->hsConnection == L"Dongle") ? L" · 2.4G" : L" · BT");
        } else {
            line = st.T(L"输出: ", L"Out: ");
            if (st.endpoints)
                for (const auto& ep : *st.endpoints)
                    if (ep.id == st.currentOutputId) { line += ep.name; break; }
        }
        if (line.size() > 24) line = line.substr(0, 23) + L"…";
        bool hovOut = (st.hoverId == CID_ACTION && st.actionHint == ACT_GOTO_DEV);
        Font fOut = MakeF(S(10));
        REAL tx = foot.X + (REAL)S(2);
        if (d) {
            // 连接状态点（2.4G 绿 / 蓝牙黄）
            // 文字视觉中心实测在 outY+8.5（10px 字体），点跟着文字走
            Dot(g, foot.X + (REAL)S(6), (REAL)(L.outY + S(8)) + (REAL)0.5f, (REAL)S(3),
                (d->hsConnection == L"Dongle") ? t.success : t.warn);
            tx = foot.X + (REAL)S(14);
        }
        TextLeft(g, tx, (REAL)L.outY, line.c_str(), fOut,
                 (hovOut || d) ? t.text : t.textDim);
    }

    // 左：连接状态
    {
        Font fSm = MakeF(S(10));
        REAL cy = foot.Y + foot.Height / 2;
        // ★ Dot 收的是圆心坐标；实测文字中心正好等于行中心 cy
        Dot(g, foot.X + (REAL)S(5), cy, (REAL)S(3),
            st.pipeReady ? t.success : t.warn);
        TextLeft(g, foot.X + (REAL)S(13), cy - (REAL)S(7),
                 st.pipeReady ? st.T(L"服务已连接", L"Service connected")
                              : st.T(L"服务未连接", L"Service offline"),
                 fSm, st.pipeReady ? t.success : t.warn);
    }

    // 右：语言图标按钮（点开向上弹出语言列表）
    {
        RectF cb((REAL)L.langX, (REAL)L.langY, (REAL)L.langW, (REAL)L.langH);
        bool open = st.langOpen;
        bool hov = (st.hoverId == CID_ACTION && st.actionHint == ACT_LANG) || open;
        FillRound(g, cb, (REAL)7, open ? t.surfaceHi : (hov ? t.surfaceHi : t.bg));
        StrokeRound(g, cb, (REAL)7, open ? t.accent : (hov ? t.borderHi : t.border), 1.0f);
        DrawIcon(g, RectF(cb.X + (REAL)S(9), cb.Y + (REAL)S(6), (REAL)S(16), (REAL)S(16)),
                 ICON_LANG, open ? t.accent : (hov ? t.text : t.textDim));
    }
}

// ---------------- 语言下拉浮层（最后画，盖在一切之上）----------------
inline void DrawLangPopup(Graphics& g, AppState& st) {
    if (!st.langOpen) return;
    const Metrics& m = M();
    const int dd = m.dpi;
    auto S = [dd](int v) { return MulDiv(v, dd, 96); };
    const Theme& t = eui::T();
    Layout L = ComputeLayout(st);

    // 弹层宽度 = 整条底部（langX/langW 现在只是右侧小按钮）
    RectF pr((REAL)L.footX, (REAL)L.langPopY, (REAL)L.footW, (REAL)L.langPopH);
    // 阴影感：先画一层稍大的深色圆角
    RectF sh(pr.X - 1, pr.Y + 2, pr.Width + 2, pr.Height + 3);
    FillRound(g, sh, (REAL)9, Color(90, 0, 0, 0));
    FillRound(g, pr, (REAL)8, t.surfaceHi);
    StrokeRound(g, pr, (REAL)8, t.borderHi, 1.0f);

    const wchar_t* names[2] = { L"中文", L"English" };
    Font f = MakeF(S(12));
    for (int i = 0; i < 2; i++) {
        RectF ir(pr.X + (REAL)S(4), pr.Y + (REAL)S(4) + (REAL)(i * L.langItemH),
                 pr.Width - (REAL)S(8), (REAL)L.langItemH);
        bool sel = st.english == (i == 1);
        bool hov = (st.hoverId == CID_LANG_POP && st.hoverIndex == i);
        if (hov) FillRound(g, ir, (REAL)6, t.surface);
        TextLeftVC(g, ir, names[i], f, sel ? t.accent : t.text, (REAL)S(10));
        if (sel) {
            // 右侧勾
            REAL cx = ir.X + ir.Width - (REAL)S(16), cy = ir.Y + ir.Height / 2;
            Pen pen(t.accent, 1.8f);
            pen.SetStartCap(LineCapRound); pen.SetEndCap(LineCapRound);
            g.DrawLine(&pen, cx - (REAL)S(4), cy, cx - (REAL)S(1), cy + (REAL)S(3));
            g.DrawLine(&pen, cx - (REAL)S(1), cy + (REAL)S(3), cx + (REAL)S(5), cy - (REAL)S(4));
        }
    }
}


// ---------------- 顶栏 ----------------
inline void DrawTopBar(Graphics& g, AppState& st) {
    const Metrics& m = M();
    const int dd = m.dpi;
    auto S = [dd](int v) { return MulDiv(v, dd, 96); };
    const Theme& t = eui::T();
    int navW = S(m.navW);
    int pad = S(m.pad);
    Layout L = ComputeLayout(st);

    Pen sep(t.border, 1.0f);
    g.DrawLine(&sep, navW, S(m.topH) - 1, m.w, S(m.topH) - 1);

    int x = navW + pad;

    const eposdev::DeviceInfo* dev = nullptr;
    if (st.selectedDevice >= 0 && st.selectedDevice < (int)st.devices.size())
        dev = &st.devices[st.selectedDevice];

    // ---- 设备信息已移到侧栏左下角（见 DrawSidebar 底部）----
    // 顶栏只留页面标题：这是通用音频面板，不该被某款耳机占一块位置。


    // ---- 页面标题 ----
    int titleX = x;
    Font fTitle = MakeF(S(16), true);
    const NavDef* nd = NavByPage(st, st.page);
    TextLeft(g, (REAL)titleX, (REAL)S(18), st.T(nd->zh, nd->en), fTitle, t.text);

    // ---- 右上角关闭按钮 ----
    {
        RectF cb = CloseButtonRect();
        bool hov = (st.hoverId == CID_CLOSE);
        if (hov) FillRound(g, cb, (REAL)6, t.surfaceHi);
        REAL ins = (REAL)S(6);
        DrawIcon(g, RectF(cb.X + ins, cb.Y + ins, cb.Width - ins * 2, cb.Height - ins * 2),
                 ICON_CLOSE, hov ? t.text : t.textDim);
    }

    // ---- 右侧电量环（放在关闭按钮左边，避免重叠）----
    if (dev && dev->IsHeadset() && dev->battery >= 0) {
        REAL cx = (REAL)(m.w - S(16) - S(24) - S(10) - S(15)), cy = (REAL)(S(m.topH) / 2);
        REAL r = (REAL)S(15);
        RectF rc(cx - r, cy - r, r * 2, r * 2);
        Pen track(t.border, (REAL)3.5f);
        g.DrawEllipse(&track, rc);
        int pct = dev->battery;
        Color pc = pct <= 20 ? t.danger : (pct <= 40 ? t.warn : t.success);
        Pen prog(pc, (REAL)3.5f);
        prog.SetStartCap(LineCapRound); prog.SetEndCap(LineCapRound);
        g.DrawArc(&prog, rc, -90.0f, 360.0f * pct / 100.0f);
        Font fB = MakeF(S(11), true);
        TextCenter(g, rc, std::to_wstring(pct).c_str(), fB, t.text);
    }
}

// ---------------- 降噪显示（统一口径）----------------
//
// ★ 重要事实（实测）：H3PRO 上服务返回的 noiseReductionLevel 恒为 "0"，
//   且 nrAvailability 恒为空串 —— 空表示"服务不提供该设备的降噪档位信息"。
//   所以不能直接把 0 显示成"关闭"，那会与耳机实际的 ANC 开关矛盾。
//   H3PRO 的 ANC 是耳机上的物理开关，Windows 侧读不到。
//
//   判定：
//     nrStateKnown = true  -> 服务确实给了降噪状态，按档位显示
//     nrStateKnown = false -> 显示"耳机端控制"，不谎报开关状态
inline std::wstring NoiseReductionText(const AppState& st, const eposdev::DeviceInfo* dev) {
    if (!dev) return st.T(L"未连接", L"Not connected");
    if (!dev->IsHeadset()) return st.T(L"耳机离线", L"Headset offline");
    // ★ HID 监听拿到的真实 ANC 状态优先（dongle 按键上报）
    if (st.ancState == 1) return st.T(L"开", L"On");
    if (st.ancState == 0) return st.T(L"关", L"Off");
    if (dev->nrStateKnown && dev->noiseReductionLevel >= 0) {
        if (dev->noiseReductionLevel == 0) return st.T(L"关闭", L"Off");
        int lv = dev->noiseReductionLevel;
        return (lv <= 1) ? st.T(L"开启 · 低", L"On · Low")
             : (lv == 2) ? st.T(L"开启 · 中", L"On · Medium")
                         : st.T(L"开启 · 高", L"On · High");
    }
    // 服务读不到真实状态 —— 如实说明，不谎报
    return st.T(L"耳机端控制", L"Headset-side");
}

// ---------------- 概览页 ----------------
inline void DrawOverview(Graphics& g, AppState& st) {
    const Metrics& m = M();
    const int dd = m.dpi;
    auto S = [dd](int v) { return MulDiv(v, dd, 96); };
    const Theme& t = eui::T();
    Layout L = ComputeLayout(st);

    const eposdev::DeviceInfo* dev = nullptr;
    if (st.selectedDevice >= 0 && st.selectedDevice < (int)st.devices.size())
        dev = &st.devices[st.selectedDevice];

    // ---- 指标卡 ----
    struct Tile { const wchar_t* label; std::wstring value; Color vc; };
    std::vector<Tile> tiles;
    if (st.eposOnline) {
        tiles.push_back({ st.T(L"电量", L"Battery"),
                          dev && dev->battery >= 0 ? (std::to_wstring(dev->battery) + L"%") : L"—",
                          (dev && dev->battery >= 0 && dev->battery <= 20) ? t.danger : t.text });
        tiles.push_back({ st.T(L"降噪", L"Noise reduction"), NoiseReductionText(st, dev), t.text });
        tiles.push_back({ st.T(L"耳机固件", L"Headset firmware"),
                          dev && dev->IsHeadset() && !dev->firmwareVersion.empty()
                            ? dev->firmwareVersion
                            : (dev && dev->IsDongle() ? st.T(L"耳机离线", L"Offline") : L"—"), t.text });
        tiles.push_back({ st.T(L"播放延迟", L"Playback latency"),
                          dev ? (dev->latencyLevel == 1 ? st.T(L"低延迟", L"Low")
                               : dev->latencyLevel == 2 ? st.T(L"稳定", L"Stable")
                               : dev->latencyLevel >= 0 ? st.T(L"标准", L"Standard")
                               : st.T(L"不支持", L"N/A")) : L"—", t.text });
    } else {
        // 通用模式：统计卡换成所有用户都关心的系统指标
        std::wstring btBatt = L"—";
        int outN = 0, inN = 0;
        std::wstring outFmt = L"—";
        if (st.endpoints) {
            for (const auto& ep : *st.endpoints) {
                outN++;
                if (ep.kind == KIND_BT && ep.batt >= 0)
                    btBatt = std::to_wstring(ep.batt) + L"%";
                if (ep.id == st.currentOutputId && !ep.fmt.empty()) outFmt = ep.fmt;
            }
        }
        if (st.captureEndpoints) inN = (int)st.captureEndpoints->size();
        tiles.push_back({ st.T(L"蓝牙电量", L"BT battery"), btBatt,
                          btBatt != L"—" && btBatt <= L"20%" ? t.danger : t.text });
        tiles.push_back({ st.T(L"播放设备", L"Playback devs"), std::to_wstring(outN), t.text });
        tiles.push_back({ st.T(L"录音设备", L"Recording devs"), std::to_wstring(inN), t.text });
        if (outFmt.size() > 11) outFmt = outFmt.substr(0, 10) + L"…";
        tiles.push_back({ st.T(L"输出格式", L"Output format"), outFmt, t.text });
    }

    Font fTileL = MakeF(S(11));
    Font fTileV = MakeF(S(18), true);
    for (size_t i = 0; i < tiles.size(); i++) {
        RectF r((REAL)(L.contentX + (int)i * (L.ovTileW + S(m.gutter))), (REAL)L.ovTileY,
                (REAL)L.ovTileW, (REAL)L.ovTileH);
        FillRound(g, r, (REAL)10, t.surface);
        StrokeRound(g, r, (REAL)10, t.border, 1.0f);
        TextLeft(g, r.X + (REAL)S(14), r.Y + (REAL)S(12), tiles[i].label, fTileL, t.textDim);
        // 数值可能较长（"开启 · 中"），用小一号字体并允许省略
        TextLeft(g, r.X + (REAL)S(14), r.Y + (REAL)S(32), tiles[i].value.c_str(), fTileV, tiles[i].vc);
    }

    // ---- 音量卡片 ----
    {
        RectF vc((REAL)L.contentX, (REAL)L.volY, (REAL)L.contentW, (REAL)L.volH);
        FillRound(g, vc, (REAL)10, t.surface);
        StrokeRound(g, vc, (REAL)10, t.border, 1.0f);
        Font fVolTitle = MakeF(S(13), true);
        TextLeft(g, vc.X + (REAL)S(14), vc.Y + (REAL)S(12),
                 st.T(L"音量", L"Volume"), fVolTitle, t.text);

        struct VolRow { const wchar_t* zh; const wchar_t* en; int val; bool muted; bool isMic; };
        VolRow vr[2] = {
            { L"扬声器", L"Speakers", st.speakerVol, st.speakerMuted, false },
            { L"麦克风", L"Microphone", st.micVol,   st.micMuted,    true  },
        };
        Font fVolLbl = MakeF(S(12));
        Font fVolVal = MakeF(S(11));
        for (int k = 0; k < 2; k++) {
            int ry = L.volY + S(40) + k * L.volRowH;
            // 标签
            StringFormat sfL; sfL.SetAlignment(StringAlignmentNear); sfL.SetLineAlignment(StringAlignmentCenter);
            SolidBrush bL(t.textDim);
            g.DrawString(st.T(vr[k].zh, vr[k].en), -1, &fVolLbl,
                         RectF((REAL)L.contentX + (REAL)S(14), (REAL)ry,
                               (REAL)(L.volTrackX - L.contentX - S(20)), (REAL)S(30)),
                         &sfL, (Brush*)&bL);

            // 轨道
            RectF tr((REAL)L.volTrackX, (REAL)(ry + S(6)), (REAL)L.volTrackW, (REAL)S(22));
            double pct = vr[k].val >= 0 ? vr[k].val / 100.0 : 0.0;
            wchar_t buf[16];
            if (vr[k].val >= 0) swprintf(buf, 16, L"%d", vr[k].val);
            else wcscpy(buf, L"--");
            Slider(g, tr, pct, fVolVal, buf);

            // 静音按钮：图标按钮（喇叭 / 麦克风，静音时加斜杠并变红）
            RectF mb((REAL)L.volBtnX, (REAL)(ry + S(2)),
                     (REAL)L.volBtnW, (REAL)S(28));
            bool hovM = (st.hoverId == CID_ACTION &&
                         st.actionHint == (k == 0 ? ACT_MUTE_SPK : ACT_MUTE_MIC));
            FillRound(g, mb, (REAL)6, vr[k].muted ? t.dangerSoft : (hovM ? t.surfaceHi : t.surface));
            StrokeRound(g, mb, (REAL)6, vr[k].muted ? t.danger : t.border, 1.0f);
            {
                REAL ins = (REAL)S(7);
                RectF ir(mb.X + ins, mb.Y + (mb.Height - (mb.Width - ins * 2)) / 2,
                         mb.Width - ins * 2, mb.Width - ins * 2);
                Color ic = vr[k].muted ? t.danger : (hovM ? t.text : t.textDim);
                // 麦克风行用麦克风图标，扬声器行用喇叭图标
                if (k == 1) {
                    DrawIcon(g, ir, ICON_MIC, ic);
                } else {
                    DrawIcon(g, ir, ICON_SPEAKER, ic);
                }
                if (vr[k].muted) {
                    // 静音斜杠
                    Pen sl(ic, (REAL)1.6f);
                    sl.SetStartCap(LineCapRound); sl.SetEndCap(LineCapRound);
                    g.DrawLine(&sl, ir.X + ir.Width * 0.08f, ir.Y + ir.Height * 0.92f,
                                     ir.X + ir.Width * 0.92f, ir.Y + ir.Height * 0.08f);
                }
            }
        }
    }

    // ---- 两栏面板 ----
    int colW = (L.contentW - S(m.gutter)) / 2;
    int gap = L.contentW - colW * 2;      // 保证两栏之和等于内容宽

    // 左：已连接设备（面板固定高，设备多时内部滚动）
    OvDevList od = OvDevLayout(st, L);
    {
        // ★ 滚动量在这里夹紧：内容变短时立即回弹到合法范围
        if (st.ovDevScroll > od.maxScroll) st.ovDevScroll = od.maxScroll;
        if (st.ovDevScroll < 0) st.ovDevScroll = 0;
    }
    RectF pl = od.panel;
    FillRound(g, pl, (REAL)10, t.surface);
    StrokeRound(g, pl, (REAL)10, t.border, 1.0f);
    Font fPanel = MakeF(S(13), true);
    TextLeft(g, pl.X + (REAL)S(14), pl.Y + (REAL)S(14),
             st.T(L"已连接设备", L"Connected devices"), fPanel, t.text);

    Font fDev = MakeF(S(13), true);
    Font fDevS = MakeF(S(11));
    {
        // 内部滚动：裁剪到可视区，绘制 y 统一减去滚动量
        g.SetClip(od.view);
        int iy = (int)od.view.Y - (int)st.ovDevScroll;
        if (od.hasHeadset) {
            const eposdev::DeviceInfo& dv = st.devices[st.selectedDevice];
            RectF dr((REAL)(pl.X + S(10)), (REAL)iy, (REAL)(pl.Width - S(20)), (REAL)S(48));
            FillRound(g, dr, (REAL)8, t.surfaceHi);
            RectF av(dr.X + (REAL)S(10), dr.Y + (REAL)S(11), (REAL)S(26), (REAL)S(26));
            FillRound(g, av, (REAL)13, t.accent);
            DrawIcon(g, RectF(av.X + av.Width * 0.26f, av.Y + av.Height * 0.26f,
                              av.Width * 0.48f, av.Height * 0.48f),
                     ICON_HEADSET, Color(255, 255, 255, 255));
            TextLeft(g, dr.X + (REAL)S(46), dr.Y + (REAL)S(8),
                     dv.deviceDisplayName.c_str(), fDev, t.text);
            std::wstring sub = (dv.hsConnection == L"Dongle") ? L"2.4 GHz" : st.T(L"蓝牙", L"Bluetooth");
            if (dv.battery >= 0) sub += L" · " + std::to_wstring(dv.battery) + L"%";
            TextLeft(g, dr.X + (REAL)S(46), dr.Y + (REAL)S(26), sub.c_str(), fDevS, t.textDim);
            iy += S(52);
        }
        if (st.endpoints) {
            for (const auto& ep : *st.endpoints) {
                if (ep.kind == KIND_EPOS || ep.kind == KIND_EXCLUDED) continue;
                RectF dr((REAL)(pl.X + S(10)), (REAL)iy, (REAL)(pl.Width - S(20)), (REAL)S(42));
                bool cur = (ep.id == st.currentOutputId);
                if (cur) FillRound(g, dr, (REAL)8, t.accentSoft);
                RectF av(dr.X + (REAL)S(10), dr.Y + (REAL)S(8), (REAL)S(26), (REAL)S(26));
                FillRound(g, av, (REAL)13, cur ? t.accent : t.surfaceHi);
                IconId eic = ICON_MUSIC;
                if (ep.kind == KIND_BT)           eic = ICON_BT;
                else if (ep.kind == KIND_REALTEK) eic = ICON_CHIP;
                else if (ep.kind == KIND_VIRTUAL) eic = ICON_SLIDERS;
                DrawIcon(g, RectF(av.X + av.Width * 0.28f, av.Y + av.Height * 0.28f,
                                  av.Width * 0.44f, av.Height * 0.44f),
                         eic, cur ? Color(255,255,255,255) : t.textDim);
                std::wstring n2 = ep.name;
                if (n2.size() > 22) n2 = n2.substr(0, 21) + L"…";
                TextLeft(g, dr.X + (REAL)S(46), dr.Y + (REAL)S(5), n2.c_str(), fDev, t.text);
                std::wstring sub2 = cur ? st.T(L"当前默认输出", L"Current output") : L"";
                if (ep.kind == KIND_BT && ep.batt >= 0)
                    sub2 += (sub2.empty() ? L"" : L"  ·  ") + std::to_wstring(ep.batt) + L"%";
                TextLeft(g, dr.X + (REAL)S(46), dr.Y + (REAL)S(22),
                         sub2.c_str(), fDevS, t.accent);
                iy += S(46);
            }
        }
        g.ResetClip();

        // 可滚动时画一条细滚动条（面板右缘）
        if (od.maxScroll > 0) {
            REAL trackH = od.view.Height - (REAL)S(8);
            REAL th = trackH * (od.view.Height / od.content);
            if (th < (REAL)S(24)) th = (REAL)S(24);
            REAL ty = od.view.Y + (REAL)S(4) +
                      (trackH - th) * (st.ovDevScroll / od.maxScroll);
            RectF track(od.view.X + od.view.Width - (REAL)S(5),
                        od.view.Y + (REAL)S(4), (REAL)S(3), trackH);
            FillRound(g, track, (REAL)1.5f, t.border);
            FillRound(g, RectF(track.X, ty, (REAL)S(3), th), (REAL)1.5f, t.textFaint);
        }
    }

    // 右：设备状态（EPOS 在线 → 耳机状态；否则 → 系统默认设备）
    RectF pr((REAL)(L.contentX + colW + gap), (REAL)L.ovPanelY, (REAL)colW, (REAL)L.ovPanelH);
    FillRound(g, pr, (REAL)10, t.surface);
    StrokeRound(g, pr, (REAL)10, t.border, 1.0f);
    TextLeft(g, pr.X + (REAL)S(14), pr.Y + (REAL)S(14),
             st.eposOnline ? st.T(L"耳机状态", L"Headset status")
                           : st.T(L"系统默认设备", L"System defaults"), fPanel, t.text);

    struct Row { const wchar_t* label; std::wstring value; };
    std::vector<Row> rows;
    if (st.eposOnline && dev && dev->IsHeadset()) {
        // ★ ANC：dongle HID 会上报按键状态（只读监听），未按键前显示"待同步"
        std::wstring anc;
        if (st.ancState == 1)      anc = st.T(L"开（耳机端）", L"On (headset)");
        else if (st.ancState == 0) anc = st.T(L"关（耳机端）", L"Off (headset)");
        else                       anc = st.T(L"按一下耳机 ANC 键即可同步",
                                              L"Press the ANC key once to sync");
        rows.push_back({ st.T(L"降噪", L"Noise reduction"), anc });
        rows.push_back({ st.T(L"侧音", L"Sidetone"),
                         dev->sideToneGain >= 0 ? std::to_wstring(dev->sideToneGain) : L"—" });
        rows.push_back({ st.T(L"连接方式", L"Connection"),
                         (dev->hsConnection == L"Dongle") ? L"2.4 GHz" : st.T(L"蓝牙", L"Bluetooth") });
        rows.push_back({ st.T(L"固件", L"Firmware"),
                         dev->firmwareVersion.empty() ? L"—" : dev->firmwareVersion });
    } else if (st.eposOnline && dev && dev->IsDongle()) {
        rows.push_back({ st.T(L"降噪", L"Noise reduction"), st.T(L"耳机离线", L"Headset offline") });
        rows.push_back({ st.T(L"接收器固件", L"Dongle firmware"),
                         dev->firmwareVersion.empty() ? L"—" : dev->firmwareVersion });
    } else {
        // 通用模式：列出系统默认输出 / 输入 —— 这是所有用户都关心的
        std::wstring outName = L"—", inName = L"—";
        if (st.endpoints)
            for (const auto& ep : *st.endpoints)
                if (ep.id == st.currentOutputId) { outName = ep.name; break; }
        if (st.captureEndpoints)
            for (const auto& ep : *st.captureEndpoints)
                if (ep.id == st.currentInputId) { inName = ep.name; break; }
        if (outName.size() > 34) outName = outName.substr(0, 33) + L"…";
        if (inName.size() > 34)  inName  = inName.substr(0, 33) + L"…";
        rows.push_back({ st.T(L"模式", L"Mode"),
                         st.T(L"通用音频控制", L"Generic audio control") });
        rows.push_back({ st.T(L"默认输出", L"Default output"), outName });
        rows.push_back({ st.T(L"默认输入", L"Default input"), inName });
        rows.push_back({ st.T(L"提示", L"Tip"),
                         st.T(L"连接 EPOS 耳机后解锁 EQ / 效果控制",
                              L"Connect an EPOS headset for EQ / effects") });
    }
    Font fRowL = MakeF(S(12));
    Font fRowV = MakeF(S(12), true);
    int ry = (int)pr.Y + S(44);
    for (size_t i = 0; i < rows.size(); i++) {
        KeyValueRow(g, pr.X + (REAL)S(10), (REAL)ry, (REAL)(colW - S(20)), (REAL)S(28),
                    rows[i].label, rows[i].value, fRowL, fRowV, (i % 2) == 0);
        ry += S(28);
    }
    // 降噪的"状态来源 + 最后同步时间"：让用户能判断这是实时值还是旧值
    if (st.eposOnline && dev && dev->IsHeadset()) {
        std::wstring note;
        if (st.ancAt == 0) {
            note = st.T(L"按一下耳机 ANC 键即可同步", L"Press the headset ANC key to sync");
        } else if (st.ancAt == 1) {
            // 1 是哨兵：状态来自 settings.ini（上次运行拨过），不是本次实时同步
            note = st.T(L"降噪状态：上次已知，拨动滑块即刷新",
                        L"ANC state: last known — toggle the slider to refresh");
        } else {
            DWORD age = (GetTickCount() - st.ancAt) / 1000;
            if (age < 5) note = st.T(L"降噪状态：刚刚同步", L"ANC state: synced just now");
            else if (age < 60) note = st.T(L"降噪状态：%d 秒前同步", L"ANC state: synced %ds ago");
            else note = st.T(L"降噪状态：%d 分钟前同步", L"ANC state: synced %dm ago");
            if (age >= 5 && age < 60) {
                wchar_t b[96];
                swprintf(b, 96, note.c_str(), (int)age);
                note = b;
            } else if (age >= 60) {
                wchar_t b[96];
                swprintf(b, 96, note.c_str(), (int)(age / 60));
                note = b;
            }
        }
        TextLeft(g, pr.X + (REAL)S(12), (REAL)(ry + S(2)), note.c_str(), MakeF(S(10)), t.textFaint);
    }

    st.contentHeight = (float)(L.ovPanelY + L.ovPanelH - L.contentTop + S(24));
}

// ---------------- 声音页 ----------------
inline void DrawSound(Graphics& g, AppState& st) {
    const Metrics& m = M();
    const int dd = m.dpi;
    auto S = [dd](int v) { return MulDiv(v, dd, 96); };
    const Theme& t = eui::T();
    Layout L = ComputeLayout(st);

    Font fSec = MakeF(S(13), true);
    Font fCard = MakeF(S(13), true);
    Font fCardS = MakeF(S(11));
    Font fVal = MakeF(S(11));

    const eposdev::DeviceInfo* dev = nullptr;
    if (st.selectedDevice >= 0 && st.selectedDevice < (int)st.devices.size())
        dev = &st.devices[st.selectedDevice];

    // ---- 预设 ----
    SectionTitle(g, (REAL)L.contentX, (REAL)(L.presetGridY - L.titleH + S(4)),
                 st.T(L"预设", L"Presets"), fSec);

    for (int i = 0; i < PresetCount(st); i++) {
        int r = i / L.presetCols, c = i % L.presetCols;
        RectF cr((REAL)(L.contentX + c * (L.presetCardW + S(m.gutter))),
                 (REAL)(L.presetGridY + r * (L.presetCardH + S(m.gutter))),
                 (REAL)L.presetCardW, (REAL)L.presetCardH);
        bool sel = (i == st.currentPreset);
        // ★ 命中返回的是 CID_PRESET_BASE + hoverIndex（不带序号的 id），
        //   这里必须用 hoverIndex 判断，否则永远只高亮第 0 张卡。
        bool hov = (st.hoverId == CID_PRESET_BASE && st.hoverIndex == i);
        Card(g, cr, hov && !sel, sel);
        const eposfx::Preset& p = st.presets[i];
        TextLeft(g, cr.X + (REAL)S(12), cr.Y + (REAL)S(9),
                 st.english ? p.en.c_str() : p.zh.c_str(), fCard, sel ? t.accent : t.text);
        TextLeft(g, cr.X + (REAL)S(12), cr.Y + (REAL)S(28),
                 sel ? st.T(L"当前使用", L"In use")
                     : (p.builtin ? st.T(L"出厂预设", L"Factory") : st.T(L"自定义", L"Custom")),
                 fCardS, sel ? t.accent : t.textFaint);
        // 迷你曲线
        {
            REAL mx = cr.X + (REAL)S(12), my = cr.Y + (REAL)S(50);
            REAL mw = cr.Width - (REAL)S(24), mh = (REAL)S(10);
            Pen mp(t.borderHi, (REAL)1.4f);
            g.DrawLine(&mp, mx, my, mx + mw, my);
            REAL px = mx, py = my;
            for (int b = 0; b < 9; b++) {
                REAL nx = mx + mw * (b + 1) / 9.0f;
                REAL ny = my - (REAL)(p.speakerEq[b] / 6.0 * mh / 2.0);
                Pen sp(sel ? t.accent : t.textFaint, (REAL)1.4f);
                g.DrawLine(&sp, px, py, nx, ny);
                px = nx; py = ny;
            }
        }
        // 自定义预设：右上角 ×（点了删除）
        if (!p.builtin) {
            RectF xx(cr.X + cr.Width - (REAL)S(22), cr.Y + (REAL)S(4),
                     (REAL)S(18), (REAL)S(18));
            bool xhov = (st.hoverId == CID_PRESET_DEL && st.hoverIndex == i);
            if (xhov) FillRound(g, xx, (REAL)5, t.surfaceHi);
            DrawIcon(g, RectF(xx.X + (REAL)S(2), xx.Y + (REAL)S(2),
                              (REAL)S(14), (REAL)S(14)),
                     ICON_CLOSE, xhov ? t.danger : t.textFaint);
        }
    }

    // ---- 扬声器 EQ ----
    SectionTitle(g, (REAL)L.contentX, (REAL)(L.eqY - L.titleH + S(4)),
                 st.T(L"扬声器均衡器", L"Speaker equalizer"), fSec);

    RectF eq((REAL)L.contentX, (REAL)L.eqY, (REAL)L.contentW, (REAL)L.eqH);
    FillRound(g, eq, (REAL)10, t.surface);
    StrokeRound(g, eq, (REAL)10, t.border, 1.0f);

    REAL plotX = eq.X + (REAL)S(40);
    REAL plotW = eq.Width - (REAL)S(56);
    REAL top = eq.Y + (REAL)S(22);
    REAL h = eq.Height - (REAL)S(56);
    REAL mid = top + h / 2;

    Pen ml(t.border, 1.0f);
    g.DrawLine(&ml, plotX, mid, plotX + plotW, mid);
    Font fTick = MakeF(S(9));
    StringFormat sfTick; sfTick.SetAlignment(StringAlignmentFar); sfTick.SetLineAlignment(StringAlignmentCenter);
    SolidBrush brt(t.textFaint);
    g.DrawString(L"+6", -1, &fTick, RectF(eq.X + (REAL)S(8), top - (REAL)S(8), (REAL)S(24), (REAL)16), &sfTick, &brt);
    g.DrawString(L"0",  -1, &fTick, RectF(eq.X + (REAL)S(8), mid - (REAL)S(8), (REAL)S(24), (REAL)16), &sfTick, &brt);
    g.DrawString(L"-6", -1, &fTick, RectF(eq.X + (REAL)S(8), top + h - (REAL)S(8), (REAL)S(24), (REAL)16), &sfTick, &brt);

    REAL bandW = plotW / 9.0f;
    REAL prevX = 0, prevY = 0;
    for (int b = 0; b < 9; b++) {
        REAL cx = plotX + bandW * (b + 0.5f);
        REAL cy = mid - (REAL)(st.sound.speakerEq[b] / 6.0 * (h / 2));
        if (b > 0) { Pen sp(t.accent, (REAL)2); g.DrawLine(&sp, prevX, prevY, cx, cy); }
        prevX = cx; prevY = cy;
    }
    for (int b = 0; b < 9; b++) {
        REAL cx = plotX + bandW * (b + 0.5f);
        REAL cy = mid - (REAL)(st.sound.speakerEq[b] / 6.0 * (h / 2));
        bool act = (st.hoverId == CID_EQ_BASE && st.hoverIndex == b && st.scroll.dragging);
        Dot(g, cx, cy, (REAL)S(4.5), act ? Color(255,255,255,255) : t.accent);
        static const wchar_t* kF[9] = { L"32", L"64", L"125", L"250", L"500", L"1K", L"2K", L"4K", L"8K" };
        StringFormat cf; cf.SetAlignment(StringAlignmentCenter);
        g.DrawString(kF[b], -1, &fTick,
                     RectF(cx - bandW / 2, top + h + (REAL)S(10), bandW, (REAL)16), &cf, &brt);
    }

    // ---- 效果 ----
    SectionTitle(g, (REAL)L.contentX, (REAL)(L.segY - L.titleH + S(4)),
                 st.T(L"效果", L"Effects"), fSec);

    struct SegDef { SegKind kind; const wchar_t* zh; const wchar_t* en;
                    const wchar_t* hzh; const wchar_t* hen;
                    std::vector<std::wstring> opts; int selected; bool enabled = true; };
    std::vector<SegDef> segs;
    segs.push_back({ SEG_SURROUND, L"3D 声场", L"3D surround",
                     L"模拟环绕包围感，看片更沉浸；听音乐可能变糊",
                     L"Simulated surround for movies; can smear music",
                     { st.T(L"关",L"Off"), st.T(L"开",L"On") }, st.sound.surround3D ? 1 : 0 });
    segs.push_back({ SEG_VOICE, L"语音增强", L"Voice enhance",
                     L"抬中频，让队友说话更容易听清",
                     L"Lifts mids so voices cut through",
                     { st.T(L"关",L"Off"), st.T(L"暖",L"Warm"), st.T(L"清晰",L"Clear") }, st.sound.voiceEnhance ? 2 : 0 });
    segs.push_back({ SEG_DIRECT, L"直达声", L"Direct sound",
                     L"跳过部分音效处理，声音更直接、更少延迟",
                     L"Bypasses extra processing: more direct, lower latency",
                     { st.T(L"关",L"Off"), st.T(L"开",L"On") }, st.sound.directSound ? 1 : 0 });
    {
        // 降噪档位：来自设备的 noiseReductionLevel（0=关，1/2/3=低中高）
        // H3PRO 上服务读不到真实档位（nrAvailability 为空）→ 分段控件置灰，
        // 并且**不选中任何一项**（选中"关"会误导用户以为降噪是关的）。
        int nrSel = -1;
        bool nrKnown = false;
        if (dev && dev->IsHeadset()) {
            nrKnown = dev->nrStateKnown;
            if (nrKnown && dev->noiseReductionLevel >= 0) {
                nrSel = (dev->noiseReductionLevel == 0) ? 0
                     : (dev->noiseReductionLevel <= 1) ? 1
                     : (dev->noiseReductionLevel == 2) ? 2 : 3;
            }
        }
        segs.push_back({ SEG_NR, L"降噪", L"Noise reduction",
                         L"抵消环境低频噪声（发动机、空调）；H3PRO 由耳机上的开关控制",
                         L"Cancels low-frequency noise; on H3PRO it is a physical switch",
                         { st.T(L"关",L"Off"), st.T(L"低",L"Low"), st.T(L"中",L"Mid"), st.T(L"高",L"High") }, nrSel, nrKnown });
    }

    Font fSeg = MakeF(S(11));
    Font fSegLbl = MakeF(S(12));
    int segBoxH = S(26);
    for (size_t k = 0; k < segs.size(); k++) {
        SegDef& sd = segs[k];
        int rowTop = L.segY + (int)k * L.segRowH;
        // 分段控件（右对齐）
        RectF sr((REAL)(L.contentX + L.contentW - L.segW), (REAL)rowTop,
                 (REAL)L.segW, (REAL)segBoxH);
        std::vector<SegItem> items;
        for (auto& o : sd.opts) items.push_back({ o, sd.enabled });
        DrawSeg(g, sr, items, sd.selected, fSeg);

        // 标签：与分段控件垂直居中对齐
        Font fLbl = MakeF(S(12));
        StringFormat sfL; sfL.SetAlignment(StringAlignmentNear); sfL.SetLineAlignment(StringAlignmentCenter);
        SolidBrush bL(t.textDim);
        g.DrawString(st.T(sd.zh, sd.en), -1, &fLbl,
                     RectF((REAL)L.contentX, (REAL)rowTop, (REAL)L.labelW, (REAL)segBoxH),
                     &sfL, (Brush*)&bL);

        // 不可控时给出说明（放在标签右边，避免与分段控件重叠）
        if (!sd.enabled) {
            StringFormat sfN; sfN.SetAlignment(StringAlignmentNear); sfN.SetLineAlignment(StringAlignmentCenter);
            SolidBrush bN(t.textFaint);
            Font fN = MakeF(S(10));
            g.DrawString(st.T(L"耳机上开关", L"On the headset"), -1, &fN,
                         RectF((REAL)(L.contentX + L.labelW), (REAL)rowTop,
                               (REAL)(L.contentW - L.labelW - L.segW), (REAL)segBoxH),
                         &sfN, (Brush*)&bN);
        }

        // hint：控件下方一行灰字，解释这个开关到底在干什么
        {
            Font fHint = MakeF(S(10));
            StringFormat sfH; sfH.SetAlignment(StringAlignmentNear); sfH.SetLineAlignment(StringAlignmentNear);
            sfH.SetTrimming(StringTrimmingEllipsisCharacter);
            sfH.SetFormatFlags(StringFormatFlagsNoWrap);
            SolidBrush bH(t.textFaint);
            g.DrawString(st.T(sd.hzh, sd.hen), -1, &fHint,
                         RectF((REAL)L.contentX, (REAL)(rowTop + segBoxH + S(4)),
                               (REAL)(L.contentW - S(4)), (REAL)S(16)), &sfH, (Brush*)&bH);
        }
    }

    // ---- 数值 ----
    SectionTitle(g, (REAL)L.contentX, (REAL)(L.sldY - L.titleH + S(4)),
                 st.T(L"数值", L"Levels"), fSec);

    struct SldDef { const wchar_t* zh; const wchar_t* en; const wchar_t* hzh; const wchar_t* hen;
                    double val; double max; };
    double stGain = (dev && dev->sideToneGain >= 0) ? (double)dev->sideToneGain
                                                     : st.sound.sideToneLevel;
    std::vector<SldDef> slds = {
        { L"混响",   L"Reverb",
          L"给声音加一点空间残响；建议 0–20，再大就像在浴室里",
          L"Adds room reverb; keep it 0–20",
          st.sound.reverb,       100 },
        { L"噪声门", L"Noise gate",
          L"麦克风低于阈值的音量直接静音，挡键盘声/呼吸声；设太高会吃掉说话开头",
          L"Silences quiet mic input (keyboard, breathing); too high cuts speech",
          st.sound.noiseGate,    100 },
        { L"侧音",   L"Sidetone",
          L"把你自己的声音回放到耳机里，戴耳机说话才不别扭；0 = 关",
          L"Hear your own voice in the headset; 0 = off",
          stGain,                 20 },
    };
    Font fSldLbl = MakeF(S(12));
    for (size_t k = 0; k < slds.size(); k++) {
        SldDef& sd = slds[k];
        int rowTop = L.sldY + (int)k * L.sldRowH;
        // 标签与轨道垂直居中对齐
        StringFormat sfL; sfL.SetAlignment(StringAlignmentNear); sfL.SetLineAlignment(StringAlignmentCenter);
        SolidBrush bL(t.textDim);
        Font fL2 = MakeF(S(12));
        g.DrawString(st.T(sd.zh, sd.en), -1, &fL2,
                     RectF((REAL)L.contentX, (REAL)rowTop, (REAL)L.labelW, (REAL)S(22)),
                     &sfL, (Brush*)&bL);
        REAL tx = (REAL)L.trackX;
        REAL tw = (REAL)L.trackW;
        RectF tr(tx, (REAL)(rowTop + S(3)), tw, (REAL)S(22));
        wchar_t buf[32];
        swprintf(buf, 32, L"%d", (int)sd.val);
        Slider(g, tr, sd.max > 0 ? sd.val / sd.max : 0, fVal, buf);
        // hint
        {
            Font fHint = MakeF(S(10));
            StringFormat sfH; sfH.SetAlignment(StringAlignmentNear); sfH.SetLineAlignment(StringAlignmentNear);
            sfH.SetTrimming(StringTrimmingEllipsisCharacter);
            sfH.SetFormatFlags(StringFormatFlagsNoWrap);
            SolidBrush bH(t.textFaint);
            g.DrawString(st.T(sd.hzh, sd.hen), -1, &fHint,
                         RectF((REAL)L.contentX, (REAL)(rowTop + S(26)),
                               (REAL)(L.contentW - S(4)), (REAL)S(16)), &sfH, (Brush*)&bH);
        }
    }


    // ---- 底部按钮 ----
    struct Btn { const wchar_t* zh; const wchar_t* en; int action; };
    std::vector<Btn> btns = {
        { L"从官方捕获", L"Capture from app", ACT_CAPTURE },
        { L"保存为预设", L"Save as preset",   ACT_SAVE },
    };
    int halfW = (L.contentW - S(10)) / 2;
    for (size_t k = 0; k < btns.size(); k++) {
        REAL bx = (REAL)(L.contentX + (int)k * (halfW + S(10)));
        RectF br(bx, (REAL)L.btnY, (REAL)halfW, (REAL)S(m.btnH));
        bool hov = (st.hoverId == CID_ACTION && st.actionHint == btns[k].action);
        FillRound(g, br, (REAL)8, hov ? t.surfaceHi : t.surface);
        StrokeRound(g, br, (REAL)8, t.border, 1.0f);
        TextCenter(g, br, st.T(btns[k].zh, btns[k].en), MakeF(S(12)), hov ? t.text : t.textDim);
    }

    st.contentHeight = (float)(L.btnY + S(m.btnH) - L.contentTop + S(24));
}

// ---------------- 麦克风页 ----------------
inline void DrawMic(Graphics& g, AppState& st) {
    const Metrics& m = M();
    const int dd = m.dpi;
    auto S = [dd](int v) { return MulDiv(v, dd, 96); };
    const Theme& t = eui::T();
    Layout L = ComputeLayout(st);

    Font fSec = MakeF(S(13), true);

    const eposdev::DeviceInfo* dev = nullptr;
    if (st.selectedDevice >= 0 && st.selectedDevice < (int)st.devices.size())
        dev = &st.devices[st.selectedDevice];

    // ================= 麦克风均衡器 =================
    SectionTitle(g, (REAL)L.contentX, (REAL)(L.micPresetY - L.titleH + S(4)),
                 st.T(L"麦克风均衡器", L"Microphone equalizer"), fSec);

    // ---- 预设 chips ----
    {
        const int n = MicPresetCount();
        const MicPreset* ps = MicPresets();
        int cw = (L.contentW - S(8) * (n - 1)) / n;
        Font fChip = MakeF(S(11));
        for (int i = 0; i < n; i++) {
            RectF cr((REAL)(L.contentX + i * (cw + S(8))), (REAL)L.micPresetY,
                     (REAL)cw, (REAL)L.micPresetH);
            bool sel = (st.micPresetIdx == i);
            bool hov = (st.hoverId == CID_ACTION && st.actionHint == ACT_MIC_PRESET &&
                        st.hoverIndex == i);
            FillRound(g, cr, (REAL)6, sel ? t.accentSoft : (hov ? t.surfaceHi : t.surface));
            StrokeRound(g, cr, (REAL)6, sel ? t.accent : t.border, sel ? 1.4f : 1.0f);
            TextCenter(g, cr, st.T(ps[i].zh, ps[i].en), fChip, sel ? t.text : t.textDim);
        }
    }

    // ---- EQ 曲线 ----
    RectF eq((REAL)L.contentX, (REAL)L.micEqY, (REAL)L.contentW, (REAL)L.micEqH);
    FillRound(g, eq, (REAL)10, t.surface);
    StrokeRound(g, eq, (REAL)10, t.border, 1.0f);

    REAL plotX = eq.X + (REAL)S(40);
    REAL plotW = eq.Width - (REAL)S(56);
    REAL top = eq.Y + (REAL)S(20);
    REAL h = eq.Height - (REAL)S(52);
    REAL mid = top + h / 2;
    Pen ml(t.border, 1.0f);
    g.DrawLine(&ml, plotX, mid, plotX + plotW, mid);

    REAL bandW = plotW / 9.0f;
    REAL prevX = 0, prevY = 0;
    for (int b = 0; b < 9; b++) {
        REAL cx = plotX + bandW * (b + 0.5f);
        REAL cy = mid - (REAL)(st.sound.micEq[b] / 6.0 * (h / 2));
        if (b > 0) { Pen sp(t.purple, (REAL)2); g.DrawLine(&sp, prevX, prevY, cx, cy); }
        prevX = cx; prevY = cy;
    }
    for (int b = 0; b < 9; b++) {
        REAL cx = plotX + bandW * (b + 0.5f);
        REAL cy = mid - (REAL)(st.sound.micEq[b] / 6.0 * (h / 2));
        Dot(g, cx, cy, (REAL)S(4), t.purple);
    }

    // ================= 输入测试（实时电平）=================
    SectionTitle(g, (REAL)L.contentX, (REAL)(L.micTestY - L.titleH + S(4)),
                 st.T(L"输入测试", L"Input test"), fSec);

    {
        RectF card((REAL)L.contentX, (REAL)L.micTestY, (REAL)L.contentW, (REAL)L.micTestH);
        FillRound(g, card, (REAL)10, t.surface);
        StrokeRound(g, card, (REAL)10, t.border, 1.0f);

        const bool on = st.micTestOn;

        // ---- 标题 + 开关按钮 ----
        TextLeft(g, card.X + (REAL)S(14), card.Y + (REAL)S(14),
                 st.T(L"实时电平", L"Live level"), MakeF(S(12)), t.textDim);

        {
            RectF br((REAL)L.micTestBtnX, (REAL)L.micTestBtnY,
                     (REAL)L.micTestBtnW, (REAL)L.micTestBtnH);
            bool hov = (st.hoverId == CID_MIC_TEST);
            if (on) {
                FillRound(g, br, (REAL)7, t.dangerSoft);
                StrokeRound(g, br, (REAL)7, t.danger, 1.0f);
                TextCenter(g, br, st.T(L"停止测试", L"Stop test"), MakeF(S(11), true), t.danger);
            } else {
                FillRound(g, br, (REAL)7, hov ? t.surfaceHi : t.bg);
                StrokeRound(g, br, (REAL)7, hov ? t.borderHi : t.border, 1.0f);
                TextCenter(g, br, st.T(L"开始测试", L"Start test"), MakeF(S(11)), t.text);
            }
        }

        // ---- 电平条 ----
        int barX = card.X + S(14);
        int barW = L.contentW - S(28) - S(64);
        int barY = L.micTestY + S(46);
        int barH = S(12);
        FillRound(g, RectF((REAL)barX, (REAL)barY, (REAL)barW, (REAL)barH),
                  (REAL)6, t.bg);

        auto dbToPx = [&](float db) {
            if (db < -60.0f) db = -60.0f;
            if (db > 0.0f) db = 0.0f;
            return (int)((db + 60.0f) / 60.0f * barW);
        };
        if (on) {
            int fillW = dbToPx(st.micPeakDb);
            if (fillW > 2) {
                // 超过 -9dB 转黄，-2dB 转红，提示可能爆音
                Color c = (st.micPeakDb > -2.0f) ? t.danger
                        : (st.micPeakDb > -9.0f) ? t.warn : t.success;
                FillRound(g, RectF((REAL)barX, (REAL)barY, (REAL)fillW, (REAL)barH),
                          (REAL)6, c);
            }
            // 峰值保持标记
            int holdX = dbToPx(st.micPeakHoldDb);
            if (st.micPeakHoldDb > -60.0f) {
                SolidBrush hb(t.textDim);
                g.FillRectangle(&hb, (REAL)(barX + holdX - 1), (REAL)(barY - 3),
                                (REAL)2, (REAL)(barH + 6));
            }
        }

        // ---- 右侧数值 ----
        {
            wchar_t buf[48];
            if (!on) wcscpy(buf, L"—");
            else if (st.micPeakDb <= -60.0f) wcscpy(buf, L"—");
            else swprintf(buf, 48, L"%.0f dB", (double)st.micPeakDb);
            StringFormat sf; sf.SetAlignment(StringAlignmentFar);
            sf.SetLineAlignment(StringAlignmentNear);
            SolidBrush b(on ? t.text : t.textFaint);
            Font fDb = MakeF(S(12), true);
            g.DrawString(buf, -1, &fDb,
                         RectF(card.X + card.Width - (REAL)S(60), (REAL)(barY - 3),
                               (REAL)S(46), (REAL)S(18)), &sf, (Brush*)&b);
        }

        // ---- 判定 / 说明 ----
        {
            const wchar_t* verdict;
            const wchar_t* tip = L"";
            Color vc = t.textFaint;
            if (!on) {
                verdict = st.T(L"测试未开始", L"Test is off");
                tip = st.T(L"点「开始测试」才会占用麦克风，平时不录音", 
                           L"Nothing is captured until you start the test");
            } else {
                verdict = st.T(L"未检测到输入", L"No input detected");
                tip = st.T(L"对着麦克风说句话试试", L"Say something to test");
                if (st.micActive) {
                    verdict = st.T(L"有信号 ✓", L"Signal OK ✓");
                    // 有信号时把"正在测哪个设备"显示出来
                    tip = st.micDevName.empty() ? L"" : st.micDevName.c_str();
                    vc = t.success;
                } else if (st.micSilentLong) {
                    // 连续完全没信号：最常见原因是麦克风杆抬起（H3PRO 抬起即静音）或已拆下
                    verdict = st.T(L"完全没有信号", L"No signal at all");
                    tip = st.T(L"麦克风杆抬起即静音；也确认麦克风是否已拆下",
                               L"Boom up = muted; also check the mic is attached");
                    vc = t.danger;
                } else if (st.micPeakDb > -55.0f) {
                    verdict = st.T(L"信号很弱", L"Very weak signal");
                    vc = t.warn;
                }
            }
            int vy = L.micTestY + S(68);
            TextLeft(g, card.X + (REAL)S(14), (REAL)vy, verdict, MakeF(S(11)), vc);
            if (tip[0])
                TextLeft(g, card.X + (REAL)S(150), (REAL)vy, tip, MakeF(S(11)), t.textFaint);
        }
    }

    // ================= 麦克风处理 =================
    SectionTitle(g, (REAL)L.contentX, (REAL)(L.micCtlY - L.titleH + S(4)),
                 st.T(L"麦克风处理", L"Microphone processing"), fSec);

    // ★ 这四项都是 APO（Windows 音频驱动层）的处理，与耳机 DSP 无关。
    //   hpfAvailability / frcAvailability 为空只表示"耳机不回读状态"，不代表不能用。
    //   代价：下发后无法回读，所以界面用本地记录值显示（st.micOverride）。
    struct MicDef {
        const wchar_t* zh; const wchar_t* en;
        const wchar_t* hzh; const wchar_t* hen;
        int action; int cur;
    };
    int devVal[4] = {
        dev ? dev->micNrEnabled : 0,
        dev ? dev->micHpfEnabled : 0,
        dev ? dev->micFrcEnabled : 0,
        dev ? dev->micMetersEnabled : 0,
    };
    MicDef micDefs[4] = {
        { L"降噪", L"Noise reduction",
          L"抑制风扇、空调这类稳态底噪", L"Suppresses steady noise (fans, AC)",
          ACT_MIC_NR,     st.micOverride[0] >= 0 ? st.micOverride[0] : devVal[0] },
        { L"高通滤波", L"High-pass",
          L"切掉 80Hz 以下低频，减少轰隆声", L"Cuts rumble below ~80 Hz",
          ACT_MIC_HPF,    st.micOverride[1] >= 0 ? st.micOverride[1] : devVal[1] },
        { L"FRC 语音清晰", L"FRC clarity",
          L"增强人声清晰度，适合远场拾音", L"Boosts speech clarity for far-field use",
          ACT_MIC_FRC,    st.micOverride[2] >= 0 ? st.micOverride[2] : devVal[2] },
        { L"电平表", L"Level meter",
          L"让队友看到你的麦克风电平", L"Shows your mic level to teammates",
          ACT_MIC_METERS, st.micOverride[3] >= 0 ? st.micOverride[3] : devVal[3] },
    };

    Font fSeg = MakeF(S(11));
    bool canControl = (dev && dev->IsHeadset());
    for (int k = 0; k < 4; k++) {
        int ry = L.micCtlY + k * L.micRowH;
        RectF sr((REAL)(L.contentX + L.contentW - L.segW), (REAL)(ry + S(2)),
                 (REAL)L.segW, (REAL)S(26));
        std::vector<SegItem> items = { {st.T(L"关",L"Off"), canControl},
                                       {st.T(L"开",L"On"),  canControl} };
        int sel = canControl ? (micDefs[k].cur ? 1 : 0) : -1;
        DrawSeg(g, sr, items, sel, fSeg);

        // 标签（与控件居中对齐）+ 下方一行 hint
        StringFormat sfL; sfL.SetAlignment(StringAlignmentNear); sfL.SetLineAlignment(StringAlignmentCenter);
        SolidBrush bL(canControl ? t.text : t.textFaint);
        Font fName = MakeF(S(12));
        g.DrawString(st.T(micDefs[k].zh, micDefs[k].en), -1, &fName,
                     RectF((REAL)L.contentX, (REAL)ry, (REAL)L.labelW, (REAL)S(26)),
                     &sfL, (Brush*)&bL);

        SolidBrush bH(t.textFaint);
        Font fHint = MakeF(S(11));
        g.DrawString(st.T(micDefs[k].hzh, micDefs[k].hen), -1, &fHint,
                     RectF((REAL)L.contentX, (REAL)(ry + S(22)),
                           (REAL)(L.contentW - L.segW - S(16)), (REAL)S(18)),
                     &sfL, (Brush*)&bH);
    }

    // ================= 麦克风音量 =================
    SectionTitle(g, (REAL)L.contentX, (REAL)(L.micVolY - L.titleH + S(4)),
                 st.T(L"麦克风音量", L"Microphone volume"), fSec);
    {
        int ry = L.micVolY;
        wchar_t buf[16];
        if (st.micVol >= 0) swprintf(buf, 16, L"%d", st.micVol);
        else wcscpy(buf, L"--");
        RectF tr((REAL)L.trackX, (REAL)(ry + S(3)), (REAL)L.trackW, (REAL)S(22));
        Slider(g, tr, st.micVol >= 0 ? st.micVol / 100.0 : 0.0, MakeF(S(11)), buf);

        StringFormat sfL; sfL.SetAlignment(StringAlignmentNear); sfL.SetLineAlignment(StringAlignmentCenter);
        SolidBrush bL(t.textDim);
        Font fL3 = MakeF(S(12));
        g.DrawString(st.T(L"采集音量", L"Capture level"), -1, &fL3,
                     RectF((REAL)L.contentX, (REAL)ry, (REAL)L.labelW, (REAL)S(28)),
                     &sfL, (Brush*)&bL);
    }

    // ================= 底部说明 =================
    {
        int ny = L.micVolY + S(34);
        Font fNote = MakeF(S(11));
        StringFormat sfN;
        SolidBrush bN(t.textFaint);
        g.DrawString(st.T(
            L"说明：降噪 / 高通 / FRC 走音频驱动下发（与官方软件同一通道），"
            L"但耳机不回读开关状态，所以这里显示的是你最后一次设置的值。",
            L"Note: NR / HPF / FRC are pushed through the audio driver (same path as the "
            L"official app). The headset does not report their state back, so the switch "
            L"shows the last value you set."),
            -1, &fNote,
            RectF((REAL)L.contentX, (REAL)ny, (REAL)L.contentW, (REAL)S(40)),
            &sfN, (Brush*)&bN);
        st.contentHeight = (float)(ny + S(40) - L.contentTop + S(20));
    }
}

// ---------------- 输出设备页 ----------------
inline void DrawDevices(Graphics& g, AppState& st) {
    const Metrics& m = M();
    const int dd = m.dpi;
    auto S = [dd](int v) { return MulDiv(v, dd, 96); };
    const Theme& t = eui::T();
    Layout L = ComputeLayout(st);

    Font fSec = MakeF(S(13), true);

    // ---- 输出 / 输入 标签 ----
    {
        RectF tabR((REAL)L.contentX, (REAL)L.devTabY, (REAL)S(220), (REAL)L.devTabH);
        std::vector<SegItem> items = {
            { st.T(L"播放设备", L"Playback"), true },
            { st.T(L"录音设备", L"Recording"), true },
        };
        DrawSeg(g, tabR, items, st.devTab, MakeF(S(11)));
    }

    bool isCapture = (st.devTab == 1);
    std::vector<Endpoint>* list = isCapture ? st.captureEndpoints : st.endpoints;
    const std::wstring& curId = isCapture ? st.currentInputId : st.currentOutputId;

    // ★ 不再重复一遍"播放设备/录音设备"（上面的标签已经写明了），
    //   这里改成一句用途说明，信息密度更高。
    {
        TextLeft(g, (REAL)L.contentX, (REAL)(L.devListY - L.titleH + S(6)),
                 isCapture ? st.T(L"当前默认录音设备标记为「当前默认」，点其它卡片即切换",
                                  L"The device marked Default is in use — click another to switch")
                           : st.T(L"当前默认播放设备标记为「当前默认」，点其它卡片即切换",
                                  L"The device marked Default is in use — click another to switch"),
                 MakeF(S(11)), t.textFaint);
    }

    if (!list) {
        st.contentHeight = (float)(L.devListY - L.contentTop + S(60));
        return;
    }

    // ★ 行序/行矩形全部来自 DevLayout()，和 HitTest 用同一个函数，杜绝错位
    DevRows dr = DevLayout(st, L, isCapture);
    bool hasVol = ((int)st.devVol.size() == (int)dr.idx.size() &&
                   (int)st.devMuted.size() == (int)dr.idx.size());

    int visibleIdx = 0;
    for (int r = 0; r < (int)dr.idx.size(); r++) {
        const Endpoint& ep = (*list)[dr.idx[r]];

        // 虚拟设备分组标题
        if (r == 0 || (dr.groupTitleY >= 0 && visibleIdx > 0 &&
                       (*list)[dr.idx[r]].kind == KIND_VIRTUAL &&
                       (*list)[dr.idx[r - 1]].kind != KIND_VIRTUAL)) {
            TextLeft(g, (REAL)L.contentX, (REAL)dr.groupTitleY,
                     isCapture ? st.T(L"虚拟设备 / 其它录音", L"Virtual & other inputs")
                               : st.T(L"虚拟设备 / 其它输出", L"Virtual & other outputs"),
                     MakeF(S(11), true), t.textFaint);
        }

        const RectF& rct = dr.row[r];
        bool cur = (ep.id == curId);
        bool hov = (st.hoverId == CID_ACTION && st.actionHint == ACT_OUTPUT &&
                    st.hoverIndex == visibleIdx);
        FillRound(g, rct, (REAL)8, hov ? t.surfaceHi : (cur ? t.accentSoft : t.surface));
        StrokeRound(g, rct, (REAL)8, cur ? t.accent : t.border, cur ? 1.4f : 1.0f);

        RectF ic(rct.X + (REAL)S(12), rct.Y + (REAL)S(15), (REAL)S(26), (REAL)S(26));
        FillRound(g, ic, (REAL)13, cur ? t.accent : t.surfaceHi);
        IconId dic = ICON_MUSIC;
        if (ep.kind == KIND_BT)           dic = ICON_BT;
        else if (ep.kind == KIND_EPOS)    dic = isCapture ? ICON_MIC : ICON_HEADSET;
        else if (ep.kind == KIND_REALTEK) dic = ICON_CHIP;
        else if (ep.kind == KIND_VIRTUAL) dic = ICON_SLIDERS;
        DrawIcon(g, RectF(ic.X + ic.Width * 0.28f, ic.Y + ic.Height * 0.28f,
                          ic.Width * 0.44f, ic.Height * 0.44f),
                 dic, cur ? Color(255,255,255,255) : t.textDim);

        Font fN = MakeF(S(13), cur);
        TextLeft(g, rct.X + (REAL)S(48), rct.Y + (REAL)S(9), ep.name.c_str(), fN, t.text);

        // 第二行：类型 · 采样率格式
        std::wstring sub;
        switch (ep.kind) {
        case KIND_BT:      sub = st.T(L"蓝牙", L"Bluetooth"); break;
        case KIND_EPOS:    sub = isCapture ? st.T(L"2.4 GHz 无线耳机麦克风", L"2.4 GHz headset mic")
                                           : st.T(L"2.4 GHz 无线耳机", L"2.4 GHz wireless headset"); break;
        case KIND_REALTEK: sub = st.T(L"主板声卡", L"Mainboard audio"); break;
        case KIND_VIRTUAL: {
            // 虚拟设备也分种类，别都叫"虚拟"
            if (ep.name.find(L"Broadcast") != std::wstring::npos)
                sub = st.T(L"虚拟设备 · AI 降噪链", L"Virtual · AI noise cleanup");
            else if (ep.name.find(L"High Definition") != std::wstring::npos)
                sub = st.T(L"HDMI / DP 音频", L"HDMI / DP audio");
            else
                sub = st.T(L"虚拟设备", L"Virtual device");
            break;
        }
        default:           sub = st.T(L"其他", L"Other"); break;
        }
        if (!ep.fmt.empty()) sub += L"  ·  " + ep.fmt;
        // 蓝牙耳机：显示电量
        if (ep.kind == KIND_BT && ep.batt >= 0)
            sub += L"  ·  电量 " + std::to_wstring(ep.batt) + L"%";
        TextLeft(g, rct.X + (REAL)S(48), rct.Y + (REAL)S(31), sub.c_str(), MakeF(S(11)), t.textDim);

        // ---- 右侧：音量条 + 百分比 + 静音（当前默认设备不再占这一块）----
        if (hasVol) {
            int vol = st.devVol[r];
            bool muted = st.devMuted[r];

            // 轨道
            const RectF& tr = dr.track[r];
            FillRound(g, tr, (REAL)4, t.bg);
            if (vol > 0 && !muted) {
                RectF fill(tr.X, tr.Y, tr.Width * (vol / 100.0f), tr.Height);
                FillRound(g, fill, (REAL)4, cur ? t.accent : t.teal);
            }
            // 滑块头
            REAL hx = tr.X + tr.Width * (vol / 100.0f);
            Dot(g, hx, tr.Y + tr.Height / 2, (REAL)S(4),
                muted ? t.textFaint : (cur ? t.accent : t.text));

            // 百分比
            wchar_t vb[16];
            swprintf(vb, 16, L"%d%%", vol);
            TextCenter(g, dr.pct[r], vb, MakeF(S(10)),
                       muted ? t.textFaint : (cur ? t.text : t.textDim));

            // 静音按钮
            const RectF& mb = dr.mute[r];
            bool mhov = (st.hoverId == CID_ACTION && st.actionHint == ACT_DEV_MUTE &&
                         st.hoverIndex == visibleIdx);
            if (mhov) FillRound(g, mb, (REAL)7, t.surfaceHi);
            DrawIcon(g, RectF(mb.X + (REAL)S(5), mb.Y + (REAL)S(5),
                              mb.Width - (REAL)S(10), mb.Height - (REAL)S(10)),
                     muted ? ICON_MUTE : ICON_VOLUME,
                     muted ? t.danger : (mhov ? t.text : t.textDim));
        } else if (cur) {
            // 音量数据还没刷新出来时，至少把"当前默认"标出来
            StringFormat sf; sf.SetAlignment(StringAlignmentFar); sf.SetLineAlignment(StringAlignmentCenter);
            SolidBrush b(t.accent);
            RectF vr(rct.X + rct.Width - (REAL)S(150), rct.Y, (REAL)S(136), rct.Height);
            Font fCur = MakeF(S(12), true);
            g.DrawString(isCapture ? st.T(L"当前默认", L"Default input")
                                   : st.T(L"当前默认", L"Default output"),
                         -1, &fCur, vr, &sf, (Brush*)&b);
        }
        visibleIdx++;
    }

    // 窄带提示：16kHz 单声道通常是"语音通话"模式，录出来会明显发闷
    bool narrow = false;
    for (const auto& ep : *list) {
        if (ep.fmt.empty()) continue;
        if (ep.fmt.find(L"16000 Hz") != std::wstring::npos) narrow = true;
    }
    // 行循环结束后的 y（含虚拟分组标题）；DevRows 里能算出来
    int y = L.devListY;
    if (!dr.idx.empty()) {
        y = (int)dr.row.back().Y + (int)dr.row.back().Height + S(6);
    }
    if (narrow) {
        RectF nb((REAL)L.contentX, (REAL)(y + S(2)), (REAL)L.contentW, (REAL)S(30));
        FillRound(g, nb, (REAL)8, t.surface);
        RectF bar(nb.X, nb.Y + (REAL)S(6), (REAL)3, nb.Height - (REAL)S(12));
        FillRound(g, bar, (REAL)1.5f, t.warn);
        TextLeft(g, nb.X + (REAL)S(12), nb.Y + (REAL)S(7),
                 st.T(L"检测到 16 kHz 单声道设备：这是「语音通话」模式，录音会明显发闷。"
                      L"可在 Windows 声音设置里改用 48 kHz 立体声。",
                      L"A 16 kHz mono device was found — that is voice-call mode and sounds muffled. "
                      L"Switch it to 48 kHz stereo in Windows sound settings."),
                 MakeF(S(11)), t.warn);
        y += S(36);
    }

    // 底部说明
    {
        int ny = y + S(6);
        Font fNote = MakeF(S(11));
        StringFormat sfN;
        SolidBrush bN(t.textFaint);
        g.DrawString(st.T(
            L"提示：点击卡片即切换默认设备（同时作用于「默认」「通话」「多媒体」三种用途）。"
            L"采样率由设备自身能力决定，Windows 会按设备的混音格式输出。",
            L"Tip: click a card to make it the default (applies to Default, Communications "
            L"and Multimedia roles). The sample rate comes from the device's mix format."),
            -1, &fNote,
            RectF((REAL)L.contentX, (REAL)ny, (REAL)L.contentW, (REAL)S(36)),
            &sfN, (Brush*)&bN);
        st.contentHeight = (float)(ny + S(36) - L.contentTop + S(20));
    }
}

// ---------------- 混音器页（每应用音量） ----------------
inline void DrawMixer(Graphics& g, AppState& st) {
    const Metrics& m = M();
    const int dd = m.dpi;
    auto S = [dd](int v) { return MulDiv(v, dd, 96); };
    const Theme& t = eui::T();
    Layout L = ComputeLayout(st);

    SectionTitle(g, (REAL)L.contentX, (REAL)(L.mixY - L.titleH),
                 st.T(L"音量混音器", L"Volume mixer"), MakeF(S(13), true));

    if (st.mix.empty()) {
        TextLeft(g, (REAL)L.contentX, (REAL)L.mixY,
                 st.T(L"当前没有应用在占用扬声器。放点音乐或开个游戏再回来看看。",
                      L"No app is using the speaker right now. Play something and come back."),
                 MakeF(S(12)), t.textFaint);
        st.contentHeight = (float)(L.mixY + S(60) - L.contentTop);
        return;
    }

    int mx = L.contentX + L.contentW - S(12) - S(26);
    int px = mx - S(8) - S(40);
    int tx = px - S(8) - S(110);

    Font fN = MakeF(S(13));
    Font fS = MakeF(S(11));
    for (int i = 0; i < (int)st.mix.size(); i++) {
        const AppState::MixRow& r = st.mix[i];
        int ry = L.mixY + i * L.mixRowH;
        int cy = ry + S(21);

        TextLeft(g, (REAL)L.contentX, (REAL)(ry + S(2)), r.name.c_str(), fN, t.text);

        // 轨道
        RectF tr((REAL)tx, (REAL)(cy - S(4)), (REAL)S(110), (REAL)S(8));
        FillRound(g, tr, (REAL)4, t.bg);
        if (r.vol > 0 && !r.muted)
            FillRound(g, RectF(tr.X, tr.Y, tr.Width * (r.vol / 100.0f), tr.Height), (REAL)4, t.teal);
        Dot(g, tr.X + tr.Width * (r.vol / 100.0f), tr.Y + tr.Height / 2, (REAL)S(4),
            r.muted ? t.textFaint : t.text);

        wchar_t vb[16];
        swprintf(vb, 16, L"%d%%", r.muted ? 0 : r.vol);
        TextCenter(g, RectF((REAL)px, (REAL)(cy - S(10)), (REAL)S(40), (REAL)S(20)),
                   vb, MakeF(S(10)), r.muted ? t.textFaint : t.textDim);

        // 静音图标
        RectF mb((REAL)mx, (REAL)(cy - S(13)), (REAL)S(26), (REAL)S(26));
        bool hov = (st.hoverId == CID_ACTION && st.actionHint == ACT_MIX_MUTE &&
                    st.hoverIndex == i);
        if (hov) FillRound(g, mb, (REAL)7, t.surfaceHi);
        DrawIcon(g, RectF(mb.X + (REAL)S(5), mb.Y + (REAL)S(5),
                          mb.Width - (REAL)S(10), mb.Height - (REAL)S(10)),
                 r.muted ? ICON_MUTE : ICON_VOLUME,
                 r.muted ? t.danger : (hov ? t.text : t.textDim));
    }

    int y = L.mixY + (int)st.mix.size() * L.mixRowH + S(4);
    TextLeft(g, (REAL)L.contentX, (REAL)y,
             st.T(L"点喇叭图标 = 静音（记住之前的音量，再点恢复）；音量条只作用于这一个应用。",
                  L"Click the speaker icon to mute (volume is remembered); sliders affect that app only."),
             MakeF(S(11)), t.textFaint);
    st.contentHeight = (float)(y + S(30) - L.contentTop + S(20));
}

// ---------------- 应用规则页（profiles.txt 的界面化编辑） ----------------
inline void DrawApps(Graphics& g, AppState& st) {
    const Metrics& m = M();
    const int dd = m.dpi;
    auto S = [dd](int v) { return MulDiv(v, dd, 96); };
    const Theme& t = eui::T();
    Layout L = ComputeLayout(st);

    // ---- 总开关（与设置页同一项）----
    {
        int ry = L.appToggleY;
        bool hov = (st.hoverId == CID_ACTION && st.actionHint == ACT_SET_TOGGLE &&
                    st.hoverIndex == 5);
        if (hov) FillRound(g, RectF((REAL)L.contentX - (REAL)S(6), (REAL)(ry - S(4)),
                                    (REAL)(L.contentW + S(12)), (REAL)S(50)), (REAL)6, t.surface);
        TextLeft(g, (REAL)L.contentX, (REAL)ry, st.T(L"按应用自动切预设", L"Per-app presets"),
                 MakeF(S(13)), t.text);
        TextLeft(g, (REAL)L.contentX, (REAL)(ry + S(21)),
                 st.T(L"前台程序匹配到规则时自动应用对应 EQ 预设",
                      L"Applies the matching EQ preset when a listed app comes to the foreground"),
                 MakeF(S(11)), t.textFaint);
        REAL sw = (REAL)S(48), sh = (REAL)S(26);
        RectF sr((REAL)(L.contentX + L.contentW) - sw, (REAL)(ry + S(3)), sw, sh);
        bool on = st.setAppPreset;
        FillRound(g, sr, sh / 2, on ? t.accent : t.surfaceHi);
        REAL d2 = sh - (REAL)S(6);
        REAL kx = on ? (sr.X + sw - d2 - (REAL)S(3)) : (sr.X + (REAL)S(3));
        SolidBrush kb(on ? Color(255, 255, 255, 255) : t.textFaint);
        g.FillEllipse(&kb, kx, sr.Y + (REAL)S(3), d2, d2);
    }

    SectionTitle(g, (REAL)L.contentX, (REAL)(L.appListY - L.titleH + S(4)),
                 st.T(L"规则", L"Rules"), MakeF(S(13), true));

    Font fN = MakeF(S(12));
    Font fS = MakeF(S(11));
    for (int i = 0; i < (int)st.appRulesView.size(); i++) {
        int ry = L.appListY + i * L.appRowH;
        bool hov = (st.hoverId == CID_ACTION && st.actionHint == ACT_APP_DEL &&
                    st.hoverIndex == i);
        if (hov) FillRound(g, RectF((REAL)L.contentX - (REAL)S(6), (REAL)(ry - S(2)),
                                    (REAL)(L.contentW + S(12)), (REAL)(L.appRowH - S(4))),
                           (REAL)6, t.surface);
        // 进程名 + 预设名
        TextLeft(g, (REAL)L.contentX, (REAL)(ry + S(2)),
                 st.appRulesView[i].first.c_str(), fN, t.text);
        // 预设显示名
        std::wstring pn = st.appRulesView[i].second;
        for (const auto& p : st.presets)
            if (p.key == st.appRulesView[i].second) {
                pn = st.english ? p.en : p.zh;
                break;
            }
        std::wstring arrow = (std::wstring)L"→ " + pn;
        TextLeft(g, (REAL)L.contentX, (REAL)(ry + S(22)),
                 arrow.c_str(), fS, t.accent);

        // × 删除
        RectF xr((REAL)(L.contentX + L.contentW - S(12) - S(26)), (REAL)(ry + S(10)),
                 (REAL)S(26), (REAL)S(26));
        if (hov) FillRound(g, xr, (REAL)7, t.surfaceHi);
        DrawIcon(g, RectF(xr.X + (REAL)S(6), xr.Y + (REAL)S(6),
                          xr.Width - (REAL)S(12), xr.Height - (REAL)S(12)),
                 ICON_CLOSE, hov ? t.danger : t.textFaint);
    }
    if (st.appRulesView.empty()) {
        TextLeft(g, (REAL)L.contentX, (REAL)(L.appListY + S(6)),
                 st.T(L"还没有规则。用下面的按钮加一条：先选程序，再选它要用的预设。",
                      L"No rules yet. Add one below: pick a program, then its preset."),
                 MakeF(S(12)), t.textFaint);
    }

    // ---- 添加区 ----
    if (!st.appAddText.empty()) {
        // 已暂存进程名
        RectF chip((REAL)L.contentX, (REAL)L.appAddY, (REAL)S(220), (REAL)S(30));
        FillRound(g, chip, (REAL)7, t.accentSoft);
        StrokeRound(g, chip, (REAL)7, t.accent, 1.0f);
        TextLeftVC(g, chip, st.appAddText.c_str(), fN, t.text, (REAL)S(12));

        // 预设下拉按钮
        RectF pb((REAL)(L.contentX + L.contentW - S(12) - S(200)), (REAL)L.appAddY,
                 (REAL)S(200), (REAL)S(30));
        bool hov = (st.hoverId == CID_ACTION && st.actionHint == ACT_APP_PICKOPEN) ||
                   st.appPickOpen;
        FillRound(g, pb, (REAL)7, hov ? t.surfaceHi : t.surface);
        StrokeRound(g, pb, (REAL)7, st.appPickOpen ? t.accent : t.border, 1.0f);
        TextLeftVC(g, pb, st.T(L"选择预设…", L"Choose preset…"), fN, t.text, (REAL)S(10));
        RectF ico(pb.X + pb.Width - (REAL)S(22), pb.Y + (REAL)S(7), (REAL)S(16), (REAL)S(16));
        DrawIcon(g, ico, ICON_CHEVRON_DOWN, t.textDim);

        TextLeft(g, (REAL)(L.contentX + S(236)), (REAL)(L.appAddY + S(7)),
                 st.T(L"点进程名可重新输入", L"Click the name to retype it"),
                 fS, t.textFaint);
    }

    // 两个来源按钮
    {
        int halfW = (L.contentW - S(10)) / 2;
        struct B { const wchar_t* zh; const wchar_t* en; int act; };
        B bs[2] = {
            { st.T(L"捕获前台程序", L"Capture foreground app"), L"", ACT_APP_CAPTURE },
            { st.T(L"手动输入进程名", L"Type a process name"), L"", ACT_APP_MANUAL },
        };
        for (int k = 0; k < 2; k++) {
            REAL bx = (REAL)(L.contentX + k * (halfW + S(10)));
            RectF br(bx, (REAL)L.appBtnY, (REAL)halfW, (REAL)S(34));
            bool hov = (st.hoverId == CID_ACTION && st.actionHint == bs[k].act);
            FillRound(g, br, (REAL)8, hov ? t.surfaceHi : t.surface);
            StrokeRound(g, br, (REAL)8, t.border, 1.0f);
            TextCenter(g, br, bs[k].zh, MakeF(S(12)), hov ? t.text : t.textDim);
        }
    }

    // 输入态提示 / 说明
    int ny = L.appBtnY + S(44);
    if (st.appAddMode) {
        std::wstring tip = st.T(L"直接输入进程名（如 cs2.exe），Enter 确认，Esc 取消。当前：",
                                L"Type the process name (e.g. cs2.exe), Enter to confirm, Esc cancels. Now: ");
        TextLeft(g, (REAL)L.contentX, (REAL)ny, (tip + st.appAddText).c_str(),
                 MakeF(S(12)), t.accent);
    } else {
        TextLeft(g, (REAL)L.contentX, (REAL)ny,
                 st.T(L"进程名 = 任务管理器里的「名称」+ .exe，小写匹配。规则保存在 profiles.txt。",
                      L"Process name = the name in Task Manager + \".exe\", matched lowercase. Rules persist in profiles.txt."),
                 MakeF(S(11)), t.textFaint);
    }
    st.contentHeight = (float)(ny + S(30) - L.contentTop + S(20));
}

// 应用规则页的预设下拉浮层（最后画）
inline void DrawAppPickPopup(Graphics& g, AppState& st) {
    if (!st.appPickOpen) return;
    const Metrics& m = M();
    const int dd = m.dpi;
    auto S = [dd](int v) { return MulDiv(v, dd, 96); };
    const Theme& t = eui::T();
    Layout L = ComputeLayout(st);
    int n = (int)st.presets.size(); if (n > 12) n = 12;
    RectF pr((REAL)(L.contentX + L.contentW - S(12) - S(200)),
             (REAL)(L.appAddY + S(34)), (REAL)S(200), (REAL)(n * S(30) + S(8)));
    RectF sh(pr.X - 1, pr.Y + 2, pr.Width + 2, pr.Height + 3);
    FillRound(g, sh, (REAL)9, Color(90, 0, 0, 0));
    FillRound(g, pr, (REAL)8, t.surfaceHi);
    StrokeRound(g, pr, (REAL)8, t.borderHi, 1.0f);

    Font f = MakeF(S(12));
    for (int i = 0; i < n; i++) {
        RectF ir(pr.X + (REAL)S(4), pr.Y + (REAL)S(4) + (REAL)(i * S(30)),
                 pr.Width - (REAL)S(8), (REAL)S(30));
        bool hov = (st.hoverId == CID_APP_PICK && st.hoverIndex == i);
        if (hov) FillRound(g, ir, (REAL)6, t.surface);
        const eposfx::Preset& p = st.presets[i];
        TextLeftVC(g, ir, st.english ? p.en.c_str() : p.zh.c_str(), f, t.text, (REAL)S(10));
        TextRightVC(g, ir, p.key.c_str(), MakeF(S(10)), t.textFaint, (REAL)S(10));
    }
}

// ---------------- 设置页 ----------------
inline void DrawSettings(Graphics& g, AppState& st) {
    const Metrics& m = M();
    const int dd = m.dpi;
    auto S = [dd](int v) { return MulDiv(v, dd, 96); };
    const Theme& t = eui::T();
    Layout L = ComputeLayout(st);
    Font fSec = MakeF(S(13), true);

    SectionTitle(g, (REAL)L.contentX, (REAL)(L.setY - L.titleH + S(4)),
                 st.T(L"常规", L"General"), fSec);

    struct Row {
        const wchar_t* zh; const wchar_t* en;
        const wchar_t* hzh; const wchar_t* hen;
        bool val;
    };
    Row rows[8] = {
        { L"开机自动启动", L"Launch at sign-in",
          L"登录 Windows 后自动在托盘运行", L"Start silently in the tray after you sign in",
          st.setAutostart },
        { L"关闭按钮收进托盘", L"Close to tray",
          L"关掉窗口后继续在后台工作（托盘右键可退出）", L"Keep running in the background; quit from the tray menu",
          st.setCloseToTray },
        { L"自动切换输出设备", L"Auto-switch output",
          L"耳机连上时自动切成默认播放设备", L"Make the headset the default output when it connects",
          st.setAutoSwitch },
        { L"耳机重连后切回", L"Restore on reconnect",
          L"断线前在用耳机的话，重连后自动切回", L"Switch back after a reconnect if it was in use",
          st.setAutoRestore },
        { L"低电量提醒", L"Low battery alert",
          L"电量 20% 和 10% 各弹一次系统通知", L"One notification at 20% and another at 10%",
          st.setBattAlert },
        { L"按应用自动切预设", L"Per-app presets",
          L"按前台程序自动换 EQ 预设（规则在 profiles.txt）", L"Switch EQ presets by foreground app (see profiles.txt)",
          st.setAppPreset },
        { L"启用全局快捷键", L"Global hotkeys",
          L"关掉后所有组合键都不再注册，不占用系统热键", L"Turn off to release all hotkeys back to Windows",
          st.setHotkeys },
        { L"音量浮层提示", L"Volume overlay",
          L"调音量时在右下角弹出提示；Windows 自带的已够用，可关",
          L"Show a card in the corner when volume changes; Windows already has one",
          st.setVolOsd },
    };

    bool values[8] = { st.setAutostart, st.setCloseToTray, st.setAutoSwitch,
                       st.setAutoRestore, st.setBattAlert, st.setAppPreset,
                       st.setHotkeys, st.setVolOsd };

    for (int k = 0; k < L.setN && k < 8; k++) {
        int ry = L.setY + k * L.setRowH;
        bool hov = (st.hoverId == CID_ACTION && st.actionHint == ACT_SET_TOGGLE &&
                    st.hoverIndex == k);
        if (hov) FillRound(g, RectF((REAL)L.contentX - (REAL)S(6), (REAL)(ry - S(4)),
                                    (REAL)(L.contentW + S(12)), (REAL)(L.setRowH - S(4))),
                           (REAL)6, t.surface);

        Font fName = MakeF(S(13));
        TextLeft(g, (REAL)L.contentX, (REAL)ry, st.T(rows[k].zh, rows[k].en), fName, t.text);
        Font fHint = MakeF(S(11));
        TextLeft(g, (REAL)L.contentX, (REAL)(ry + S(21)),
                 st.T(rows[k].hzh, rows[k].hen), fHint, t.textFaint);

        // 开关：开启时用强调色实底 + 白字，关闭时中性底 + 灰字。
        // 直接用 DrawSeg 的话 selected 只差一个很浅的底色，一眼看不出来。
        {
            REAL sw = (REAL)S(48), sh = (REAL)S(26);
            RectF sr((REAL)(L.contentX + L.contentW) - sw, (REAL)(ry + S(3)), sw, sh);
            bool on = values[k];
            FillRound(g, sr, sh / 2, on ? t.accent : t.surfaceHi);
            // 小圆点
            REAL d2 = sh - (REAL)S(6);
            REAL kx = on ? (sr.X + sw - d2 - (REAL)S(3)) : (sr.X + (REAL)S(3));
            SolidBrush kb(on ? Color(255, 255, 255, 255) : t.textFaint);
            g.FillEllipse(&kb, kx, sr.Y + (REAL)S(3), d2, d2);
            // 文字放在圆点另一侧
            Font fSw = MakeF(S(10), true);
            RectF tr(on ? RectF(sr.X + (REAL)S(4), sr.Y, sw - d2 - (REAL)S(8), sh)
                        : RectF(sr.X + d2 + (REAL)S(6), sr.Y, sw - d2 - (REAL)S(8), sh));
            TextCenter(g, tr, on ? st.T(L"开", L"ON") : st.T(L"关", L"OFF"), fSw,
                       on ? Color(255, 255, 255, 255) : t.textDim);
        }
    }

    // ---- 界面语言：整行列表（比分段控件好点、也好加第三种语言）----
    SectionTitle(g, (REAL)L.contentX, (REAL)(L.setLangTitleY + S(4)),
                 st.T(L"界面语言", L"Language"), fSec);
    {
        const wchar_t* names[2]   = { L"中文", L"English" };
        const wchar_t* subs[2]    = { st.english ? L"Chinese (Simplified)" : L"简体中文界面",
                                      st.english ? L"English interface" : L"英文界面" };
        for (int i = 0; i < 2; i++) {
            int ry = L.setLangY + i * L.setLangRowH;
            RectF r((REAL)L.contentX, (REAL)ry, (REAL)L.contentW, (REAL)(L.setLangRowH - S(2)));
            bool sel = st.english == (i == 1);
            bool hov = (st.hoverId == CID_SET_LANG_ROW && st.hoverIndex == i);
            FillRound(g, r, (REAL)7, sel ? t.accentSoft : (hov ? t.surfaceHi : t.surface));
            if (sel) StrokeRound(g, r, (REAL)7, t.accent, 1.2f);
            TextLeftVC(g, r, names[i], MakeF(S(12)),
                       sel ? t.text : t.textDim, (REAL)S(12));
            TextRightVC(g, r, subs[i], MakeF(S(10)),
                        sel ? t.accent : t.textFaint, (REAL)S(14));
        }
    }

    // ---- 全局快捷键 ----
    SectionTitle(g, (REAL)L.contentX, (REAL)(L.setHkTitleY + S(4)),
                 st.T(L"全局快捷键", L"Global hotkeys"), fSec);
    {
        const wchar_t* names[3] = {
            st.T(L"切换播放设备", L"Cycle playback device"),
            st.T(L"扬声器静音",   L"Mute speakers"),
            st.T(L"循环切换 EQ 预设", L"Cycle EQ presets"),
        };
        Font fName = MakeF(S(12));
        Font fKey  = MakeF(S(11));
        for (int i = 0; i < 3; i++) {
            int ry = L.setHkY + i * L.setHkRowH;
            bool editing = (st.hkEditing == i);

            TextLeftVC(g, RectF((REAL)L.contentX, (REAL)ry,
                                (REAL)(L.hkComboX - L.contentX - S(8)), (REAL)S(28)),
                       names[i], fName, st.setHotkeys ? t.textDim : t.textFaint);

            // 组合键按钮
            RectF cb((REAL)L.hkComboX, (REAL)(ry + S(3)), (REAL)L.hkComboW, (REAL)S(28));
            bool hov = (st.hoverId == CID_HK_EDIT && st.hoverIndex == i);
            Color bgc = editing ? t.accentSoft : (hov ? t.surfaceHi : t.surface);
            FillRound(g, cb, (REAL)7, bgc);
            StrokeRound(g, cb, (REAL)7,
                        editing ? t.accent : (hov ? t.borderHi : t.border),
                        editing ? 1.4f : 1.0f);
            const wchar_t* txt;
            Color tc;
            if (editing) { txt = st.T(L"按下新的组合键…", L"Press new combo…"); tc = t.accent; }
            else if (!st.setHotkeys) { txt = st.T(L"已停用", L"Disabled"); tc = t.textFaint; }
            else if (st.hk[i].text.empty()) { txt = st.T(L"未设置", L"Not set"); tc = t.textFaint; }
            else { txt = st.hk[i].text.c_str(); tc = st.hk[i].ok ? t.text : t.danger; }
            TextCenter(g, cb, txt, fKey, tc);

            // 清除按钮
            RectF clr((REAL)L.hkClearX, (REAL)(ry + S(3)), (REAL)L.hkClearW, (REAL)S(28));
            bool chov = (st.hoverId == CID_HK_CLEAR && st.hoverIndex == i);
            if (chov) FillRound(g, clr, (REAL)7, t.surfaceHi);
            DrawIcon(g, RectF(clr.X + (REAL)S(6), clr.Y + (REAL)S(6),
                              (REAL)S(16), (REAL)S(16)),
                     ICON_CLOSE, chov ? t.danger : t.textFaint);
        }

        // 录制提示 / 错误提示
        int msgY = L.setHkY + 3 * L.setHkRowH + S(2);
        Font fMsg = MakeF(S(10));
        if (st.hkEditing >= 0) {
            TextLeft(g, (REAL)L.contentX, (REAL)msgY,
                     st.T(L"请按下「Ctrl / Alt / Shift + 字母或 F1–F12」，Esc 取消，Backspace 清除",
                          L"Press Ctrl/Alt/Shift + letter or F1–F12. Esc cancels, Backspace clears"),
                     fMsg, t.accent);
        } else if (!st.hkMsg.empty()) {
            TextLeft(g, (REAL)L.contentX, (REAL)msgY, st.hkMsg.c_str(), fMsg, t.warn);
        } else {
            TextLeft(g, (REAL)L.contentX, (REAL)msgY,
                     st.T(L"点组合键即可重新录制；右侧 × 清除该项",
                          L"Click a combo to re-record; × clears it"),
                     fMsg, t.textFaint);
        }
    }

    // ---- 关于 / 状态 ----
    SectionTitle(g, (REAL)L.contentX, (REAL)(L.setInfoTitleY + S(4)),
                 st.T(L"状态", L"Status"), fSec);
    {
        struct KV { const wchar_t* zh; const wchar_t* en; std::wstring v; };
        std::vector<KV> kvs;
        kvs.push_back({ L"后台服务", L"Service",
                        st.pipeReady ? st.T(L"已连接", L"Connected")
                                     : st.T(L"未连接（官方软件可能正在运行）", L"Offline (official app may hold the pipe)") });
        kvs.push_back({ L"耳机", L"Headset",
                        st.selectedDevice >= 0 ? st.T(L"已识别", L"Detected")
                                               : st.T(L"未识别", L"Not detected") });
        kvs.push_back({ L"配置文件", L"Config", L"settings.ini / profiles.txt" });

        Font fL = MakeF(S(12));
        Font fV = MakeF(S(12), true);
        int yy = L.setInfoY;
        for (size_t i = 0; i < kvs.size(); i++) {
            KeyValueRow(g, (REAL)L.contentX, (REAL)yy, (REAL)L.contentW, (REAL)S(30),
                        st.T(kvs[i].zh, kvs[i].en), kvs[i].v, fL, fV, (i % 2) == 0);
            yy += S(30);
        }
        st.contentHeight = (float)(yy - L.contentTop + S(30));
    }
}


// ---------------- 固件页 ----------------
inline void DrawFirmware(Graphics& g, AppState& st) {
    const Metrics& m = M();
    const int dd = m.dpi;
    auto S = [dd](int v) { return MulDiv(v, dd, 96); };
    const Theme& t = eui::T();
    Layout L = ComputeLayout(st);

    Font fSec = MakeF(S(13), true);
    int y = L.fwY;

    const eposdev::DeviceInfo* dev = nullptr;
    if (st.selectedDevice >= 0 && st.selectedDevice < (int)st.devices.size())
        dev = &st.devices[st.selectedDevice];

    SectionTitle(g, (REAL)L.contentX, (REAL)y, st.T(L"固件版本", L"Firmware versions"), fSec);
    y += L.titleH;

    struct Row { const wchar_t* label; std::wstring value; };
    std::vector<Row> rows;
    if (dev) {
        rows.push_back({ st.T(L"耳机固件", L"Headset firmware"),
                         dev->firmwareVersion.empty() ? L"—" : dev->firmwareVersion });
        rows.push_back({ st.T(L"接收器固件", L"Dongle firmware"),
                         dev->dongleFirmwareVersion.empty() ? L"—" : dev->dongleFirmwareVersion });
        rows.push_back({ st.T(L"序列号", L"Serial number"),
                         dev->serialNumber.empty() ? L"—" : dev->serialNumber });
        rows.push_back({ st.T(L"接收器序列号", L"Dongle serial"),
                         dev->dongleSerialNumber.empty() ? L"—" : dev->dongleSerialNumber });
    } else {
        rows.push_back({ st.T(L"状态", L"Status"), st.T(L"未检测到设备", L"No device") });
    }
    Font fRowL = MakeF(S(12));
    Font fRowV = MakeF(S(12), true);
    for (size_t i = 0; i < rows.size(); i++) {
        KeyValueRow(g, (REAL)L.contentX, (REAL)y, (REAL)L.contentW, (REAL)S(32),
                    rows[i].label, rows[i].value, fRowL, fRowV, (i % 2) == 0);
        y += S(32);
    }
    y += S(m.blockGap);

    // 更新检查
    SectionTitle(g, (REAL)L.contentX, (REAL)y, st.T(L"更新", L"Updates"), fSec);
    y += L.titleH;
    RectF br((REAL)L.contentX, (REAL)y, (REAL)S(180), (REAL)S(m.btnH));
    FillRound(g, br, (REAL)8, t.surface);
    StrokeRound(g, br, (REAL)8, t.border, 1.0f);
    TextCenter(g, br, st.T(L"检查更新", L"Check for updates"), MakeF(S(12)), t.textDim);
    y += S(m.btnH);

    st.contentHeight = (float)(y - L.contentTop + S(24));
}

// ---------------- 主绘制 ----------------
inline void DrawAll(Graphics& g, AppState& st) {
    const Metrics& m = M();
    const int dd = m.dpi;
    auto S2 = [dd](int v) { return MulDiv(v, dd, 96); };
    const Theme& t = eui::T();
    SolidBrush bg(t.bg);
    g.FillRectangle(&bg, 0, 0, m.w, m.h);

    DrawSidebar(g, st);
    DrawTopBar(g, st);

    // 内容区裁剪
    // 注意：MinGW 的 GDI+ 没有 Graphics::GetState()，用 SetClip/ResetClip 配对
    g.SetClip(RectF((REAL)S2(m.navW), (REAL)S2(m.topH),
                    (REAL)(m.w - S2(m.navW)), (REAL)(m.h - S2(m.topH))));
    switch (st.page) {
    case PAGE_OVERVIEW: DrawOverview(g, st); break;
    case PAGE_SOUND:    DrawSound(g, st);    break;
    case PAGE_MIC:      DrawMic(g, st);      break;
    case PAGE_DEVICES:  DrawDevices(g, st);  break;
    case PAGE_MIXER:    DrawMixer(g, st);    break;
    case PAGE_APPS:     DrawApps(g, st);     break;
    case PAGE_FIRMWARE: DrawFirmware(g, st); break;
    case PAGE_SETTINGS: DrawSettings(g, st); break;
    }
    g.ResetClip();

    // 浮层最后画（要盖在导航栏和内容区之上）
    DrawLangPopup(g, st);
    DrawAppPickPopup(g, st);
}

}  // namespace eui2
