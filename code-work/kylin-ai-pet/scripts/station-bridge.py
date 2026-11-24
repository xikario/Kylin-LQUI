"""Pass a pet chat or an external drop to the user's running AI station."""
import json
import sys
from pathlib import Path
from gi.repository import Gio, GLib
import subprocess
import time

def call(bus, service, path, method, args, timeout):
    return bus.call_sync(service, path, service, method, args, None,
                         Gio.DBusCallFlags.NO_AUTO_START, timeout, None).unpack()[0]

def main():
    data = json.load(sys.stdin)
    bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)
    if sys.argv[1] == 'drop':
        shown = call(bus, 'org.ukui.AiStationPanel', '/AiStation', 'PetDrop',
                     GLib.Variant('(s)', (json.dumps(data, ensure_ascii=False),)), 10000)
        if not shown:
            raise ValueError('任务栏无法读取拖入的内容')
        return True
    raise ValueError('未知的小站操作')

try:
    print(json.dumps({'result':main()},ensure_ascii=False))
except (GLib.Error,OSError,ValueError,KeyError) as error:
    print(json.dumps({'error':str(error)},ensure_ascii=False))
