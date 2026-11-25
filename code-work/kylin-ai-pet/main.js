// 克劳德大小姐桌宠主进程：透明置顶窗口 + 本地事件接口 + 作息感知。
const { app, BrowserWindow, ipcMain, Menu, Tray, screen, powerMonitor, shell } = require('electron');
const characters = require('./renderer/characters');
const path = require('path');
const fs = require('fs');
const http = require('http');

const { execFile } = require('child_process');

const PORT = 47321;
const CLIP_W = 960, CLIP_H = 540;       // 素材比例（人物在画面中间，两边留给卷轴、键盘这类大动作）
const BUBBLE_H = 64;                     // 头顶气泡的空间（够放两行）
const SIZES = { 小: 160, 中: 210, 大: 280 }; // 人物身高，中号和大肥鱼差不多

// 设置固定存在「Claude 娘桌宠」文件夹里（改名前就是这个，hooks/hook.js 也从这里读开关）
app.setPath('userData', path.join(app.getPath('appData'), 'Claude 娘桌宠'));
// 只允许一只：Claude Code 的 hook 可能同时把她叫起来好几次
if (!app.requestSingleInstanceLock()) process.exit(0);

const cfgPath = () => path.join(app.getPath('userData'), 'config.json');
let cfg = {
  size: '中', atHome: true, x: null, y: null, quiet: false, lastMorning: '', top: true, whisper: true, fling: true,
};
function loadCfg() {
  try {
    const saved = JSON.parse(fs.readFileSync(cfgPath(), 'utf8'));
    for (const key of Object.keys(cfg)) if (Object.hasOwn(saved,key)) cfg[key] = saved[key];
    if ('balance' in saved || 'chat' in saved || 'autoStart' in saved) saveCfg();
  } catch (e) {}
}
function saveCfg() {
  try { fs.writeFileSync(cfgPath(), JSON.stringify(cfg, null, 1), { mode: 0o600 }); } catch (e) {}
}

let win;
let tray;
let homeTimer;

// 动作设计：pet-config.json（右键「重载配置」重新读）
let petCfg = {};
let character = 'claude';
let entryMode = 'station';
const entryDir = () => path.join(app.getPath('appData'), 'kylin-lqui');
function refreshEntry() {
  let ini = '';
  try { ini = fs.readFileSync(path.join(entryDir(), 'ai-entry.ini'), 'utf8'); } catch {}
  const id = /^character=([a-z]+)$/m.exec(ini)?.[1];
  const mode = /^mode=pet$/m.test(ini) ? 'pet' : 'station';
  const next = Object.hasOwn(characters,id) ? id : 'claude';
  if (win && (next !== character || mode !== entryMode)) {
    if (next !== character) {
      character = next;loadPetCfg();
      const {w, h} = winSize();
      const [oldWidth, oldHeight] = win.getSize();
      if (oldWidth !== w || oldHeight !== h) {
        if (cfg.atHome) {
          win.setMinimumSize(1,1); win.setMaximumSize(9999,9999);
          win.setSize(w,h);
        } else resize(cfg.size);
      }
      win.loadFile(path.join(__dirname,'renderer','index.html'), {query:{character}});
    }
    if (mode === 'station' && entryMode === 'pet') goHome(false);
  }
  character = next;entryMode = mode;
  if (tray) updateTray();
}
function stationUi(flag) {
  execFile(path.join(app.getPath('home'), '.local/bin/liquid-cli'), [flag], error => {
    if (error) send('say','无法打开 AI 小站');
  }).unref();
}
function stationCall(method, data) {
  return new Promise((resolve, reject) => {
    const proc = execFile('/usr/bin/python3', [path.join(__dirname,'scripts','station-bridge.py'), method],
      { maxBuffer: 4 * 1024 * 1024, timeout: 610000 }, (error, out) => {
        if (error) { reject(new Error('AI 小站连接中断')); return; }
        try { const value=JSON.parse(out); if(value.error) throw new Error(value.error); resolve(value.result); }
        catch (err) { reject(err); }
      });
    proc.stdin.on('error',reject);
    proc.stdin.end(JSON.stringify(data));
  });
}
ipcMain.handle('station-drop', async (event,data) => {
  if (event.sender !== win.webContents) return {error:'未知的拖放窗口'};
  try { await stationCall('drop',data); return {}; } catch (e) { return {error:e.message}; }
});
function loadPetCfg() {
  try {
    petCfg = JSON.parse(fs.readFileSync(path.join(__dirname, 'pet-config.json'), 'utf8'));
    if (characters[character].clips) {
      petCfg.names = characters[character].names;
      petCfg.animations = characters[character].animations;
    }
    return true;
  } catch (e) {
    console.error('pet-config.json 读不了：', e.message);
    return false;
  }
}

