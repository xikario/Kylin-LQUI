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
for (const [id, role] of Object.entries(roles)) {
  checkFile('assets/pets', id, 'avatar.png');
  for (const file of role.files || []) checkFile('assets/pets', id, file);
  const {file,holdMs} = greetings[id];
  checkFile('assets/pets', id, file);
  const png = fs.readFileSync(path.join(root, 'assets/pets', id, file));
  assert.equal(png.subarray(1,4).toString(), 'PNG');
  assert.ok(holdMs.every(n => Number.isInteger(n) && n > 0));
  assert.equal(png.readUInt32BE(16), png.readUInt32BE(20) * holdMs.length);
}
console.log(clips.length + ' 段原作动画、四套角色素材与悬停帧检查通过。');
