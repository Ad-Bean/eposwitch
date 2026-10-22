// 新版主界面：左侧导航 + 顶栏（设备选择器）+ 内容区（可滚动）
//
// ★ 布局约定（重要）
//   整个内容区使用**同一套行栅格**，由 Layout 结构体统一描述：
//     - contentX / contentW：内容区左边界与宽度
//     - scroll：当前滚动偏移
//     - 每个区块的 y 坐标都在 ComputeLayout() 里算出来
//   绘制(DrawXxx) 与 命中测试(HitTest) 必须调用**同一个** ComputeLayout()，
//   任何页面都不得自己另算一套 y 坐标，否则绘制与点击必然错位。
#pragma once
#include "eui.h"
#include "eposfx.h"
#include "eposdev.h"

namespace eui2 {

// ---------------- 页面 ----------------
enum Page {
    PAGE_OVERVIEW = 0,
    PAGE_SOUND,
    PAGE_MIC,
    PAGE_DEVICES,
    PAGE_MIXER,
    PAGE_APPS,
    PAGE_FIRMWARE,
    PAGE_SETTINGS,
    PAGE_COUNT
};

// ---------------- 动作标识（CID_ACTION 用 action 字段区分）----------------
enum {
    ACT_NONE = 0,
    ACT_LANG,        // 语言切换
    ACT_OUTPUT,      // 输出设备卡片
    ACT_CAPTURE,     // 从官方捕获
    ACT_SAVE,        // 保存为预设
    ACT_CLEAR_EQ,    // 清除 EQ
    ACT_PAIRING,     // 重新配对
    ACT_REFRESH,     // 刷新设备
    ACT_MUTE_SPK,    // 扬声器静音
    ACT_MUTE_MIC,    // 麦克风静音
    ACT_MIC_NR,      // 麦克风降噪开关
    ACT_MIC_HPF,     // 麦克风高通
    ACT_MIC_FRC,     // 麦克风 FRC
    ACT_MIC_METERS,  // 麦克风电平表
    ACT_MIC_PRESET,  // 麦克风 EQ 预设（index = 预设序号）
    ACT_DEV_TAB,     // 设备页标签切换（index 0/1）
    ACT_DEV_MUTE,    // 设备页：单端点静音（index = 可见行序号）
    ACT_MIX_MUTE,    // 混音器：单应用静音/恢复（index = 行序号）
    ACT_APP_DEL,     // 应用规则：删除（index = 规则序号）
    ACT_APP_CAPTURE, // 应用规则：捕获前台程序
    ACT_APP_MANUAL,  // 应用规则：进入手动输入
    ACT_APP_PICKOPEN,// 应用规则：展开预设下拉
    ACT_SET_TOGGLE,  // 设置页开关（index = 第几项）
    ACT_SET_LANG,    // 设置页语言
    ACT_SET_OPENCFG, // 打开配置文件所在目录
    ACT_MIC_TEST,    // 麦克风输入测试 开/停
    ACT_HK_EDIT,     // 全局快捷键：录制新组合键（index = 第几项）
    ACT_HK_CLEAR     // 全局快捷键：清除该项（index = 第几项）
};

// 音量滑块 ID（命中测试返回，拖动由 WM_MOUSEMOVE 处理）
enum { CID_VOL_SPEAKER = 640, CID_VOL_MIC = 641 };

// ---------------- 全局状态 ----------------
struct AppState {
    int  page = PAGE_OVERVIEW;
    bool english = false;

    // 设备
    std::vector<eposdev::DeviceInfo> devices;    // 来自 requestType=1
    int  selectedDevice = -1;                    // 选中的设备索引
    std::vector<Endpoint>* endpoints = nullptr;        // 输出端点（main.cpp 填充）
    std::vector<Endpoint>* captureEndpoints = nullptr;  // 输入端点
    std::wstring currentOutputId;
    std::wstring currentInputId;
    int  devTab = 0;              // 设备页 0=输出 1=输入
    // 设备页每行音量/静音（下标 = DeviceRows 的可见行序号；main.cpp 定时器填充）
    std::vector<int>  devVol;
    std::vector<bool> devMuted;

    // 混音器页：每行一个"有会话的应用"（main.cpp 定时器填充）
    struct MixRow {
        std::wstring name;      // 进程名（xxx.exe）
        int  vol  = 100;
        int  prev = 100;        // 静音前的音量（恢复用）
        bool muted = false;
    };
    std::vector<MixRow> mix;

    // 总览页"已连接设备"面板的内部滚动（面板高度固定，设备多时内部滚）
    float ovDevScroll = 0;

    // ANC 开关状态（HID 只读监听；-1 = 未同步，等用户按一次耳机 ANC 键）
    int ancState = -1;
    int ancLevel = -1;        // 报告里与 ANC 联动的另一字段（0x99/0x8c，含义未定）

    // 应用规则页（profiles.txt 的界面化编辑）
    std::vector<std::pair<std::wstring, std::wstring>> appRulesView;  // exe, presetKey
    bool        appAddMode = false;   // 正在手动输入进程名
    std::wstring appAddText;          // 手动输入缓冲 / 已捕获待选预设的进程名
    bool        appPickOpen = false;  // 预设下拉展开
    int         appPickHover = -1;

    // 设置项（main.cpp 负责读写 settings.ini）
    bool setAutoSwitch   = true;  // 自动切换输出设备
    bool setBattAlert    = true;  // 低电量提醒
    bool setAutoRestore  = true;  // 耳机重连后切回输出
    bool setAppPreset    = true;  // 按前台应用自动切预设
    bool setAutostart    = false; // 开机自动启动
    bool setCloseToTray  = true;  // 关闭按钮收进托盘（否则直接退出）
    bool setHotkeys      = true;  // 启用全局快捷键

    // 全局快捷键的显示副本（main.cpp 每次定时器刷新）
    struct HkView { std::wstring text; bool ok = true; };
    HkView hk[3];
    int  hkEditing = -1;          // 正在录制第几项（-1 = 没在录制）
    std::wstring hkMsg;           // 录制失败/被占用之类的短暂提示

