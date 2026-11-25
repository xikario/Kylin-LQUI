// Electron 窗口里的交互：拖动、点击、右键菜单、透明区域鼠标穿透。
const api = window.petApi;
const roleId = new URLSearchParams(location.search).get('character') || 'claude';
const role = window.petCharacters[roleId] || window.petCharacters.claude;
document.title = role.name + '桌宠';
const pet = new Pet(document.getElementById('stage'), {
  clips: role.clips, base: role.clips ? '../assets/pets/' + roleId + '/' : '../clips/'
});

if (role.stillPlacement) {
  // Match the full-height stills to the source animation's neutral stance.
  const [height, bottom] = role.stillPlacement;
  for (const img of pet.layer.querySelectorAll('img')) {
    img.style.height = height + '%';
    img.style.top = 'auto';
    img.style.bottom = bottom + '%';
  }
}

// X11 的穿透窗口收不到鼠标移动。用原作各动作的轮廓设定可见、可点击的窗口区域。
if (api.nativeShape) {
  const masks = window.PET_MASKS;
  const envelopes = new Map();
  function clipRect(clip) {
    const bounds = role.clips?.[clip]?.bounds;
    if (bounds) {
      // Native masks must cover the whole animation, including tails, musical notes and props.
      const box = pet.layer.getBoundingClientRect();
      const scale = Math.min(box.width / 640, box.height / 360);
      const left = box.left + (box.width - 640 * scale) / 2;
      const top = box.bottom - 360 * scale;
      const pad = Math.ceil(box.height * .08);
      const x = Math.max(0, Math.floor(left + bounds[0]*scale) - pad);
      const y = Math.max(0, Math.floor(top + bounds[1]*scale) - pad);
      const right = Math.min(innerWidth, Math.ceil(left + bounds[2]*scale) + pad);
      const bottom = Math.min(innerHeight, Math.ceil(top + bounds[3]*scale) + pad);
      return {x, y, width:Math.max(1,right-x), height:Math.max(1,bottom-y)};
    }
    if (role.clips) {
      const bodyHeight = pet.layer.clientHeight * (role.stillPlacement?.[0] || 100) / 100;
      const width = Math.min(innerWidth, Math.ceil(bodyHeight * (roleId === 'fish' ? 1.36 : 1.18)));
      return { x: Math.floor((innerWidth - width) / 2), y: pet.layer.offsetTop - 10, width, height: pet.layer.clientHeight + 10 };
    }
    if (!envelopes.has(clip)) {
      let left = masks.w, right = 0;
      for (const frame of masks.clips[clip] || []) {
        const bytes = atob(frame);
        for (let cell = 0; cell < masks.w * masks.h; cell++) {
          if (bytes.charCodeAt(cell >> 3) & (1 << (7 - (cell & 7)))) {
            left = Math.min(left, cell % masks.w);
            right = Math.max(right, cell % masks.w + 1);
          }
        }
      }
      envelopes.set(clip, right > left ? [left / masks.w, right / masks.w] : [0, 1]);
    }
    const [left, right] = envelopes.get(clip);
    // 拖动倾斜、切换动画时给四周留出余量。
    const pad = Math.ceil(innerHeight * 0.12);
    const x = Math.max(0, Math.floor(left * innerWidth) - pad);
    const far = Math.min(innerWidth, Math.ceil(right * innerWidth) + pad);
    const y = Math.max(0, pet.layer.offsetTop - pad);
    return { x, y, width: far - x, height: innerHeight - y };
  }
  function updateShape() {
    const rects = Array.from(pet.layer.querySelectorAll('.on'), v => clipRect(v.dataset.clip));
    rects.push(clipRect(pet.current));
    if (pet.bubble.classList.contains('on')) {
      const b = pet.bubble.getBoundingClientRect();
      rects.push({ x: Math.floor(b.left) - 8, y: 0, width: Math.ceil(b.width) + 16, height: Math.ceil(b.bottom) + 16 });
    }
    api.send('pet-shape', rects);
  }
  const observer = new MutationObserver(updateShape);
  observer.observe(pet.layer, { subtree: true, attributes: true, attributeFilter: ['class'] });
  observer.observe(pet.bubble, { childList: true, attributes: true, attributeFilter: ['class'] });
  window.addEventListener('resize', updateShape);
  updateShape();
}