function winSize() {
  const bodyHeight = SIZES[cfg.size] || SIZES['中'];
  const neutralPercent = characters[character].stillPlacement?.[0] || 100;
  const h = Math.round(bodyHeight * 100 / neutralPercent);
  const w = Math.round(h * CLIP_W / CLIP_H);
  return { w, h: h + BUBBLE_H };
}

function initialPosition(w, h) {
  const area = screen.getPrimaryDisplay().workArea;
  return { x: area.x + area.width - w - 8,
           y: area.y + area.height - h - 4 };
}

let dropTarget;
function startDropTarget() {
  if (process.platform !== 'linux' || !process.env.DISPLAY || dropTarget) return;
  const id = String(win.getNativeWindowHandle().readUInt32LE());
  const child = require('child_process').spawn('/usr/bin/python3', [path.join(__dirname,'scripts','x11-drop-target.py'), id],
    { stdio: ['pipe','pipe','inherit'], env: { ...process.env, QT_AUTO_SCREEN_SCALE_FACTOR:'0',
      QT_SCALE_FACTOR:'1', QT_SCREEN_SCALE_FACTORS:'1' } });
  dropTarget = child;
  child.stdin.on('error', () => {});
  child.on('error', () => { if (dropTarget === child) dropTarget = null; });
  child.on('exit', () => { if (dropTarget === child) dropTarget = null; });
  require('readline').createInterface({input:child.stdout}).on('line', line => {
    let event;
    try { event=JSON.parse(line); } catch { return; }
    if (event.event==='toggle') { cfg.atHome ? showPet() : goHome(); return; }
    if (event.character!==character || cfg.atHome) return;
    if (event.event==='thinking') send('chat-thinking');
    else if (event.event==='reply'||event.event==='error') send('chat-reply',{text:event.text,action:event.event==='error'?'A05':null});
  });
  updateDropTarget();
}
function updateDropTarget() {
  if (dropTarget && dropTarget.stdin.writable) dropTarget.stdin.write(win.isVisible()&&!cfg.atHome?'show\n':'hide\n');
}

function createWindow() {
  const { w, h } = winSize();
  const home = initialPosition(w, h);
  const x = cfg.x ?? home.x;
  const y = cfg.y ?? home.y;
  win = new BrowserWindow({
    width: w, height: h, x, y, show: !cfg.atHome,
    ...(process.platform === 'linux' ? { type: 'toolbar' } : {}),
    transparent: true, frame: false, resizable: false, hasShadow: false,
    alwaysOnTop: cfg.top, skipTaskbar: true, focusable: true,
    backgroundColor: '#00000000',
    webPreferences: { preload: path.join(__dirname, 'preload.js'), contextIsolation: true, nodeIntegration: false },
  });
  win.on('show', () => { startDropTarget();updateDropTarget(); });
  win.on('hide', updateDropTarget);
  win.on('closed', () => { if (dropTarget) dropTarget.stdin.end(); });
  startDropTarget();
  hideTaskbarEntry(win);
  win.on('minimize', () => goHome(false));
  win.setAlwaysOnTop(cfg.top, 'floating');
  win.loadFile(path.join(__dirname, 'renderer', 'index.html'), {query:{character}});
  if (process.platform !== 'linux') win.setIgnoreMouseEvents(true, { forward: true });
  win.on('will-resize', (e) => e.preventDefault());
  win.webContents.on('did-finish-load', () => {
    send('config', petCfg);
    if (cfg.quiet) send('quiet', true);
    send('whisper', cfg.whisper);
    morningCheck();
  });
}

// 麒麟窗口管理器会在窗口映射时重写状态，在显示后再声明一次任务栏提示。
function hideTaskbarEntry(window) {
  const apply = () => {
    if (window.isDestroyed()) return;
    window.setSkipTaskbar(true);
    if (process.platform === 'linux' && process.env.DISPLAY) {
      const id = String(window.getNativeWindowHandle().readUInt32LE());
      execFile('wmctrl', ['-ir', id, '-b', 'add,skip_taskbar,skip_pager'], () => {});
    }
  };
  window.on('show', apply);
  window.once('ready-to-show', apply);
}