    // 音效
    eposfx::SoundState sound;
    std::vector<eposfx::Preset> presets;
    int  currentPreset = -1;

    // 音量（来自 Windows 音频端点，-1 = 读不到）
    int  speakerVol = -1;
    int  micVol = -1;
    bool speakerMuted = false;
    bool micMuted = false;

    // 麦克风实时电平（由 Windows 采集端点提供，dBFS；-60 = 静音）
    float micPeakDb = -60.0f;
    float micPeakHoldDb = -60.0f;
    bool  micActive = false;          // 最近 2 秒内是否有有效输入
    bool  micSilentLong = false;
    // ★ 输入测试开关：默认关闭。开一条共享采集流是有代价的（驱动侧持续
    //   跑实时线程、占 CPU），不该"进了麦克风页就一直在录"，必须用户主动开。
    bool  micTestOn = false;
    std::wstring micDevName;          // 正在测试的采集设备名      // 连续 3 秒完全没有信号（可能是麦克风杆抬起/已拆下）
    int   micPresetIdx = 0;           // 当前麦克风 EQ 预设（0=默认）

    // 麦克风处理开关的"本地记录值"
    // H3PRO 不回读这几项（rt=1 恒为 0），所以用户点过之后以本地记录为准显示，
    // 否则界面会"点了开又跳回关"，看起来像没生效。
    // 顺序：0=降噪 1=高通 2=FRC 3=电平表；-1 表示用户还没点过
    int  micOverride[4] = { -1, -1, -1, -1 };

    // 交互
    eui::ScrollArea scroll;
    float contentOffset = 0;      // 内容区滚动偏移
    float contentHeight = 0;      // 内容区总高
    int  hoverId = -1;            // 悬停控件 ID
    int  hoverIndex = -1;         // 悬停控件的序号（预设/端点等）
    int  actionHint = ACT_NONE;   // 悬停的按钮动作（用于区分同类按钮）
    bool langOpen = false;        // 侧栏语言下拉是否展开
    bool pipeReady = false;
    bool serviceOk = false;

    const wchar_t* T(const wchar_t* zh, const wchar_t* en) const { return english ? en : zh; }
};

// ---------------- 布局常量 ----------------
struct Metrics {
    // ★ 窗口尺寸必须与 main.cpp 的 BASE_W / BASE_H 完全一致，
    //   否则按 m.w/m.h 计算的坐标与实际窗口不符，表现为"很多不对齐"。
    int w = 880, h = 760;
    int dpi = 96;
    int S(int v) const { return MulDiv(v, dpi, 96); }
    int navW = 176;
    int topH = 60;
    int pad = 24;
    int gutter = 16;          // 卡片间距
    int blockGap = 18;        // 区块之间的间距
    int titleH = 26;          // 区块标题占位高
    int rowH = 40;            // 一行控件（分段控件）高
    int sliderH = 34;         // 一行滑块高
    // ★ 声音页每行都要带一行 hint，行高必须比"纯控件行"高，否则 hint 会压到下一行
    int segRowH = 54;         // 效果行：分段控件 26 + hint 18 + 间距
    int sldRowH = 50;         // 数值行：滑块 22 + hint 18 + 间距
    int presetCardH = 66;     // 预设卡片高
    int eqH = 160;            // 扬声器 EQ 区高
    int micEqH = 150;         // 麦克风 EQ 区高
    int segW = 210;           // 分段控件宽
    int labelW = 100;         // 左侧标签列宽
    int btnH = 34;            // 按钮高
};

inline Metrics M() { static Metrics m; return m; }

// ---------------- 控件 ID ----------------
enum {
    CID_NONE = 0,
    CID_PAGE_BASE = 100,      // 100..104 导航项
    CID_DEV_SELECT = 200,     // 顶栏设备选择器
    CID_DEV_PICK = 210,       // 设备下拉项
    CID_PRESET_BASE = 300,    // 300..331 预设卡片
    CID_EQ_BASE = 400,        // 400..408 扬声器 EQ 频段
    CID_EQ_MIC_BASE = 420,    // 420..428 麦克风 EQ 频段
    CID_SEG_BASE = 500,       // 500+ 分段控件
    CID_SLIDER_BASE = 600,    // 600+ 滑块
    CID_ACTION = 700,         // 按钮
    CID_CLOSE = 720,          // 右上角关闭按钮
    CID_MIC_PRESET_BASE = 740, // 740..747 麦克风 EQ 预设 chips
    CID_DEV_TAB = 760,         // 设备页 输出/输入 标签
    CID_SET_ROW = 780,         // 780.. 设置页开关行
    CID_LANG_POP = 800,        // 800..801 侧栏语言下拉项
    CID_SET_LANG_ROW = 810,    // 810..811 设置页语言列表项
    CID_HK_EDIT = 820,         // 820..822 快捷键录制按钮
    CID_HK_CLEAR = 830,        // 830..832 快捷键清除按钮
    CID_MIC_TEST = 840,        // 麦克风输入测试开关按钮
    CID_DEVVOL = 850,          // 850.. 设备页单端点音量条（index = 可见行序号）
    CID_MIXVOL = 860,          // 860.. 混音器单应用音量条（index = 行序号）
    CID_APP_PICK = 870         // 870.. 应用规则预设下拉项
};

// 动作标识（CID_ACTION 用 action 字段区分）

// 输出设备页的端点序号（不含排除项），与 DrawDevices 顺序一致
inline int VisibleEndpointIndex(const AppState& st, int visibleIdx) {
    if (!st.endpoints) return -1;
    int n = 0;
    for (const auto& ep : *st.endpoints) {
        if (ep.kind == KIND_EXCLUDED) continue;
        if (n == visibleIdx) return n;
        n++;
    }
    return -1;
}

enum SegKind { SEG_SURROUND = 0, SEG_VOICE, SEG_DIRECT, SEG_NR, SEG_NOISEGATE, SEG_COUNT };

// ---------------- 导航定义 ----------------
struct NavDef { const wchar_t* zh; const wchar_t* en; int page; };
inline const NavDef* Navs() {
    static const NavDef n[] = {
        { L"总览",     L"Overview",  PAGE_OVERVIEW },
        { L"声音",     L"Sound",     PAGE_SOUND },
        { L"麦克风",   L"Microphone",PAGE_MIC },
        { L"设备",     L"Devices",   PAGE_DEVICES },
        { L"混音器",   L"Mixer",     PAGE_MIXER },
        { L"应用",     L"App rules", PAGE_APPS },
        { L"固件",     L"Firmware",  PAGE_FIRMWARE },
        { L"设置",     L"Settings",  PAGE_SETTINGS },
    };
    return n;
}
inline int NavCount() { return 8; }

// =====================================================================
// ★ 统一布局计算 —— 绘制与命中测试的唯一真相来源
// =====================================================================
struct Layout {
    // 内容区
    int contentX = 0, contentW = 0;
    int viewY = 0, viewH = 0;      // 内容视口（顶栏以下）
    int scroll = 0;                // 当前滚动偏移
    // ★ 内容坐标原点（= 未滚动时第一个元素的 y 再减去内边距）。
    //   各页设置 contentHeight 时必须用 (y - contentTop)，这样得到的高度
    //   与滚动位置无关；否则滚到底时 contentHeight 会跟着变小 → 回弹。
    int contentTop = 0;
    int padPx = 0;                 // 内边距（像素）