api.on('config', (cfg) => pet.setConfig(cfg));
api.on('whisper-now', () => pet.whisper(true));
api.on('claude', (state) => pet.claudeEvent(state));
api.on('play', (name) => pet.play(name));
api.on('say', (text) => pet.say(text));
api.on('quiet', (q) => pet.setQuiet(q));
api.on('whisper', (on) => pet.setWhisper(on));
api.on('chat-thinking', () => pet.chatThinking());
api.on('chat-reply', (r) => pet.chatReply(r));
api.on('bounce', (k) => pet.bounce(k));
api.on('fling-v', (vx) => pet.lean(vx * 0.6));
api.on('fling', (on) => { if (!on) pet.release(); });

// 双击打开聊天窗
window.addEventListener('dblclick', () => api.send('open-chat'));

// 透明的地方让鼠标穿透到下面的窗口
let ignoring = null;
function setIgnore(v) {
  if (api.nativeShape || ignoring === v) return;
  ignoring = v;
  api.send('ignore-mouse', v);
}

let down = null;
let dragging = false;
let lastX = 0, lastT = 0, vx = 0;

window.addEventListener('mousemove', (e) => {
  if (down) {
    if (!dragging && Math.hypot(e.screenX - down.x, e.screenY - down.y) > 4) {
      dragging = true;
      api.send('drag-start');
      lastX = e.screenX;
      lastT = performance.now();
    }
    if (dragging) {
      // 用屏幕坐标算拖动速度（窗口跟着鼠标走，窗口内坐标几乎不变）
      const now = performance.now();
      const dt = Math.max(8, now - lastT);
      vx = vx * 0.6 + ((e.screenX - lastX) / dt) * 16 * 0.4;
      lastX = e.screenX;
      lastT = now;
      pet.lean(vx);
    }
    return;
  }
  setIgnore(!pet.hitTest(e.clientX, e.clientY));
});

function endDrag() {
  if (dragging) {
    api.send('drag-end');
    pet.release();
  }
  down = null;
  dragging = false;
  vx = 0;
}

window.addEventListener('mouseleave', () => {
  // 拖得太快、鼠标在窗口外松开时，也要结束拖动
  if (down) endDrag();
  setIgnore(true);
});
window.addEventListener('blur', () => { if (down) endDrag(); });

window.addEventListener('mousedown', (e) => {
  if (e.button !== 0) return;
  down = { x: e.screenX, y: e.screenY };
  dragging = false;
});

window.addEventListener('mouseup', (e) => {
  if (e.button !== 0 || !down) return;
  if (dragging) endDrag();
  else {
    pet.poke();
    down = null;
  }
});

window.addEventListener('contextmenu', (e) => {
  e.preventDefault();
  api.send('menu');
});

// 外部拖放始终使用复制语义；普通按下并移动角色仍由上面的拖动处理。
for (const type of ['dragenter', 'dragover']) window.addEventListener(type, event => {
  event.preventDefault();
  event.dataTransfer.dropEffect = 'copy';
});
window.addEventListener('drop', async event => {
  event.preventDefault();
  const dt = event.dataTransfer;
  const payload = { files: [], text: dt.getData('text/plain'), links: [] };
  for (const line of dt.getData('text/uri-list').split(/\r?\n/)) {
    if (line && !line.startsWith('#')) payload.links.push(line);
  }
  try {
    for (const file of dt.files) {
      const local = api.filePath(file);
      if (local) payload.files.push(local);
      else if (file.type.startsWith('image/') && !payload.image) {
        if (file.size > 16 * 1024 * 1024) throw new Error('拖入的图片超过 16 MiB');
        payload.image = await new Promise((resolve, reject) => {
          const reader = new FileReader();
          reader.onload = () => resolve(reader.result.split(',')[1]);
          reader.onerror = () => reject(new Error('无法读取图片'));
          reader.readAsDataURL(file);
        });
      }
    }
    const result = await api.drop(payload);
    if (result.error) pet.say(result.error);
  } catch (e) { pet.say(e.message); }
});
