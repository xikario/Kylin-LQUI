// Run with npm run test:idle on X11 (or under xvfb-run).
const {app, BrowserWindow} = require('electron');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const root = path.resolve(__dirname, '..');
const roles = require('../renderer/characters');
const userData = fs.mkdtempSync(path.join(os.tmpdir(), 'lqui-idle-check-'));
app.setPath('userData', userData);

const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));
async function until(check, limit = 5000) {
  const deadline = Date.now() + limit;
  while (Date.now() < deadline) {
    if (await check()) return;
    await sleep(35);
  }
  throw new Error('Timed out: ' + check);
}
async function checkRole(role) {
  const win = new BrowserWindow({width:420,height:320,webPreferences:{preload:path.join(root,'preload.js')}});
  const read = js => win.webContents.executeJavaScript(js);
  try {
    await win.loadFile(path.join(root,'renderer/index.html'), {query:{character:role}});
    const cfg = {...require('../pet-config.json'),animations:roles[role].animations};
    await read('pet.setConfig(' + JSON.stringify(cfg) + ')');
    // Use the real whisper path. A temporary listening picture must release
    // even though the same clip is allowed to loop during an approval wait.
    await read("pet.cfg.animations.events.whisper=['A06']; pet.whisper(true)");
    assert.equal(await read('pet.current'),'A06');
    await until(() => read("pet.current==='A01' && pet._idleTimer!==null"));
    await read("pet.claudeEvent('needs-you')");
    await sleep(1750);
    assert.equal(await read('pet.current'),'A06');
    assert.equal(await read('pet._idleTimer'),null);
    await read("pet.claudeEvent('idle')");
    // A duplicate idle notification must not push an already armed deadline back.
    assert.ok(await read("(()=>{const t=pet._idleTimer;pet.claudeEvent('idle');return t!==null && pet._idleTimer===t})()"));

    const bag = await read(`(()=>{
      const clips=pet.cfg.animations.autoMotion.clips;
      return Array.from({length:2},()=>clips.map(()=>pet._nextIdleAction(clips)));
    })()`);
    assert.equal(bag[0].length,12);
    for (const round of bag) assert.equal(new Set(round).size,12);
    assert.notEqual(bag[0].at(-1),bag[1][0]);

    // Shorten only the test's wait. The scheduler itself must select a video,
    // begin advancing real frames, and schedule again after normal completion.
    await read("pet.cfg.animations.autoMotion.minMs=250;pet.cfg.animations.autoMotion.maxMs=350;pet._scheduleIdle()");
    const playing = () => read("Boolean(pet.clips[pet.current].lazyVideo && pet.videos[pet.current].currentTime>.08)");
    await until(playing);
    const first=await read('pet.current');
    await read("pet.videos[pet.current].currentTime=pet.videos[pet.current].duration-.15");
    await until(() => read("pet.current==='A01'"));
    await until(playing);
    assert.notEqual(await read('pet.current'),first);

    win.hide();
    await until(() => read("document.hidden && pet.current==='A01' && pet._idleTimer===null && !document.querySelector('video[src]')"));
    win.show();
    await until(playing);
    console.log(role + ': transient and held poses, auto playback, hidden pause and resume passed');
  } finally {
    win.destroy();
  }
}
app.on('window-all-closed', () => {});
app.whenReady().then(async () => {
  for (const role of ['gemini','gpt']) await checkRole(role);
}).then(()=>app.quit(), error=>{console.error(error);process.exitCode=1;app.quit();});
app.on('quit',()=>{fs.rmSync(userData,{recursive:true,force:true});});
