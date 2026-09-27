# -*- coding: utf-8 -*-
"""生成 _tools/preview.html：把三个页面的 UI 静态还原成可在浏览器查看的预览稿。
图片以 data URI 内嵌，产出的 HTML 完全自包含。
"""
import base64
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
IMG_DIR = os.path.join(ROOT, 'utils', 'img')

ICONS = [
    'shidu.png', 'wendu.png', 'light.png', 'light-red.png', 'light-yellow.png',
    'lightoff.png', 'light-red-on.png', 'light-yellow-on.png', 'humi-on.png',
    'humi-off.png', 'auto.png', 'manual.png', 'wran.png',
]


def data_uri(name):
    path = os.path.join(IMG_DIR, name)
    with open(path, 'rb') as fh:
        return 'data:image/png;base64,' + base64.b64encode(fh.read()).decode('ascii')


def build():
    im = {name: data_uri(name) for name in ICONS}

    # 曲线示例数据（20 点）
    temp = [24.1, 24.3, 24.6, 24.9, 25.2, 25.4, 25.6, 25.5, 25.7, 25.9,
            26.1, 26.0, 25.8, 25.6, 25.5, 25.3, 25.2, 25.4, 25.5, 25.6]
    humi = [41, 42, 43, 44, 45, 46, 47, 47, 48, 49,
            50, 50, 49, 48, 47, 46, 45, 45, 44, 43]
    times = ['16:0%d:%02d' % (i // 6, (i * 3) % 60) for i in range(20)]

    # 简单映射到 SVG 坐标
    w, h = 327, 210
    pad_l, pad_r, pad_t, pad_b = 34, 8, 12, 22
    pw, ph = w - pad_l - pad_r, h - pad_t - pad_b
    lo, hi = 20.0, 55.0

    def pts(values):
        out = []
        for i, v in enumerate(values):
            x = pad_l + pw * i / (len(values) - 1)
            y = pad_t + ph - (v - lo) / (hi - lo) * ph
            out.append('%.1f,%.1f' % (x, y))
        return ' '.join(out)

    grid_lines = ''
    for i in range(5):
        y = pad_t + ph * i / 4
        val = hi - (hi - lo) * i / 4
        grid_lines += '<line x1="%d" y1="%.1f" x2="%d" y2="%.1f" stroke="#eef1f6"/>' % (
            pad_l, y, pad_l + pw, y)
        grid_lines += '<text x="%d" y="%.1f" fill="#9aa4b0" font-size="9" text-anchor="end" dominant-baseline="middle">%.1f</text>' % (
            pad_l - 5, y, val)

    x_labels = ''
    for i in (0, 6, 12, 19):
        x = pad_l + pw * i / 19
        x_labels += '<text x="%.1f" y="%d" fill="#9aa4b0" font-size="9" text-anchor="middle">%s</text>' % (
            x, h - 6, times[i])

    chart = f'''<svg viewBox="0 0 {w} {h}" width="100%" height="210" style="display:block">
      <rect width="{w}" height="{h}" fill="#fff"/>
      {grid_lines}
      <circle cx="{pad_l + 4}" cy="9" r="3.5" fill="#ee6c3a"/>
      <text x="{pad_l + 13}" y="12" fill="#5b6673" font-size="10">温度 25.6℃</text>
      <circle cx="{pad_l + 82}" cy="9" r="3.5" fill="#1976d2"/>
      <text x="{pad_l + 91}" y="12" fill="#5b6673" font-size="10">湿度 43.0%</text>
      <polyline points="{pts(temp)}" fill="none" stroke="#ee6c3a" stroke-width="2"
                stroke-linejoin="round" stroke-linecap="round"/>
      <polyline points="{pts(humi)}" fill="none" stroke="#1976d2" stroke-width="2"
                stroke-linejoin="round" stroke-linecap="round"/>
      <circle cx="{pad_l + pw}" cy="{pad_t + ph - (temp[-1] - lo) / (hi - lo) * ph:.1f}"
              r="4" fill="#fff" stroke="#ee6c3a" stroke-width="2.5"/>
      <circle cx="{pad_l + pw}" cy="{pad_t + ph - (humi[-1] - lo) / (hi - lo) * ph:.1f}"
              r="4" fill="#fff" stroke="#1976d2" stroke-width="2.5"/>
      {x_labels}
    </svg>'''

    css = '''
    * { box-sizing: border-box; }
    body { margin:0; padding:32px 24px 64px; background:#eef1f6; color:#1f2937;
      font-family:-apple-system,BlinkMacSystemFont,'Segoe UI','PingFang SC','Microsoft Yahei',sans-serif; }
    .head { max-width:1240px; margin:0 auto 28px; }
    .head h1 { margin:0 0 8px; font-size:22px; }
    .head p { margin:0; color:#5b6673; font-size:14px; line-height:1.7; }
    .head code { background:#fff; border-radius:4px; padding:1px 6px; font-size:13px; color:#1976d2; }
    .frames { display:flex; flex-wrap:wrap; gap:28px; justify-content:center; max-width:1240px; margin:0 auto; }
    .frame { width:375px; }
    .frame__label { margin:0 0 10px 4px; font-size:14px; font-weight:600; color:#5b6673; }
    .phone { width:375px; height:760px; overflow-y:auto; background:#f4f7fb;
      border-radius:26px; border:8px solid #1f2937; box-shadow:0 18px 40px rgba(31,41,55,.18); }
    .navbar { position:sticky; top:0; z-index:9; height:48px; display:flex; align-items:center;
      justify-content:center; background:#fff; font-size:15px; font-weight:600; }
    .body { padding-bottom:20px; }

    .card { margin:12px; padding:14px; background:#fff; border-radius:10px;
      box-shadow:0 2px 10px rgba(31,41,55,.05); }
    .card-title { display:flex; align-items:center; font-size:15px; font-weight:600; color:#1976d2; }
    .card-title::before { content:''; width:4px; height:15px; margin-right:7px;
      border-radius:2px; background:#1976d2; }
    .card-head { display:flex; align-items:center; justify-content:space-between; margin-bottom:12px; }
    .mb-24 { margin-bottom:12px; }

    .pill { padding:3px 10px; border-radius:999px; font-size:12px; font-weight:500; }
    .pill--on { color:#4caf50; background:rgba(76,175,80,.14); }
    .pill--off { color:#e54545; background:rgba(229,69,69,.12); }
    .muted { color:#9aa4b0; font-size:12px; }

    .tabs { display:flex; background:#fff; padding:0 12px; gap:24px; }
    .tabs span { padding:11px 0; font-size:14px; color:#5b6673; position:relative; }
    .tabs span.on { color:#1976d2; font-weight:600; }
    .tabs span.on::after { content:''; position:absolute; left:50%; bottom:0; width:24px;
      height:3px; margin-left:-12px; border-radius:2px; background:#1976d2; }

    .alarm { margin:12px 12px 0; display:flex; align-items:center; gap:8px;
      padding:9px 12px; border-radius:8px; background:#fdecec; color:#e54545; font-size:12.5px; }
    .alarm img { width:16px; height:16px; }

    .metric-grid { display:flex; margin:4px 0 12px; }
    .metric { flex:1; display:flex; flex-direction:column; align-items:center; padding:6px 0; }
    .metric + .metric { border-left:1px solid #ebedf0; }
    .metric img { width:42px; height:42px; margin-bottom:6px; }
    .metric .label { font-size:12px; color:#9aa4b0; }
    .metric .value { display:flex; align-items:baseline; margin-top:3px; }
    .metric .num { font-size:32px; font-weight:600; color:#1976d2; line-height:1.1; }
    .metric .unit { margin-left:3px; font-size:13px; color:#5b6673; }

    .foot { display:flex; padding-top:10px; border-top:1px solid #ebedf0; }
    .foot > div { flex:1; display:flex; flex-direction:column; }
    .foot .k { font-size:12px; color:#9aa4b0; }
    .foot .v { margin-top:4px; font-size:15px; font-weight:500; }

    .setting-row { display:flex; align-items:center; justify-content:space-between; padding:9px 0; }
    .setting-row .k { display:flex; align-items:center; font-size:14px; color:#5b6673; }
    .setting-row .k img { width:23px; height:23px; margin-right:7px; }
    .stepper { display:flex; align-items:center; border-radius:4px; overflow:hidden; }
    .stepper i { width:26px; height:26px; display:flex; align-items:center; justify-content:center;
      background:#f2f3f5; color:#323233; font-style:normal; font-size:15px; }
    .stepper b { width:34px; text-align:center; font-size:13px; font-weight:500;
      border-top:1px solid #ebedf0; border-bottom:1px solid #ebedf0; line-height:26px; }

    .stage { display:flex; flex-direction:column; align-items:center; padding:4px 0 14px; }
    .stage img { width:130px; height:130px; }
    .stage .tip { margin-top:6px; font-size:12px; color:#9aa4b0; }

    .mode-row { display:flex; gap:12px; margin-bottom:12px; }
    .mode-btn { flex:1; height:48px; display:flex; align-items:center; justify-content:center;
      gap:6px; font-size:14px; font-weight:500; color:#5b6673; background:#f5f7fa; border-radius:8px; }
    .mode-btn img { width:22px; height:22px; }
    .mode-btn.on { color:#fff; background:#1976d2; }
    .power { width:100%; height:44px; line-height:44px; text-align:center; border-radius:8px;
      font-size:15px; font-weight:600; color:#fff; background:#1976d2; }
    .power.off { background:#eceff4; color:#5b6673; }
    .power.disabled { background:#eef1f6; color:#9aa4b0; }

    .lights { display:flex; gap:10px; }
    .light { flex:1; display:flex; flex-direction:column; align-items:center; padding:10px 8px;
      background:#f7f9fc; border:1px solid transparent; border-radius:8px; }
    .light.on { background:rgba(25,118,210,.06); border-color:rgba(25,118,210,.28); }
    .light > img { width:70px; height:70px; margin-bottom:6px; }
    .light .row { display:flex; align-items:center; width:100%; }
    .light .row img { width:20px; height:20px; margin-right:4px; }
    .light .row span { flex:1; font-size:14px; }
    .sw { position:relative; width:40px; height:24px; border-radius:12px; background:#dcdee0; }
    .sw::after { content:''; position:absolute; top:2px; left:2px; width:20px; height:20px;
      border-radius:50%; background:#fff; box-shadow:0 1px 3px rgba(0,0,0,.2); }
    .sw.on { background:#1976d2; }
    .sw.on::after { left:18px; }

    .footer-actions { display:flex; gap:10px; margin:12px 12px 0; }
    .footer-actions > div { flex:1; display:flex; flex-direction:column; padding:14px 12px;
      background:#fff; border-radius:10px; box-shadow:0 2px 10px rgba(31,41,55,.05); }
    .footer-actions .t { font-size:15px; font-weight:600; }
    .footer-actions .d { margin-top:4px; font-size:12px; color:#9aa4b0; }

    .summary { display:flex; margin:12px 12px 0; padding:14px 0; background:#fff; border-radius:10px;
      box-shadow:0 2px 10px rgba(31,41,55,.05); }
    .summary > div { flex:1; display:flex; flex-direction:column; align-items:center; }
    .summary > div + div { border-left:1px solid #ebedf0; }
    .summary .v { font-size:24px; font-weight:600; }
    .summary .v.on { color:#4caf50; }
    .summary .v.warn { color:#e54545; }
    .summary .k { margin-top:3px; font-size:12px; color:#9aa4b0; }

    .room-card { margin:12px 12px 0; padding:14px; background:#fff; border-radius:10px;
      border:1px solid rgba(25,118,210,.35); background-image:none;
      box-shadow:0 2px 10px rgba(31,41,55,.05); }
    .room-card.plain { border-color:transparent; }
    .room-card__head { display:flex; align-items:flex-start; justify-content:space-between; }
    .room-card__name { font-size:16px; font-weight:600; }
    .room-card__topic { margin-top:3px; font-size:11px; color:#9aa4b0; }
    .room-card__values { display:flex; margin-top:12px; }
    .room-card__values > div { flex:1; }
    .room-card__num { display:flex; align-items:baseline; font-size:20px; font-weight:600; color:#1976d2; }
    .room-card__num small { margin-left:2px; font-size:12px; font-weight:400; color:#5b6673; }
    .room-card__k { margin-top:3px; font-size:11px; color:#9aa4b0; }
    .room-card__foot { display:flex; justify-content:space-between; margin-top:10px;
      padding-top:9px; border-top:1px solid #ebedf0; font-size:12px; color:#9aa4b0; }

    .field { display:flex; align-items:center; justify-content:space-between; padding:10px 0;
      font-size:14px; }
    .field .k { color:#5b6673; }
    .field .v { color:#1f2937; }
    .field .v.ph { color:#c8c9cc; }
    .hint { margin-top:8px; padding:8px 9px; font-size:11.5px; line-height:1.6; color:#9aa4b0;
      background:#f7f9fc; border-radius:6px; }
    .room-row { display:flex; align-items:center; padding:10px 0; border-bottom:1px solid #ebedf0; }
    .room-row:last-of-type { border-bottom:none; }
    .room-row .main { flex:1; }
    .room-row .name { font-size:15px; }
    .room-row .badge { margin-left:6px; padding:1px 6px; font-size:10px; color:#1976d2;
      background:#e8f2fd; border-radius:4px; }
    .room-row .topic { margin-top:4px; font-size:11px; color:#9aa4b0; }
    .room-row .ops { display:flex; gap:4px; font-size:13px; color:#1976d2; }
    .room-row .ops .danger { color:#e54545; }
    .save-bar { position:sticky; bottom:0; margin-top:14px; padding:10px 12px;
      background:#fff; box-shadow:0 -2px 8px rgba(31,41,55,.06); }
    .btn-primary { height:44px; line-height:44px; text-align:center; border-radius:22px;
      background:#1976d2; color:#fff; font-size:15px; }
    .btn-danger-line { height:34px; line-height:32px; text-align:center; border-radius:6px;
      border:1px solid #e54545; color:#e54545; font-size:13px; }
    '''

    def stepper(value):
        return ('<div class="stepper"><i>−</i><b>%s</b><i>+</i></div>' % value)

    def setting_row(label, value, icon=None):
        left = '<img src="%s">' % icon if icon else ''
        return ('<div class="setting-row"><div class="k">%s%s</div>%s</div>'
                % (left, label, stepper(value)))

    index_html = f'''
    <div class="navbar">客房 8801 · 智慧监控</div>
    <div class="body">
      <div class="tabs"><span class="on">客房 8801</span><span>客房 8802</span><span>会议室 A</span></div>

      <div class="alarm"><img src="{im['wran.png']}">湿度过低（34.0% &lt; 设定 40%）　温度过高（36.2℃ &gt; 35℃）</div>

      <div class="card">
        <div class="card-head">
          <div class="card-title">实时环境</div>
          <div class="pill pill--on">设备在线</div>
        </div>
        <div class="metric-grid">
          <div class="metric">
            <img src="{im['shidu.png']}"><div class="label">湿度</div>
            <div class="value"><div class="num">34.0</div><div class="unit">%</div></div>
          </div>
          <div class="metric">
            <img src="{im['wendu.png']}"><div class="label">温度</div>
            <div class="value"><div class="num">36.2</div><div class="unit">℃</div></div>
          </div>
        </div>
        <div class="foot">
          <div><div class="k">烟雾值</div><div class="v">213</div></div>
          <div><div class="k">上传时间</div><div class="v">09-15 16:08:50</div></div>
        </div>
      </div>

      <div class="card">
        <div class="card-title mb-24">舒适度设定</div>
        {setting_row('设定湿度', 40, im['shidu.png'])}
        {setting_row('设定温度', 24, im['wendu.png'])}
      </div>

      <div class="card">
        <div class="card-head">
          <div class="card-title">加湿器控制</div>
          <div class="pill pill--on">运行中</div>
        </div>
        <div class="stage">
          <img src="{im['humi-on.png']}">
          <div class="tip">手动模式：由你手动开关</div>
        </div>
        <div class="mode-row">
          <div class="mode-btn on"><img src="{im['manual.png']}">手动模式</div>
          <div class="mode-btn"><img src="{im['auto.png']}">自动模式</div>
        </div>
        <div class="power off">关闭加湿器</div>
      </div>

      <div class="card">
        <div class="card-head">
          <div class="card-title">灯光控制</div>
          <div class="muted">一路灯带：红 / 黄互斥</div>
        </div>
        <div class="lights">
          <div class="light">
            <img src="{im['lightoff.png']}">
            <div class="row"><img src="{im['light.png']}"><span>黄灯</span><div class="sw"></div></div>
          </div>
          <div class="light on">
            <img src="{im['light-red-on.png']}">
            <div class="row"><img src="{im['light-red.png']}"><span>红灯</span><div class="sw on"></div></div>
          </div>
        </div>
      </div>

      <div class="card">
        <div class="card-head">
          <div class="card-title">温湿度曲线分布</div>
          <div class="muted">最近 20 个采样点</div>
        </div>
        {chart}
      </div>

      <div class="footer-actions">
        <div><div class="t">房间总览</div><div class="d">共 3 个房间</div></div>
        <div><div class="t">参数设置</div><div class="d">云端 · 阈值 · 房间</div></div>
      </div>
    </div>'''

    rooms_html = f'''
    <div class="navbar">房间总览</div>
    <div class="body">
      <div class="summary">
        <div><div class="v">3</div><div class="k">房间总数</div></div>
        <div><div class="v on">2</div><div class="k">设备在线</div></div>
        <div><div class="v warn">1</div><div class="k">告警房间</div></div>
      </div>

      <div class="room-card">
        <div class="room-card__head">
          <div>
            <div class="room-card__name">客房 8801</div>
            <div class="room-card__topic">Smart / SmartCtrl</div>
          </div>
          <div class="pill pill--on">在线</div>
        </div>
        <div class="room-card__values">
          <div><div class="room-card__num">36.2<small>℃</small></div><div class="room-card__k">温度</div></div>
          <div><div class="room-card__num">34.0<small>%</small></div><div class="room-card__k">湿度</div></div>
          <div><div class="room-card__num">213</div><div class="room-card__k">烟雾值</div></div>
        </div>
        <div class="room-card__warn" style="margin-top:10px;padding:7px 9px;font-size:12px;color:#e54545;background:#fdecec;border-radius:6px">
          温度过高（36.2℃ &gt; 35℃）；湿度过低（34.0% &lt; 设定 40%）
        </div>
        <div class="room-card__foot"><span>加湿器 运行中</span><span>更新于 09-15 16:08:50</span></div>
      </div>

      <div class="room-card plain">
        <div class="room-card__head">
          <div>
            <div class="room-card__name">客房 8802</div>
            <div class="room-card__topic">Smart2 / SmartCtrl2</div>
          </div>
          <div class="pill pill--on">在线</div>
        </div>
        <div class="room-card__values">
          <div><div class="room-card__num">25.6<small>℃</small></div><div class="room-card__k">温度</div></div>
          <div><div class="room-card__num">43.0<small>%</small></div><div class="room-card__k">湿度</div></div>
          <div><div class="room-card__num">86</div><div class="room-card__k">烟雾值</div></div>
        </div>
        <div class="room-card__foot"><span>加湿器 已关闭</span><span>更新于 09-15 16:08:47</span></div>
      </div>

      <div class="room-card plain">
        <div class="room-card__head">
          <div>
            <div class="room-card__name">会议室 A</div>
            <div class="room-card__topic">MeetingA / MeetingCtrlA</div>
          </div>
          <div class="pill pill--off">离线</div>
        </div>
        <div class="room-card__values">
          <div><div class="room-card__num">--<small>℃</small></div><div class="room-card__k">温度</div></div>
          <div><div class="room-card__num">--<small>%</small></div><div class="room-card__k">湿度</div></div>
          <div><div class="room-card__num">--</div><div class="room-card__k">烟雾值</div></div>
        </div>
        <div class="room-card__foot"><span>加湿器 已关闭</span><span>暂无上报数据</span></div>
      </div>
    </div>'''

    setting_html = f'''
    <div class="navbar">参数设置</div>
    <div class="body">
      <div class="card">
        <div class="card-title mb-24">云端连接</div>
        <div class="field"><div class="k">巴法云私钥</div><div class="v">614b23ad…007cb0</div></div>
        {setting_row('协议类型', 3)}
        {setting_row('轮询周期（ms）', 3000)}
        <div class="hint">本项目配套 STM32 设备使用 TCP 协议，协议类型填 3；轮询周期越小数据越实时，但请求更频繁。</div>
      </div>

      <div class="card">
        <div class="card-title mb-24">告警阈值</div>
        {setting_row('温度下限（℃）', 16)}
        {setting_row('温度上限（℃）', 35)}
        {setting_row('湿度偏差（%）', 5)}
        {setting_row('烟雾阈值（ADC）', 0)}
        <div class="hint">温度越界、湿度偏离设定值超过「湿度偏差」时首页会弹出告警。「烟雾阈值」为 0 表示不启用烟雾告警。</div>
      </div>

      <div class="card">
        <div class="card-title mb-24">数据展示</div>
        {setting_row('曲线保留点数（个）', 20)}
      </div>

      <div class="card">
        <div class="card-head">
          <div class="card-title">房间列表</div>
          <div class="muted">共 3 个</div>
        </div>
        <div class="room-row">
          <div class="main">
            <div class="name">客房 8801<span class="badge">当前</span></div>
            <div class="topic">温湿度 Smart · 控制 SmartCtrl</div>
          </div>
          <div class="ops"><span>编辑</span><span class="danger">删除</span></div>
        </div>
        <div class="room-row">
          <div class="main">
            <div class="name">客房 8802</div>
            <div class="topic">温湿度 Smart2 · 控制 SmartCtrl2</div>
          </div>
          <div class="ops"><span>编辑</span><span class="danger">删除</span></div>
        </div>
        <div class="room-row">
          <div class="main">
            <div class="name">会议室 A</div>
            <div class="topic">温湿度 MeetingA · 控制 MeetingCtrlA</div>
          </div>
          <div class="ops"><span>编辑</span><span class="danger">删除</span></div>
        </div>
        <div style="margin-top:12px;height:34px;line-height:32px;text-align:center;border-radius:6px;border:1px solid #1976d2;color:#1976d2;font-size:13px">+ 新增房间</div>
        <div class="hint">每个房间对应巴法云上的一组主题：温湿度主题用于接收下位机上报，控制主题用于下发灯光与加湿器指令。</div>
      </div>

      <div class="card">
        <div class="btn-danger-line">恢复默认配置</div>
      </div>

      <div class="save-bar"><div class="btn-primary">保存配置</div></div>
    </div>'''

    html = f'''<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>未来酒店·智慧监控 — 界面预览</title>
<style>{css}</style>
</head>
<body>
  <div class="head">
    <h1>未来酒店 · 智慧监控 — 界面预览</h1>
    <p>这是三个页面的静态还原稿，用于快速查看排版与配色（真实交互需在微信开发者工具中运行）。
       页面分别为 <code>pages/index</code>（智慧监控）、<code>pages/rooms</code>（房间总览）、
       <code>pages/setting</code>（参数设置），示例数据用于展示告警态与在线/离线态。</p>
  </div>
  <div class="frames">
    <div class="frame"><div class="frame__label">① 智慧监控（含告警态）</div>
      <div class="phone">{index_html}</div></div>
    <div class="frame"><div class="frame__label">② 房间总览（多房间）</div>
      <div class="phone">{rooms_html}</div></div>
    <div class="frame"><div class="frame__label">③ 参数设置</div>
      <div class="phone">{setting_html}</div></div>
  </div>
</body>
</html>'''

    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'preview.html')
    with open(out, 'w', encoding='utf-8') as fh:
        fh.write(html)
    print('written:', out)
    print('size: %.1f KB' % (os.path.getsize(out) / 1024))


if __name__ == '__main__':
    build()
