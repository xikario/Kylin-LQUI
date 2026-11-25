'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const {execFileSync} = require('node:child_process');
const root = path.resolve(__dirname, '..');
const roles = require('../renderer/characters');
const readJson = name => JSON.parse(fs.readFileSync(path.join(root, name), 'utf8'));

function checkFile(...parts) {
  const file = path.join(root, ...parts);
  assert.ok(fs.statSync(file).size > 0, parts.join('/'));
  assert.ok(fs.realpathSync(file).startsWith(root + path.sep), '包外文件：' + file);
}

for (const js of ['main.js', 'preload.js', ...fs.readdirSync(path.join(root, 'renderer'))
    .filter(name => name.endsWith('.js')).map(name => 'renderer/' + name)]) {
  execFileSync(process.execPath, ['--check', path.join(root, js)]);
}
const context = {window:{}};
vm.runInNewContext(fs.readFileSync(path.join(root, 'renderer/pet.js'), 'utf8'), context);
const clips = Object.keys(context.window.Pet.CLIPS);
for (const name of clips) checkFile('clips', name + '.webm');
for (const name of Object.keys(readJson('pet-config.json').names)) assert.ok(clips.includes(name), name);
const greetings = readJson('assets/pets/greetings.json');
let movies = 0;
for (const [id, role] of Object.entries(roles)) {
  checkFile('assets/pets', id, 'avatar.png');
  for (const file of role.files || []) checkFile('assets/pets', id, file);
  if (role.stillPlacement) {
    const {idle, clicks, categories, events} = role.animations;
    const menuIds = [...idle, ...clicks, ...categories.flatMap(c => c.actions), ...events.whisper, ...events.workStatus.flat()];
    assert.ok(menuIds.every(k => role.clips[k]), '未知动作：' + id);
    for (const [key, clip] of Object.entries(role.clips).filter(([, c]) => c.lazyVideo)) {
      assert.ok(menuIds.includes(key), '菜单缺少动作：' + key);
      assert.ok(clip.frames > 1 && clip.fps > 0 && clip.file.endsWith('.webm'));
      const [l,t,r,b] = clip.bounds;
      assert.ok(l>=0 && t>=0 && l<r && t<b && r<=640 && b<=360);
      movies++;
    }
  }
  const {file,holdMs} = greetings[id];
  checkFile('assets/pets', id, file);
  const png = fs.readFileSync(path.join(root, 'assets/pets', id, file));
  assert.equal(png.subarray(1,4).toString(), 'PNG');
  assert.ok(holdMs.every(n => Number.isInteger(n) && n > 0));
  assert.equal(png.readUInt32BE(16), png.readUInt32BE(20) * holdMs.length);
}
console.log(clips.length + ' 段大小姐动画、' + movies + ' 段白龙和星空猫扩展动画、四套角色素材与悬停帧检查通过。');
