(function (root) {
  const cast = {
    claude: { name: '克劳德大小姐' },
    fish: { name: '大肥鱼', files: ['idle.webm','think.webm','work.webm','ask.webm','happy.webm','oops.webm','react.webm'] },
    gpt: { name: 'GPT 白龙', files: ['idle.webp','thinking.webp','writing.webp','asking.webp','happy.webp','oops.webp','idle-blink.webp'] },
    gemini: { name: 'Gemini 星空猫', files: ['idle.webp','thinking.webp','writing.webp','asking.webp','happy.webp','oops.webp','happy.webp'] },
  };
  const packs = typeof module === 'object' ? require('./motion-packs') : root.petMotionPacks;
  for (const [roleId, role] of Object.entries(cast)) {
    if (!role.files) continue;
    role.clips = {};
    ['A01','A02','A03','A06','B04','A05','C06'].forEach((id, i) => {
      role.clips[id] = { pose: 'hand', loop: i < 4, file: role.files[i] };
    });
    role.names = { A01:'待机', A02:'思考', A03:'工作', A06:'倾听', B04:'开心', A05:'叹气', C06:'回应' };
    role.animations = { idle:['A01'], clicks:['C06','B04'], categories:[{id:'小动作',weight:25,actions:['B04','C06']}],
      events:{whisper:['A06','B04'],workStatus:['A02','A03','A06','A06','B04','A05']} };

    const pack = packs[roleId];
    if (!pack) continue;
    const actions = role.animations;
    for (const [category, entries] of Object.entries(pack.groups)) {
      const ids = entries.map(([id, bounds, frames, fps]) => {
        const file = 'actions/' + id + '.webm';
        role.files.push(file);
        role.clips[id] = {pose:'hand', file, lazyVideo:true, bounds, frames, fps};
        role.names[id] = id.replace(/^(点击回应|工作状态)-/, '');
        return id;
      });
      if (category === '互动') {
        actions.clicks = [...ids, ...actions.clicks];
      } else if (category === '工作') {
        const prefix = roleId === 'gemini' ? '工作状态-' : '';
        actions.events.workStatus = [prefix+'思考冒泡',prefix+'忙碌点按','A06','A06',prefix+'雀跃庆祝','A05'];
      } else {
        actions.categories.push({id:category, weight:30, actions:ids});
      }
    }
    actions.autoMotion = {
      minMs: 12000,
      maxMs: 20000,
      clips: actions.categories.flatMap(category => category.actions).filter(id => role.clips[id].lazyVideo),
    };
    role.stillPlacement = pack.stillPlacement;
  }
  if (typeof module === 'object') module.exports = cast;
  else root.petCharacters = cast;
})(globalThis);