    // 通用栅格
    int rowH = 40;                 // 一行分段控件高
    int sliderH = 34;              // 一行滑块高
    int segRowH = 54;              // 声音页效果行（含 hint）
    int sldRowH = 50;              // 声音页数值行（含 hint）
    int titleH = 26;               // 区块标题占位
    int labelW = 100;              // 左侧标签列宽
    int controlX = 0;              // 控件列左边界
    int controlW = 0;              // 控件列宽
    int segW = 210;                // 分段控件宽
    int trackX = 0, trackW = 0;    // 滑块轨道
    int valueW = 44;               // 滑块右侧数值列

    // 顶栏
    int devSelX = 0, devSelY = 0, devSelW = 0, devSelH = 0;   // 设备选择器
    int navTop = 0;                // 侧栏第一项的 y
    int navItemH = 42, navItemBoxH = 38;                      // 导航项间距 / 有效高
    // 侧栏底部设置卡片（语言）
    int footX = 0, footY = 0, footW = 0, footH = 0;
    int langX = 0, langY = 0, langW = 0, langH = 0;           // 语言下拉（收起态）
    int langPopY = 0, langPopH = 0, langItemH = 0;            // 语言下拉（展开态）

    // 概览页：音量卡片
    int volY = 0, volH = 0;
    int volTrackX = 0, volTrackW = 0;
    int volBtnX = 0, volBtnW = 0;   // 右侧静音图标按钮
    int volRowH = 0;

    // 声音页
    int presetCols = 3;
    int presetCardW = 0, presetCardH = 66;
    int presetGridY = 0;           // 预设网格顶部
    int presetGridRows = 0;
    int eqY = 0, eqH = 160;        // 扬声器 EQ
    int segY = 0;                  // 效果分段控件第一行
    int sldY = 0;                  // 数值滑块第一行
    int btnY = 0;                  // 底部按钮

    // 麦克风页
    int micPresetY = 0, micPresetH = 26;   // EQ 预设 chips 行
    int micEqY = 0, micEqH = 150;
    int micTestY = 0, micTestH = 92;       // 输入测试（开关 + 实时电平）
    int micTestBtnX = 0, micTestBtnY = 0, micTestBtnW = 0, micTestBtnH = 0;
    int micCtlY = 0;               // 麦克风控制项起始 y（第一行）
    int micRowH = 48;              // 每行高（标签 + 一行 hint）
    int micVolY = 0;               // 麦克风音量行

    // 设备页
    int devTabY = 0, devTabH = 28;
    int devListY = 0, devRowH = 64;

    // 混音器页
    int mixY = 0, mixRowH = 48;

    // 应用规则页
    int appToggleY = 0, appListY = 0, appRowH = 46;
    int appAddY = 0, appBtnY = 0;
    int appPickY = 0, appPickH = 0, appPickItemH = 0;

    // 设置页
    int setY = 0, setRowH = 54, setN = 7;
    int setLangTitleY = 0, setLangY = 0, setLangRowH = 38;
    int setHkTitleY = 0, setHkY = 0, setHkRowH = 38;
    int setInfoTitleY = 0, setInfoY = 0;
    int hkComboW = 0, hkClearW = 0, hkClearX = 0, hkComboX = 0;

    // 固件页
    int fwY = 0;