function goHome(animate = true) {
  if (homeTimer || cfg.atHome) return;
  stopFling();
  clearInterval(dragTimer);
  dragTimer = null;
  [cfg.x, cfg.y] = win.getPosition();
  cfg.atHome = true;
  saveCfg();
  if (!animate || !win.isVisible() || win.isMinimized()) { win.hide(); return; }
  const [fromX, fromY] = win.getPosition();
  const dest = initialPosition(winSize().w, winSize().h);
  const start = Date.now();
  homeTimer = setInterval(() => {
    const t = Math.min(1, (Date.now() - start) / 300);
    place(fromX + (dest.x - fromX) * t, fromY + (dest.y - fromY) * t);
    if (t === 1) {
      clearInterval(homeTimer);
      homeTimer = null;
      win.hide();
    }
  }, 16);
}

function showPet() {
  if (!win || win.isDestroyed()) return;
  clearInterval(homeTimer);
  homeTimer = null;
  if (cfg.atHome) {
    const home = initialPosition(winSize().w, winSize().h);
    place(cfg.x ?? home.x, cfg.y ?? home.y);
    cfg.atHome = false;
    saveCfg();
  }
  if (win.isMinimized()) win.restore();
  win.show();
  win.moveTop();
}

function currentIcon() { return path.join(__dirname, 'assets','pets',character,'avatar.png'); }
function updateTray() {
  tray.setImage(currentIcon());
  tray.setToolTip(characters[character].name);
  tray.setContextMenu(Menu.buildFromTemplate([
    { label: '叫出' + characters[character].name, click: showPet },
    { label: '打开 AI 小站', click: () => stationUi('--show') },
    { label: '对话', click: openChat },
    { label: 'AI 小站设置…', click: () => stationUi('--settings') },
    { label: '回家', click: () => goHome() },
    { type: 'separator' }, { label:'退出', click: () => app.quit() },
  ]));
}
function createTray() {
  tray = new Tray(currentIcon()); updateTray();
  let clickTimer;
  const openTrayChat = () => { clearTimeout(clickTimer); clickTimer=null;openChat(); };
  tray.on('click', () => {
    if (entryMode==='station') {stationUi('--show');return;}
    if (clickTimer) {openTrayChat();return;}
    clickTimer=setTimeout(() => {clickTimer=null;cfg.atHome?showPet():goHome();},350);
  });
  tray.on('double-click', openTrayChat);
}

function place(x, y) {
  const { w, h } = winSize();
  win.setBounds({ x: Math.round(x), y: Math.round(y), width: w, height: h });
}

function send(ch, data) {
  if (win && !win.isDestroyed()) win.webContents.send(ch, data);
}

// ---------- 鼠标：穿透、拖动、右键菜单 ----------
ipcMain.on('ignore-mouse', (_e, v) => {
  if (process.platform !== 'linux') win.setIgnoreMouseEvents(!!v, { forward: true });
});
ipcMain.on('pet-shape', (e, rects) => {
  if (process.platform === 'linux' && e.sender === win.webContents) win.setShape(rects);
});

let dragTimer = null;
let samples = [];
ipcMain.on('drag-start', () => {
  stopFling();
  const start = screen.getCursorScreenPoint();
  const [wx, wy] = win.getPosition();
  samples = [];
  clearInterval(dragTimer);
  dragTimer = setInterval(() => {
    const p = screen.getCursorScreenPoint();
    const nx = wx + p.x - start.x, ny = wy + p.y - start.y;
    place(nx, ny);
    samples.push({ t: Date.now(), x: nx, y: ny });
    if (samples.length > 12) samples.shift();
  }, 12);
});
ipcMain.on('drag-end', () => {
  clearInterval(dragTimer);
  dragTimer = null;
  // 用最后约 80ms 的轨迹算松手速度，够快就甩出去
  const now = Date.now();
  const recent = samples.filter((p) => now - p.t < 100);
  if (cfg.fling && recent.length >= 2) {
    const a = recent[0], b = recent[recent.length - 1];
    const dt = Math.max(1, b.t - a.t);
    const vx = (b.x - a.x) / dt * 16, vy = (b.y - a.y) / dt * 16;   // 每帧像素
    if (Math.hypot(vx, vy) > 6) return fling(vx, vy);
  }
  [cfg.x, cfg.y] = win.getPosition();
  saveCfg();
});

