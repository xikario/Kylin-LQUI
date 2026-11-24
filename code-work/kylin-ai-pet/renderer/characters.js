(function (root) {
  const cast = {
    claude: { name: '克劳德大小姐' },
    fish: { name: '大肥鱼', files: ['idle.webm','think.webm','work.webm','ask.webm','happy.webm','oops.webm','react.webm'] },
    gpt: { name: 'GPT 白龙', files: ['idle.webp','thinking.webp','writing.webp','asking.webp','happy.webp','oops.webp','idle-blink.webp'] },
    gemini: { name: 'Gemini 星空猫', files: ['idle.webp','thinking.webp','writing.webp','asking.webp','happy.webp','oops.webp','happy.webp'] },
  };
  for (const role of Object.values(cast)) {
    if (!role.files) continue;
    role.clips = {};
    ['A01','A02','A03','A06','B04','A05','C06'].forEach((id, i) => {
      role.clips[id] = { pose: 'hand', loop: i < 4, file: role.files[i] };
    });
    role.names = { A01:'待机', A02:'思考', A03:'工作', A06:'倾听', B04:'开心', A05:'叹气', C06:'回应' };
    role.animations = { idle:['A01'], clicks:['C06','B04'], categories:[{id:'小动作',weight:25,actions:['B04','C06']}],
      events:{whisper:['A06','B04'],workStatus:['A02','A03','A06','A06','B04','A05']} };
  }
  if (typeof module === 'object') module.exports = cast;
  else root.petCharacters = cast;
})(globalThis);