    // 概览页
    int ovTileY = 0, ovTileH = 72, ovTileW = 0;
    int ovPanelY = 0, ovPanelH = 210;
};

// ---------------- 麦克风 EQ 预设 ----------------
// 9 段：32 / 64 / 125 / 250 / 500 / 1K / 2K / 4K / 8K（dB，范围 -6..+6）
struct MicPreset { const wchar_t* zh; const wchar_t* en; double eq[9]; };
inline const MicPreset* MicPresets() {
    static const MicPreset p[] = {
        { L"默认",   L"Flat",
          { 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { L"人声清晰", L"Vocal",
          { -3.5, -2.5, -1.0, 1.0, 2.0, 2.5, 2.5, 1.5, 0.0 } },
        { L"播客",   L"Podcast",
          { -5.0, -3.0, -0.5, 1.5, 2.5, 2.0, 1.0, 0.0, -1.0 } },
        { L"切低频", L"Low cut",
          { -6.0, -5.0, -3.0, -1.0, 0.0, 0.0, 0.0, 0.0, 0.0 } },
        { L"通讯",   L"Comms",
          { -6.0, -3.0, 0.0, 2.0, 3.0, 2.0, 0.0, -2.0, -3.0 } },
    };
    return p;
}
inline int MicPresetCount() { return 5; }

// 预设卡片网格列数
inline int PresetCols() { return 3; }

// 计算声音页需要多少行预设（上限 12）
inline int PresetCount(const AppState& st) {
    int n = (int)st.presets.size();
    if (n > 12) n = 12;
    return n;
}

inline Layout ComputeLayout(const AppState& st) {
    const Metrics& m = M();
    const int dd = m.dpi;
    auto SC = [dd](int v) { return MulDiv(v, dd, 96); };

    Layout L;
    int navW = SC(m.navW), pad = SC(m.pad);
    L.contentX = navW + pad;
    L.contentW = m.w - navW - pad * 2;
    L.viewY = SC(m.topH);
    L.viewH = m.h - L.viewY;
    L.scroll = (int)st.contentOffset;
    L.padPx = pad;
    L.contentTop = L.viewY + pad - L.scroll;

    // ---- 通用栅格 ----
    L.rowH = SC(m.rowH);
    L.sliderH = SC(m.sliderH);
    L.segRowH = SC(m.segRowH);
    L.sldRowH = SC(m.sldRowH);
    L.titleH = SC(m.titleH);
    L.labelW = SC(m.labelW);
    L.segW = SC(m.segW);
    L.valueW = SC(48);
    // 控件列：从 contentX + labelW 开始，宽度到内容右边界为止
    L.controlX = L.contentX + L.labelW;
    L.controlW = L.contentW - L.labelW;
    // 滑块轨道 = 控件列减去右侧数值列
    L.trackX = L.controlX;
    L.trackW = L.controlW - L.valueW;

    // ---- 顶栏设备选择器 ----
    L.devSelX = L.contentX;
    L.devSelY = SC(13);
    L.devSelW = SC(210);
    L.devSelH = SC(34);

    // ---- 侧栏 ----
    L.navTop = SC(m.topH) + SC(14);
    L.navItemH = SC(42);
    L.navItemBoxH = SC(38);

    // 侧栏底部设置卡片：贴底，留 12 边距；高度按内容算
    L.footH = SC(82);
    L.footX = SC(10);
    L.footW = SC(m.navW) - SC(20);
    L.footY = m.h - L.footH - SC(12);
    L.langX = L.footX + SC(12);
    L.langY = L.footY + SC(30);
    L.langW = L.footW - SC(24);
    L.langH = SC(28);
    // 语言下拉列表：向上展开（卡片本身就贴底，向下会超出窗口）
    L.langItemH = SC(30);
    L.langPopH = L.langItemH * 2 + SC(8);
    L.langPopY = L.langY - L.langPopH - SC(6);

    // ---- 声音页 ----
    int nP = PresetCount(st);
    L.presetCols = PresetCols();
    L.presetCardH = SC(m.presetCardH);
    L.presetCardW = (L.contentW - SC(m.gutter) * (L.presetCols - 1)) / L.presetCols;
    L.presetGridRows = (nP + L.presetCols - 1) / L.presetCols;
    if (L.presetGridRows < 1) L.presetGridRows = 1;

    // y 从内容区顶部开始
    int y = L.viewY + pad - L.scroll;

    L.presetGridY = y + L.titleH;                       // 标题之后
    y = L.presetGridY + L.presetGridRows * (L.presetCardH + SC(m.gutter)) + SC(m.blockGap);

    L.eqY = y + L.titleH;                               // EQ 标题之后
    y = L.eqY + SC(m.eqH) + SC(m.blockGap);

    L.segY = y + L.titleH;                               // 效果标题之后
    y = L.segY + 4 * L.segRowH + SC(m.blockGap);         // 4 个分段控件（每个带 hint）

    L.sldY = y + L.titleH;                               // 数值标题之后
    y = L.sldY + 3 * L.sldRowH + SC(m.blockGap);

    L.btnY = y;

    // ---- 麦克风页 ----
    int my = L.viewY + pad - L.scroll;
    L.micPresetY = my + L.titleH;                  // 预设 chips（在 EQ 卡片上方）
    L.micEqY = L.micPresetY + L.micPresetH + SC(10);
    L.micEqH = SC(m.micEqH);
    L.micTestY = L.micEqY + L.micEqH + SC(m.blockGap) + L.titleH;
    L.micTestH = SC(92);
    // 测试开关按钮：卡片右上角
    L.micTestBtnW = SC(104);
    L.micTestBtnH = SC(28);
    L.micTestBtnX = L.contentX + L.contentW - SC(12) - L.micTestBtnW;
    L.micTestBtnY = L.micTestY + SC(10);
    L.micCtlY = L.micTestY + L.micTestH + SC(m.blockGap) + L.titleH;
    L.micVolY = L.micCtlY + 4 * L.micRowH + SC(m.blockGap) + L.titleH;
    // ★ 注意：不要再在这里覆盖 micCtlY / micRowH（旧代码残留会导致
    //   "输入测试"卡片被后面的控制行盖住）

    // ---- 设备页 ----
    L.devTabY = L.viewY + pad - L.scroll;
    L.devListY = L.devTabY + L.devTabH + SC(m.blockGap) + SC(12);
    L.devRowH = SC(64);

    // ---- 混音器页 ----
    L.mixY = L.viewY + pad - L.scroll + L.titleH + SC(4);
    L.mixRowH = SC(48);

    // ---- 应用规则页 ----
    // 布局：开关行 → 规则列表（每行 46）→ 添加控件 → 提示
    L.appToggleY = L.viewY + pad - L.scroll + L.titleH;
    L.appRowH = SC(46);
    {
        int n = (int)st.appRulesView.size();
        L.appListY = L.appToggleY + SC(54) + SC(m.blockGap) + L.titleH;
        L.appAddY  = L.appListY + (n > 0 ? n * L.appRowH + SC(6) : SC(4));
        L.appBtnY  = L.appAddY + SC(40);
    }

    // ---- 设置页 ----
    // 顺序：常规开关(7) → 界面语言(列表 2 项) → 全局快捷键(3 行) → 状态
    // 绘制与命中都必须走这几个 y，别在页面里自己加偏移。
    L.setY = L.viewY + pad - L.scroll + L.titleH;
    L.setRowH = SC(54);
    L.setN = 7;
    L.setLangTitleY = L.setY + L.setN * L.setRowH + SC(m.blockGap);
    L.setLangRowH = SC(38);
    L.setLangY = L.setLangTitleY + L.titleH;
    L.setHkTitleY = L.setLangY + 2 * L.setLangRowH + SC(m.blockGap);
    L.setHkRowH = SC(38);
    L.setHkY = L.setHkTitleY + L.titleH;
    // 3 行快捷键 + 一行提示（录制中/错误/帮助），留够空间再进"状态"
    L.setInfoTitleY = L.setHkY + 3 * L.setHkRowH + SC(m.blockGap) + SC(16);
    L.setInfoY = L.setInfoTitleY + L.titleH;
    L.hkComboW = SC(132);
    L.hkClearW = SC(26);
    L.hkClearX = L.contentX + L.contentW - L.hkClearW;
    L.hkComboX = L.hkClearX - SC(8) - L.hkComboW;

    // ---- 固件页 ----
    L.fwY = L.viewY + pad - L.scroll;

    // ---- 概览页 ----
    L.ovTileY = L.viewY + pad - L.scroll;
    L.ovTileH = SC(76);
    L.ovTileW = (L.contentW - SC(m.gutter) * 3) / 4;

    // 音量卡片：4 个指标卡下方
    L.volY = L.ovTileY + L.ovTileH + SC(m.blockGap);
    L.volH = SC(112);
    L.volRowH = SC(40);
    L.volTrackX = L.contentX + SC(96);
    // 右侧预留：数值列(50) + 间距(6) + 静音按钮(30) + 右边距(2)
    L.volBtnW = SC(30);
    L.volBtnX = L.contentX + L.contentW - L.volBtnW - SC(12);
    L.volTrackW = (L.volBtnX - SC(56)) - L.volTrackX;

    L.ovPanelY = L.volY + L.volH + SC(m.blockGap);
    L.ovPanelH = SC(216);

    return L;
}

// ---------------- 设备页行布局（绘制与命中共用的唯一来源）----------------
// 行序：先物理设备，后虚拟设备；虚拟分组前会留出一个分组标题行。
struct DevRows {
    std::vector<int>   idx;      // 端点在原列表中的下标
    std::vector<RectF> row;      // 每行卡片
    std::vector<RectF> track;    // 音量轨道（空 = 没有音量数据）
    std::vector<RectF> pct;      // 百分比文字带
    std::vector<RectF> mute;     // 静音按钮
    int  groupTitleY = -1;       // 「虚拟设备」分组标题的 y（-1 = 没有虚拟行）
    int  groupTitleH = 0;
};

inline DevRows DevLayout(const AppState& st, const Layout& L, bool capture) {
    DevRows out;
    std::vector<Endpoint>* list = capture ? st.captureEndpoints : st.endpoints;
    if (!list) return out;
    const Metrics& m = M();
    const int dd = m.dpi;
    auto SC = [dd](int v) { return MulDiv(v, dd, 96); };

    // 先物理后虚拟
    for (int i = 0; i < (int)list->size(); i++)
        if ((*list)[i].kind != KIND_VIRTUAL && (*list)[i].kind != KIND_EXCLUDED)
            out.idx.push_back(i);
    int firstVirtual = (int)out.idx.size();
    for (int i = 0; i < (int)list->size(); i++)
        if ((*list)[i].kind == KIND_VIRTUAL)
            out.idx.push_back(i);
    bool hasVirtual = firstVirtual < (int)out.idx.size();

    const int rowStep = L.devRowH;               // 64
    const int rowH   = rowStep - SC(6);          // 卡片实际高 58
    int y = L.devListY;
    for (int r = 0; r < (int)out.idx.size(); r++) {
        if (r == firstVirtual && hasVirtual) {
            out.groupTitleY = y;
            out.groupTitleH = SC(24);
            y += out.groupTitleH + SC(4);
        }
        RectF card((REAL)L.contentX, (REAL)y, (REAL)L.contentW, (REAL)rowH);
        out.row.push_back(card);
        // 右侧：[音量轨道 110][百分比 40][静音 26]，与右缘留 12
        int mx = L.contentX + L.contentW - SC(12) - SC(26);
        int px = mx - SC(8) - SC(40);
        int tx = px - SC(8) - SC(110);
        int cy = y + rowH / 2;
        out.mute.push_back(RectF((REAL)mx, (REAL)(cy - SC(13)), (REAL)SC(26), (REAL)SC(26)));
        out.pct.push_back(RectF((REAL)px, (REAL)(cy - SC(10)), (REAL)SC(40), (REAL)SC(20)));
        out.track.push_back(RectF((REAL)tx, (REAL)(cy - SC(4)), (REAL)SC(110), (REAL)SC(8)));
        y += rowStep;
    }
    return out;
}

// ---------------- 总览页"已连接设备"面板（内部可滚动）----------------
struct OvDevList {
    RectF panel;        // 整个面板
    RectF view;         // 内部可视区（标题以下）
    float content = 0;  // 内容总高（未滚动）
    bool  hasHeadset = false;
    int   rows = 0;     // 端点行数
    float maxScroll = 0;
};

inline OvDevList OvDevLayout(const AppState& st, const Layout& L) {
    const Metrics& m = M();
    const int dd = m.dpi;
    auto SC = [dd](int v) { return MulDiv(v, dd, 96); };
    OvDevList o;
    int colW = (L.contentW - SC(m.gutter)) / 2;
    o.panel = RectF((REAL)L.contentX, (REAL)L.ovPanelY, (REAL)colW, (REAL)L.ovPanelH);
    o.view  = RectF(o.panel.X, o.panel.Y + (REAL)SC(44),
                    o.panel.Width, (REAL)(L.ovPanelH - SC(52)));
    bool dev = (st.selectedDevice >= 0 && st.selectedDevice < (int)st.devices.size());
    o.hasHeadset = dev;
    o.rows = 0;
    if (st.endpoints)
        for (const auto& ep : *st.endpoints)
            if (ep.kind != KIND_EPOS && ep.kind != KIND_EXCLUDED) o.rows++;
    o.content = (float)(SC(o.hasHeadset ? 52 : 0) + o.rows * SC(46));
    o.maxScroll = o.content - o.view.Height;
    if (o.maxScroll < 0) o.maxScroll = 0;
    return o;
}

// 内容区最大滚动量（像素）。contentHeight 以"内容坐标"计（与滚动无关），
// 所以这里的结果是稳定的 —— 不会出现滚到底又弹回去。
inline float MaxScroll(const AppState& st) {
    const Metrics& m = M();
    const int dd = m.dpi;
    int topH = MulDiv(m.topH, dd, 96);
    int pad  = MulDiv(m.pad, dd, 96);
    int winH = MulDiv(m.h, dd, 96);
    // 内容从 topH+pad 开始，底部也要留 pad
    float viewH = (float)(winH - topH - pad);
    float mx = (float)st.contentHeight - viewH;
    return mx > 0.0f ? mx : 0.0f;
}

// 顶栏右侧关闭按钮的矩形（绘制与命中共用）
inline RectF CloseButtonRect() {
    const Metrics& m = M();
    const int dd = m.dpi;
    auto SC = [dd](int v) { return MulDiv(v, dd, 96); };
    int size = SC(24);
    return RectF((REAL)(m.w - SC(16) - size), (REAL)(SC(m.topH) / 2 - size / 2),
                 (REAL)size, (REAL)size);
}

// ---------------- 命中测试 ----------------
struct HitInfo {
    int id = CID_NONE;
    int index = -1;       // 预设索引 / 频段索引 / 分段项索引 / 端点序号
    int segKind = -1;
    int action = ACT_NONE;
};

inline bool InRect(const RectF& r, POINT p) {
    return p.x >= r.X && p.x <= r.X + r.Width && p.y >= r.Y && p.y <= r.Y + r.Height;
}

inline HitInfo HitTest(const AppState& st, POINT p) {
    HitInfo h;
    const Metrics& m = M();
    const int dd = m.dpi;
    auto SC = [dd](int v) { return MulDiv(v, dd, 96); };
    int navW = SC(m.navW);

    Layout L = ComputeLayout(st);

    // 左侧导航
    if (p.x < navW) {
        // 语言下拉展开时优先级最高（它盖在导航项上面）
        if (st.langOpen) {
            RectF pr((REAL)L.langX, (REAL)L.langPopY, (REAL)L.langW, (REAL)L.langPopH);
            if (InRect(pr, p)) {
                int idx = (int)((p.y - pr.Y - SC(4)) / (REAL)L.langItemH);
                if (idx < 0) idx = 0;
                if (idx > 1) idx = 1;
                h.id = CID_LANG_POP; h.index = idx;
                return h;
            }
        }
        // ★ 必须与 DrawSidebar 的导航项起始位置一致
        // ★ id 必须带序号：DrawSidebar 用 hoverId == CID_PAGE_BASE + i 判断悬停，
        //   之前只返回 CID_PAGE_BASE，导致只有第 0 项（总览）有 hover 效果。
        int y = L.navTop;
        for (int i = 0; i < NavCount(); i++) {
            RectF r((REAL)SC(10), (REAL)y, (REAL)(navW - SC(20)), (REAL)L.navItemBoxH);
            if (InRect(r, p)) { h.id = CID_PAGE_BASE + i; h.index = i; return h; }
            y += L.navItemH;
        }
        // 语言下拉按钮（左下角）
        {
            RectF lr((REAL)L.langX, (REAL)L.langY, (REAL)L.langW, (REAL)L.langH);
            if (InRect(lr, p)) {
                h.id = CID_ACTION; h.action = ACT_LANG; h.index = 0;
                return h;
            }
        }
        return h;
    }

    // 顶栏：关闭按钮（必须在设备选择器之前判断）
    if (p.y < L.viewY) {
        RectF cb = CloseButtonRect();
        // 命中区比视觉稍大，避免"点不中"
        RectF hitCb(cb.X - (REAL)4, cb.Y - (REAL)4, cb.Width + (REAL)8, cb.Height + (REAL)8);
        if (InRect(hitCb, p)) { h.id = CID_CLOSE; return h; }
    }

    // 顶栏：设备选择器
    RectF devSel((REAL)L.devSelX, (REAL)L.devSelY, (REAL)L.devSelW, (REAL)L.devSelH);
    if (InRect(devSel, p)) { h.id = CID_DEV_SELECT; return h; }

    const int dd2 = m.dpi;
    auto SC2 = [dd2](int v) { return MulDiv(v, dd2, 96); };

    switch (st.page) {
    case PAGE_OVERVIEW: {
        // 音量卡片：两行滑块（扬声器 / 麦克风）+ 静音按钮
        for (int k = 0; k < 2; k++) {
            int ry = L.volY + SC2(40) + k * L.volRowH;
            // 滑块轨道
            RectF tr((REAL)L.volTrackX, (REAL)(ry + SC2(6)), (REAL)L.volTrackW, (REAL)SC2(22));
            if (InRect(tr, p)) {
                h.id = (k == 0) ? CID_VOL_SPEAKER : CID_VOL_MIC;
                h.index = k;
                return h;
            }
            // 静音按钮（在轨道右侧，30px 见方的图标按钮，命中区略放大）
            RectF mb((REAL)(L.volBtnX - SC2(3)), (REAL)(ry - SC2(2)),
                     (REAL)(L.volBtnW + SC2(6)), (REAL)SC2(34));
            if (InRect(mb, p)) {
                h.id = CID_ACTION;
                h.action = (k == 0) ? ACT_MUTE_SPK : ACT_MUTE_MIC;
                return h;
            }
        }
        return h;
    }
    case PAGE_SOUND: {
        // ---- 分段控件 ----
        {
            int kinds[4] = { SEG_SURROUND, SEG_VOICE, SEG_DIRECT, SEG_NR };
            for (int k = 0; k < 4; k++) {
                int ry = L.segY + k * L.segRowH;
                RectF sr((REAL)(L.contentX + L.contentW - L.segW), (REAL)ry,
                         (REAL)L.segW, (REAL)SC2(26));
                if (InRect(sr, p)) {
                    int nOpt = (kinds[k] == SEG_VOICE) ? 3 : (kinds[k] == SEG_NR ? 4 : 2);
                    REAL gap = 2;
                    REAL optW = (sr.Width - gap * (nOpt - 1)) / nOpt;
                    int idx = (int)((p.x - sr.X) / (optW + gap));
                    if (idx < 0) idx = 0;
                    if (idx >= nOpt) idx = nOpt - 1;
                    h.id = CID_SEG_BASE + kinds[k];
                    h.index = idx;
                    h.segKind = kinds[k];
                    return h;
                }
            }
        }
        // ---- 滑块 ----
        for (int k = 0; k < 3; k++) {
            int ry = L.sldY + k * L.sldRowH;
            RectF tr((REAL)L.trackX, (REAL)(ry + SC2(3)), (REAL)L.trackW, (REAL)SC2(22));
            if (InRect(tr, p)) { h.id = CID_SLIDER_BASE + k; h.index = k; return h; }
        }
        // ---- 底部按钮 ----
        {
            int halfW = (L.contentW - SC2(10)) / 2;
            RectF b1((REAL)L.contentX, (REAL)L.btnY, (REAL)halfW, (REAL)SC2(m.btnH));
            RectF b2((REAL)(L.contentX + halfW + SC2(10)), (REAL)L.btnY, (REAL)halfW, (REAL)SC2(m.btnH));
            if (InRect(b1, p)) { h.id = CID_ACTION; h.action = ACT_CAPTURE; return h; }
            if (InRect(b2, p)) { h.id = CID_ACTION; h.action = ACT_SAVE; return h; }
        }
        // ---- 预设卡片 ----
        for (int i = 0; i < PresetCount(st); i++) {
            int r = i / L.presetCols, c = i % L.presetCols;
            RectF cr((REAL)(L.contentX + c * (L.presetCardW + SC2(m.gutter))),
                     (REAL)(L.presetGridY + r * (L.presetCardH + SC2(m.gutter))),
                     (REAL)L.presetCardW, (REAL)L.presetCardH);
            if (InRect(cr, p)) { h.id = CID_PRESET_BASE; h.index = i; return h; }
        }
        // ---- 扬声器 EQ 曲线 ----
        {
            RectF eq((REAL)L.contentX, (REAL)L.eqY, (REAL)L.contentW, (REAL)L.eqH);
            if (InRect(eq, p)) {
                REAL plotX = eq.X + (REAL)SC2(40);
                REAL plotW = eq.Width - (REAL)SC2(56);
                REAL bandW = plotW / 9.0f;
                int idx = (int)((p.x - plotX) / bandW);
                if (idx < 0) idx = 0;
                if (idx > 8) idx = 8;
                h.id = CID_EQ_BASE; h.index = idx; return h;
            }
        }
        return h;
    }
    case PAGE_MIC: {
        // 输入测试开关按钮（卡片右上角）
        {
            RectF br((REAL)L.micTestBtnX, (REAL)L.micTestBtnY,
                     (REAL)L.micTestBtnW, (REAL)L.micTestBtnH);
            if (InRect(br, p)) { h.id = CID_MIC_TEST; return h; }
        }
        // 麦克风 EQ 预设 chips
        {
            const int n = MicPresetCount();
            int cw = (L.contentW - SC2(8) * (n - 1)) / n;
            for (int i = 0; i < n; i++) {
                RectF cr((REAL)(L.contentX + i * (cw + SC2(8))), (REAL)L.micPresetY,
                         (REAL)cw, (REAL)L.micPresetH);
                if (InRect(cr, p)) {
                    h.id = CID_ACTION; h.action = ACT_MIC_PRESET; h.index = i;
                    return h;
                }
            }
        }
        // 麦克风控制项：4 个开关（降噪/高通/FRC/电平表）+ 1 个音量滑块
        for (int k = 0; k < 4; k++) {
            int ry = L.micCtlY + k * L.micRowH;
            RectF sr((REAL)(L.contentX + L.contentW - L.segW), (REAL)ry,
                     (REAL)L.segW, (REAL)SC2(26));
            if (InRect(sr, p)) {
                int acts[4] = { ACT_MIC_NR, ACT_MIC_HPF, ACT_MIC_FRC, ACT_MIC_METERS };
                h.id = CID_ACTION; h.action = acts[k];
                h.index = (p.x < sr.X + sr.Width / 2) ? 0 : 1;   // 0=关 1=开
                return h;
            }
        }
        // 麦克风音量
        {
            int ry = L.micVolY;
            RectF tr((REAL)L.trackX, (REAL)(ry + SC2(6)), (REAL)L.trackW, (REAL)SC2(22));
            if (InRect(tr, p)) { h.id = CID_VOL_MIC; h.index = 1; return h; }
        }
        RectF eq((REAL)L.contentX, (REAL)L.micEqY, (REAL)L.contentW, (REAL)L.micEqH);
        if (InRect(eq, p)) {
            REAL plotX = eq.X + (REAL)SC2(40);
            REAL plotW = eq.Width - (REAL)SC2(56);
            REAL bandW = plotW / 9.0f;
            int idx = (int)((p.x - plotX) / bandW);
            if (idx < 0) idx = 0;
            if (idx > 8) idx = 8;
            h.id = CID_EQ_MIC_BASE; h.index = idx; return h;
        }
        return h;
    }
    case PAGE_DEVICES: {
        // 播放/录音 标签
        RectF tabR((REAL)L.contentX, (REAL)L.devTabY, (REAL)SC2(220), (REAL)L.devTabH);
        if (InRect(tabR, p)) {
            // ★ 必须返回 CID_ACTION：点击分发只在 CID_ACTION 分支里看 action
            h.id = CID_ACTION;
            h.action = ACT_DEV_TAB;
            h.index = (p.x < tabR.X + tabR.Width / 2) ? 0 : 1;
            return h;
        }
        // ★ 行内命中顺序：音量条 > 静音 > 整行（切换默认）。布局必须和 DrawDevices 一致
        bool capture = (st.devTab == 1);
        DevRows dr = DevLayout(st, L, capture);
        for (int r = 0; r < (int)dr.idx.size(); r++) {
            if (r < (int)dr.track.size() && InRect(dr.track[r], p)) {
                h.id = CID_DEVVOL; h.index = r; return h;
            }
            if (r < (int)dr.mute.size() && InRect(dr.mute[r], p)) {
                h.id = CID_ACTION; h.action = ACT_DEV_MUTE; h.index = r; return h;
            }
            if (r < (int)dr.row.size() && InRect(dr.row[r], p)) {
                h.id = CID_ACTION; h.action = ACT_OUTPUT; h.index = r;
                return h;
            }
        }
        return h;
    }
    case PAGE_MIXER: {
        // 每应用音量：行内命中 = 音量条 > 静音图标（行本身不可点）
        int mx = L.contentX + L.contentW - SC2(12) - SC2(26);
        int px = mx - SC2(8) - SC2(40);
        int tx = px - SC2(8) - SC2(110);
        for (int i = 0; i < (int)st.mix.size(); i++) {
            int ry = L.mixY + i * L.mixRowH;
            int cy = ry + SC2(21);
            RectF tr((REAL)tx, (REAL)(cy - SC2(4)), (REAL)SC2(110), (REAL)SC2(8));
            if (InRect(tr, p)) { h.id = CID_MIXVOL; h.index = i; return h; }
            RectF mr((REAL)mx, (REAL)(cy - SC2(13)), (REAL)SC2(26), (REAL)SC2(26));
            if (InRect(mr, p)) { h.id = CID_ACTION; h.action = ACT_MIX_MUTE; h.index = i; return h; }
        }
        return h;
    }
    case PAGE_APPS: {
        // 预设下拉展开时优先
        if (st.appPickOpen) {
            int n = (int)st.presets.size(); if (n > 12) n = 12;
            RectF pr((REAL)(L.contentX + L.contentW - SC2(12) - SC2(200)),
                     (REAL)(L.appAddY + SC2(34)), (REAL)SC2(200), (REAL)(n * SC2(30) + SC2(8)));
            if (InRect(pr, p)) {
                int k = (int)((p.y - pr.Y - SC2(4)) / (REAL)SC2(30));
                if (k < 0) k = 0;
                if (k >= n) k = n - 1;
                h.id = CID_APP_PICK; h.index = k; return h;
            }
        }
        // 总开关（与设置页"按应用自动切预设"同一项）
        {
            RectF tr((REAL)L.contentX, (REAL)(L.appToggleY - SC2(4)),
                     (REAL)L.contentW, (REAL)SC2(50));
            if (InRect(tr, p)) { h.id = CID_ACTION; h.action = ACT_SET_TOGGLE; h.index = 5; return h; }
        }
        // 规则行：右侧 × 删除
        for (int i = 0; i < (int)st.appRulesView.size(); i++) {
            int ry = L.appListY + i * L.appRowH;
            int cx = L.contentX + L.contentW - SC2(12) - SC2(26);
            int cy = ry + SC2(10);
            RectF xr((REAL)cx, (REAL)cy, (REAL)SC2(26), (REAL)SC2(26));
            if (InRect(xr, p)) { h.id = CID_ACTION; h.action = ACT_APP_DEL; h.index = i; return h; }
        }
        // 添加区：暂存进程名 chip + 预设下拉按钮
        if (!st.appAddText.empty()) {
            RectF chip((REAL)L.contentX, (REAL)L.appAddY, (REAL)SC2(220), (REAL)SC2(30));
            if (InRect(chip, p)) { h.id = CID_ACTION; h.action = ACT_APP_MANUAL; h.index = 2; return h; }  // 点 chip 重新输入
            RectF pb((REAL)(L.contentX + L.contentW - SC2(12) - SC2(200)),
                     (REAL)L.appAddY, (REAL)SC2(200), (REAL)SC2(30));
            if (InRect(pb, p)) { h.id = CID_ACTION; h.action = ACT_APP_PICKOPEN; return h; }
        }
        // 两个来源按钮
        {
            int halfW = (L.contentW - SC2(10)) / 2;
            RectF b1((REAL)L.contentX, (REAL)L.appBtnY, (REAL)halfW, (REAL)SC2(34));
            RectF b2((REAL)(L.contentX + halfW + SC2(10)), (REAL)L.appBtnY, (REAL)halfW, (REAL)SC2(34));
            if (InRect(b1, p)) { h.id = CID_ACTION; h.action = ACT_APP_CAPTURE; return h; }
            if (InRect(b2, p)) { h.id = CID_ACTION; h.action = ACT_APP_MANUAL; h.index = 0; return h; }
        }
        return h;
    }
    case PAGE_SETTINGS: {
        // 常规开关行
        for (int k = 0; k < L.setN; k++) {
            int ry = L.setY + k * L.setRowH;
            RectF r((REAL)L.contentX, (REAL)(ry - SC2(4)), (REAL)L.contentW,
                    (REAL)(L.setRowH - SC2(4)));
            if (InRect(r, p)) {
                h.id = CID_ACTION;
                h.action = ACT_SET_TOGGLE;
                h.index = k;
                return h;
            }
        }
        // 界面语言：整行列表（点哪行就是哪种语言）
        for (int k = 0; k < 2; k++) {
            int ry = L.setLangY + k * L.setLangRowH;
            RectF r((REAL)L.contentX, (REAL)ry, (REAL)L.contentW,
                    (REAL)(L.setLangRowH - SC2(2)));
            if (InRect(r, p)) { h.id = CID_SET_LANG_ROW; h.index = k; return h; }
        }
        // 全局快捷键：录制 / 清除
        for (int k = 0; k < 3; k++) {
            int ry = L.setHkY + k * L.setHkRowH;
            RectF cb((REAL)L.hkComboX, (REAL)(ry + SC2(3)), (REAL)L.hkComboW, (REAL)SC2(28));
            if (InRect(cb, p)) { h.id = CID_HK_EDIT; h.index = k; return h; }
            RectF clr((REAL)L.hkClearX, (REAL)(ry + SC2(3)), (REAL)L.hkClearW, (REAL)SC2(28));
            if (InRect(clr, p)) { h.id = CID_HK_CLEAR; h.index = k; return h; }
        }
        return h;
    }
    case PAGE_FIRMWARE:
    default:
        return h;
    }
}

}  // namespace eui2