// ---------- 甩出去：撞到屏幕边会弹回来，最后落在任务栏上 ----------
let flingTimer = null;
function stopFling() {
  clearInterval(flingTimer);
  flingTimer = null;
}
function fling(vx, vy) {
  stopFling();
  const { w, h } = winSize();
  const area = screen.getDisplayMatching(win.getBounds()).workArea;
  // 人物只占窗口中间一段，允许窗口透明的两边伸出屏幕，让人物本身碰到边
  const side = Math.round(w * 0.28);
  const minX = area.x - side, maxX = area.x + area.width - w + side;
  const minY = area.y - BUBBLE_H, maxY = area.y + area.height - h;
  let [x, y] = win.getPosition();
  const G = 1.1, BOUNCE = 0.62, FRICTION = 0.86, MAX = 70;
  vx = Math.max(-MAX, Math.min(MAX, vx));
  vy = Math.max(-MAX, Math.min(MAX, vy));
  send('fling', true);
  flingTimer = setInterval(() => {
    vy += G;
    x += vx;
    y += vy;
    let hit = 0;
    if (x < minX) { x = minX; hit = Math.abs(vx); vx = -vx * BOUNCE; }
    if (x > maxX) { x = maxX; hit = Math.abs(vx); vx = -vx * BOUNCE; }
    if (y < minY) { y = minY; hit = Math.abs(vy); vy = -vy * BOUNCE; }
    if (y > maxY) {
      y = maxY;
      hit = Math.max(hit, vy);
      vy = -vy * BOUNCE;
      vx *= FRICTION;
      if (Math.abs(vy) < 2.5) vy = 0;
    }
    if (hit > 4) send('bounce', Math.min(1, hit / 30));
    send('fling-v', vx);
    place(x, y);
    if (y >= maxY && vy === 0 && Math.abs(vx) < 0.6) {
      stopFling();
      place(x, y);
      send('fling', false);
      [cfg.x, cfg.y] = win.getPosition();
      saveCfg();
    }
  }, 16);
}

function nameOf(k) { return (petCfg.names && petCfg.names[k]) || k; }
function clipItems(list, seen) {
  return (list || []).flat().filter((k) => !seen.has(k) && seen.add(k)).map((k) => ({ label: nameOf(k), click: () => send('play', k) }));
}

// 「动作」子菜单：按 pet-config.json 的分类列出来，和 DSH 一样
function actionMenu() {
  const a = petCfg.animations || {};
  const ev = a.events || {};
  const groups = [
    ['待机', a.idle],
    ['点击回应', a.clicks],
    ...(a.categories || []).map((c) => [c.id, c.actions]),
    ['碎碎念', ev.whisper],
    ['工作状态', ev.workStatus],
  ];
  // 同一个动作只在第一个出现的分组里列一次，避免菜单里重复
  const seen = new Set();
  return groups.map(([label, l]) => ({ label, submenu: clipItems(l, seen) })).filter((g) => g.submenu.length);
}

ipcMain.on('menu', () => {
  const menu = Menu.buildFromTemplate([
    { label: '打开 AI 小站', click: () => stationUi('--show') },
    { label: 'AI 小站设置…', click: () => stationUi('--settings') },
    { label: '现在截图', click: () => setTimeout(() => stationUi('--capture'),250) },
    { label: '延时 3 秒截图', click: () => setTimeout(() => stationUi('--capture'),3000) },
    { type: 'separator' },
    { label: '碎碎念', click: () => send('whisper-now') },
    { label: '对话', click: () => openChat() },
    { label: '回家', click: () => goHome() },
    { label: '移到桌面右下角', click: resetPosition },
    { label: '重载配置', click: () => { if (loadPetCfg()) { send('config', petCfg); send('say', '配置重新读好啦'); } else send('say', '配置文件写错了…'); } },
    { label: '动作', submenu: actionMenu() },
    { type: 'separator' },
    { label: '设置', submenu: [
      { label: '大小', submenu: Object.keys(SIZES).map((s) => ({
        label: s, type: 'radio', checked: cfg.size === s, click: () => resize(s),
      })) },
      { label: '自动碎碎念', type: 'checkbox', checked: cfg.whisper, click: (m) => {
        cfg.whisper = m.checked; saveCfg(); send('whisper', cfg.whisper);
      } },
      { label: '安静模式（不冒气泡）', type: 'checkbox', checked: cfg.quiet, click: (m) => {
        cfg.quiet = m.checked; saveCfg(); send('quiet', cfg.quiet);
      } },
      { label: '可以甩出去（撞墙反弹）', type: 'checkbox', checked: cfg.fling, click: (m) => {
        cfg.fling = m.checked; saveCfg();
      } },
      { label: '总在最前', type: 'checkbox', checked: cfg.top, click: (m) => {
        cfg.top = m.checked; saveCfg(); win.setAlwaysOnTop(cfg.top, 'floating');
      } },
    ] },
    { label: '退出', click: () => app.quit() },
  ]);
  menu.popup({ window: win });
});

function resetPosition() {
  stopFling();
  const { w, h } = winSize();
  const home = initialPosition(w, h);
  place(home.x, home.y);
  [cfg.x, cfg.y] = win.getPosition();
  saveCfg();
}

