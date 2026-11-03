"""Private PulseAudio playback test. Requires libcanberra and a volume theme sound.

Run with dbus-run-session -- /usr/bin/python3 test_volume_playback.py.
Only virtual null-sink monitor audio is sampled; no hardware module is loaded.
"""
import os,sys,tempfile,time,subprocess,json,re,array,math
from pathlib import Path
source=Path(__file__).resolve().parents[1];helper=source/'volume_feedback.py';sys.path.insert(0,str(source))
from system_backend import SystemBackend

def main():
    results=[]
    for flat in ['no','yes']:
     with tempfile.TemporaryDirectory(prefix='lqui-feedback-') as folder:
      root=Path(folder)
      for name in ['runtime','state','config']:(root/name).mkdir(mode=0o700)
      (root/'config/pulse').mkdir();(root/'config/pulse/daemon.conf').write_text('flat-volumes = '+flat+'\n')
      socket=root/'native';config=root/'test.pa';config.write_text('load-module module-native-protocol-unix socket='+str(socket)+' auth-anonymous=1\nload-module module-stream-restore\nload-module module-null-sink sink_name=preview\nset-default-sink preview\n')
      env=dict(os.environ,PULSE_RUNTIME_PATH=str(root/'runtime'),PULSE_STATE_PATH=str(root/'state'),PULSE_SERVER='unix:'+str(socket),XDG_CONFIG_HOME=str(root/'config'),LC_ALL='C')
      server=subprocess.Popen(['pulseaudio','-n','--daemonize=no','--use-pid-file=no','--exit-idle-time=-1','--disable-shm=yes','--log-level=error','--file='+str(config)],env=env,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
      try:
       deadline=time.monotonic()+5
       while not socket.exists() and time.monotonic()<deadline:
        if server.poll() is not None:raise RuntimeError(server.stderr.read().decode())
        time.sleep(.02)
       def command(*args):return subprocess.check_output(['pactl',*args],env=env,text=True).strip()
       for volume in [25,60]:
        command('set-sink-volume','preview',str(volume)+'%')
        record=root/'audio.raw'
        with record.open('wb') as file:
         recorder=subprocess.Popen(['parec','--device=preview.monitor','--format=s16le','--rate=48000','--channels=2','--latency-msec=20','--process-time-msec=10'],env=env,stdout=file,stderr=subprocess.DEVNULL)
         try:
          time.sleep(.15);player=subprocess.Popen(['/usr/bin/python3',str(helper),'--play','preview'],env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
          seen=[];deadline=time.monotonic()+6
          while player.poll() is None and time.monotonic()<deadline:
           block=command('list','sinks');seen.extend(map(int,re.findall(r'(\d+)%',SystemBackend.pulse_field(block,'Volume'))));time.sleep(.03)
          if player.poll() is None:player.kill()
          out,err=player.communicate();assert player.returncode==0,(player.returncode,err)
          time.sleep(.15)
         finally:recorder.terminate();recorder.wait(timeout=2)
        data=array.array('h',record.read_bytes());rms=math.sqrt(sum(float(v)*v for v in data)/max(1,len(data)))
        results.append({'flatVolumes':flat,'volume':volume,'seenMin':min(seen),'seenMax':max(seen),'rms':round(rms,2),'played':rms>1})
        assert min(seen)==max(seen)==volume,results[-1]
        assert rms>1,results[-1]
       assert results[-1]['rms']>results[-2]['rms']*2,results[-2:]
      finally:server.terminate();server.wait(timeout=5)
    print(json.dumps(results));return results

if __name__=="__main__":main()