function resize(s) {
  cfg.size = s;
  const [x, y] = win.getPosition();
  const [ow, oh] = win.getSize();
  const { w, h } = winSize();
  win.setMinimumSize(1, 1); win.setMaximumSize(9999, 9999);
  // 以脚底中心为锚点缩放，人不会跳位置
  win.setBounds({ x: Math.round(x + ow / 2 - w / 2), y: y + oh - h, width: w, height: h });
  [cfg.x, cfg.y] = win.getPosition();
  saveCfg();
}

// 从桌宠和托盘打开同一个原生抽屉，由任务栏提供准确的入口坐标。
function openChat() {
  execFile('gdbus',['call','--session','--dest','org.ukui.AiStationPanel',
    '--object-path','/AiStation','--method','org.ukui.AiStationPanel.OpenPetChat',character],(err,reply) => {
    if (err || !reply.includes('true')) stationUi('--show');
  });
}
ipcMain.on('open-chat', openChat);

// ---------- Claude Code 事件接口 ----------
// hooks/hook.js 把 Claude Code 的 hook 事件 POST 到这里。
function classify(evt) {
  const name = evt.hook_event_name || evt.event || '';
  switch (name) {
    case 'UserPromptSubmit': return 'thinking';
    case 'PreToolUse': return 'working';
    case 'PostToolUseFailure': return 'error';
    case 'PostToolUse': {
      const r = evt.tool_response;
      const failed = r && typeof r === 'object' && (r.is_error === true || r.success === false || (r.error && r.error !== ''));
      return failed ? 'error' : 'working';
    }
    case 'Notification': return 'needs-you';
    case 'Stop': return 'done';
    case 'SessionEnd': return 'idle';
    default: return null;
  }
}

function startServer() {
  const server = http.createServer((req, res) => {
    const url = new URL(req.url, `http://127.0.0.1:${PORT}`);
    const done = (code, body) => { res.writeHead(code, { 'Content-Type': 'text/plain; charset=utf-8' }); res.end(body); };
    if (req.method === 'POST' && url.pathname === '/hook') {
      let body = '';
      req.on('data', (c) => { body += c; if (body.length > 1e6) req.destroy(); });
      req.on('end', () => {
        let evt = {};
        try { evt = JSON.parse(body || '{}'); } catch (e) {}
        const state = classify(evt);
        if (state) send('claude', state);
        done(200, state || 'ignored');
      });
      return;
    }
    if (url.pathname === '/ping') return done(200, 'pong');
    if (url.pathname === '/play') { send('play', url.searchParams.get('clip')); return done(200, 'ok'); }
    if (url.pathname === '/say') { send('say', url.searchParams.get('text')); return done(200, 'ok'); }
    if (url.pathname === '/state') { send('claude', url.searchParams.get('s')); return done(200, 'ok'); }
    done(404, 'not found');
  });
  server.on('error', (e) => console.error('桌宠接口启动失败：', e.message));
  server.listen(PORT, '127.0.0.1');
}

// ---------- 作息：早安、熬夜、离开和回来 ----------
const today = () => new Date().toLocaleDateString('sv-SE');

function morningCheck() {
  const h = new Date().getHours();
  if (cfg.lastMorning !== today() && h >= 5) {
    cfg.lastMorning = today();
    saveCfg();
    setTimeout(() => send('play', 'B05'), 1500);
  }
}

let away = false;
let lastNight = 0;
function tick() {
  const idle = powerMonitor.getSystemIdleTime();     // 秒
  if (!away && idle >= 30 * 60) away = true;
  if (away && idle < 5) {
    away = false;
    send('play', 'B04');
    morningCheck();
  }
  const h = new Date().getHours();
  if (h < 5 && idle < 60 && Date.now() - lastNight > 45 * 60 * 1000) {
    lastNight = Date.now();
    send('play', 'B02');
  }
}

app.whenReady().then(() => {
  loadCfg();
  fs.mkdirSync(entryDir(),{recursive:true});
  refreshEntry();
  let refreshTimer;
  fs.watch(entryDir(),(_event, name) => {
    if (!name || name === 'ai-entry.ini') { clearTimeout(refreshTimer);refreshTimer=setTimeout(refreshEntry,60); }
  });
  if(process.argv.includes('--show')) cfg.atHome=false;
  loadPetCfg();
  createTray();
  createWindow();
  startServer();
  setInterval(tick, 5000);
  powerMonitor.on('resume', () => { away = true; });
});

app.on('second-instance', showPet);

app.on('window-all-closed', () => app.quit());
