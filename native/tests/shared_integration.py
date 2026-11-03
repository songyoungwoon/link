"""Real sockets against a disposable shared-state fixture and optional sibling server.

Bearer credentials stay inside test processes and are never written to reports.
The fixture is an executable contract test, not an RPG or headset simulation.
"""
from __future__ import annotations
import argparse
import copy
import json
from pathlib import Path
import socket
import socketserver
import struct
import subprocess
import sys
import tempfile
import threading
import time

HEADER=struct.Struct('!4sBBBBII')
SECRET='private-fixture-bearer-do-not-log'

def player(slot, active=False, identity='', name=''):
    return dict(id=identity,slot=slot,active=active,name=name,position=[0,1.65,28],yaw=0,hp=100 if active else 0,maxHp=100,level=1,xp=0,gold=0,weapon='starter',kills=0,quest=0,attack=0,respawn=0)

class Fixture(socketserver.ThreadingTCPServer):
    allow_reuse_address=True
    daemon_threads=True
    block_on_close=False
    def __init__(self,mode='normal'):
        self.mode=mode; self.lock=threading.Lock(); self.players={}; self.live={}; self.actions=[]; self.steps=[]; self.failures=[]; self.tick=0
        super().__init__(('127.0.0.1',0),Handler)
    def snapshot(self,identity):
        self.tick+=1
        players=[player(i) for i in range(8)]
        for token,(slot,record) in self.live.items():
            players[slot]=copy.deepcopy(record)
        monsters=[]
        for index,kind in enumerate(['slime']*4+['wolf']*2+['golem','warden']):
            suffix=index if kind=='slime' else index-4 if kind=='wolf' else 0
            monsters.append(dict(id=f'{kind}_{suffix}',active=True,position=[0,0,-10],yaw=0,hp=20,maxHp=20,kind=kind,attack=0,respawnIn=0))
        return dict(tick=self.tick,time=time.time(),self=next(copy.deepcopy(p) for p in players if p['id']==identity),players=players,monsters=monsters,notice='\ud55c\uae00 notice \U0001f30d')

class Handler(socketserver.BaseRequestHandler):
    def exact(self,count):
        result=b''
        while len(result)<count:
            block=self.request.recv(count-len(result))
            if not block: raise EOFError
            result+=block
        return result
    def receive(self):
        magic,major,minor,kind,flags,request,size=HEADER.unpack(self.exact(16))
        if (magic,major,minor,flags)!=(b'LINK',0,1,0) or size>65536: raise AssertionError('bad client header')
        body=self.exact(size)
        return kind,request,json.loads(body) if body else None
    def send(self,kind,request,value,**changes):
        body=json.dumps(value,ensure_ascii=True,separators=(',',':'),allow_nan=False).encode()
        header=HEADER.pack(changes.get('magic',b'LINK'),changes.get('major',0),changes.get('minor',1),kind,changes.get('flags',0),changes.get('id',request),changes.get('size',len(body)))
        packet=header+body
        if self.server.mode=='fragmented':
            for offset in range(0,len(packet),7): self.request.sendall(packet[offset:offset+7])
        else: self.request.sendall(packet)
    def handle(self):
        server=self.server; token=None
        try:
            self.request.settimeout(5)
            kind,request,joined=self.receive()
            if (kind,request)!=(16,0) or set(joined)!={'version','world','token','name'}: raise AssertionError('bad client join')
            if server.mode=='silent': time.sleep(.4); return
            if server.mode=='fail-reflection': self.send(21,0,{'message':SECRET}); return
            with server.lock:
                token=joined['token'] or SECRET+str(len(server.players))
                if token not in server.players:
                    server.players[token]=player(0,True,'p'+str(len(server.players)),joined['name'])
                record=copy.deepcopy(server.players[token]); slots={slot for slot,_ in server.live.values()}; slot=next(i for i in range(8) if i not in slots); record['slot']=slot
                server.live[token]=(slot,record)
                state=server.snapshot(record['id'])
            response=dict(token=token,playerId=record['id'],snapshot=state)
            if server.mode=='oversize': self.send(17,0,response,size=65537); return
            if server.mode=='wrong-id': self.send(17,0,response,id=9); return
            if server.mode=='wrong-version': self.send(17,0,response,minor=2); return
            if server.mode=='wrong-flags': self.send(17,0,response,flags=1); return
            if server.mode=='wrong-type': self.send(19,0,response); return
            if server.mode=='missing-field': del state['self']['hp']
            if server.mode=='token-in-snapshot': state['token']=SECRET
            if server.mode=='mismatched-self': state['self']['id']='other-player'
            if server.mode=='duplicate-key':
                body=b'{"token":"'+token.encode()+b'","token":"duplicate","playerId":"p0","snapshot":{}}'
                self.request.sendall(HEADER.pack(b'LINK',0,1,17,0,0,len(body))+body); return
            if server.mode=='unquoted-key':
                body=b'{token:"invalid"}'; self.request.sendall(HEADER.pack(b'LINK',0,1,17,0,0,len(body))+body); return
            if server.mode=='trailing-comma':
                body=b'{"token":"invalid",}'; self.request.sendall(HEADER.pack(b'LINK',0,1,17,0,0,len(body))+body); return
            if server.mode=='nan-number':
                body=json.dumps(response).replace('"tick": 1','"tick": NaN').encode();self.request.sendall(HEADER.pack(b'LINK',0,1,17,0,0,len(body))+body);return
            self.send(17,0,response)
            expected=1
            while True:
                kind,request,body=self.receive()
                if kind==20:
                    if body is not None: raise AssertionError('LEAVE must be empty')
                    break
                if (kind,request)!=(18,expected) or set(body)!={'position','yaw','action'}: raise AssertionError('bad client STEP')
                with server.lock:
                    server.steps.append(time.monotonic())
                    slot,record=server.live[token];record['position']=body['position'];record['yaw']=body['yaw']
                    if body['action'] is not None:
                        action=body['action']
                        if set(action)!={'kind','target','seq'} or not isinstance(action['seq'],int) or action['seq']<=0: raise AssertionError('bad action')
                        server.actions.append(action)
                    state=server.snapshot(record['id'])
                self.send(19,request,state);expected+=1
        except (ConnectionError,EOFError,OSError):
            pass
        except Exception as error:
            with server.lock: server.failures.append(type(error).__name__)
        finally:
            if token:
                with server.lock: server.live.pop(token,None)

def run_probe(probe,address,*args):
    result=subprocess.run([str(probe),address,*map(str,args)],stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=18)
    text=result.stdout.decode('utf-8','replace')
    if SECRET in text or SECRET in result.stderr.decode('utf-8','replace'): raise AssertionError('Credential leaked by probe')
    if result.returncode: raise AssertionError(f'Probe failed with exit {result.returncode}; payload output withheld')
    report=json.loads(text)
    if report.get('passed') is not True: raise AssertionError('Probe failed')
    return report

def fixture_case(probe,mode,*args):
    server=Fixture(mode); thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
    try:
        report=run_probe(probe,f'link://127.0.0.1:{server.server_address[1]}/sao',*args)
        if server.failures: raise AssertionError('Fixture rejected native client: '+','.join(server.failures))
        return report,server
    finally:
        server.shutdown();server.server_close();thread.join(timeout=2)

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--probe',type=Path,required=True);parser.add_argument('--server',type=Path);options=parser.parse_args();probe=options.probe.resolve()
    report,server=fixture_case(probe,'normal','--two-players','--resume-test','--action','rest','rest_shrine','--name','\ud55c\uae00 \ud0d0\ud5d8\uac00')
    assert report['maximumPlayers']==2 and report['resumed'] and report['actionQueued']
    assert report['unicodeNotice'] is True and server.actions
    assert all(action['kind']=='rest' and action['target']=='rest_shrine' for action in server.actions)
    report,_=fixture_case(probe,'fragmented');assert report['snapshots']>=2
    report,_=fixture_case(probe,'normal','--queue-stress');assert report['queued']==32
    report,_=fixture_case(probe,'silent','--stop-after-ms','40');assert report['stopMs']<500
    for mode in ('oversize','wrong-id','wrong-version','wrong-flags','wrong-type','missing-field','token-in-snapshot','mismatched-self','duplicate-key','unquoted-key','trailing-comma','nan-number','fail-reflection'):
        report,_=fixture_case(probe,mode,'--expect-error');assert report['rejected']
    if options.server:
        script=options.server.resolve()
        with tempfile.TemporaryDirectory(prefix='link-shared-rpg-') as temporary:
            with socket.socket() as reservation:
                reservation.bind(('127.0.0.1',0));port=reservation.getsockname()[1]
            process=subprocess.Popen([sys.executable,str(script),'--host','127.0.0.1','--port',str(port),'--database',str(Path(temporary)/'characters.sqlite3')],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
            try:
                deadline=time.monotonic()+12
                while time.monotonic()<deadline:
                    if process.poll() is not None: raise AssertionError('Disposable RPG server exited during startup')
                    try:
                        with socket.create_connection(('127.0.0.1',port),timeout=.15): break
                    except OSError: time.sleep(.05)
                else: raise AssertionError('Disposable RPG server startup timed out')
                report=run_probe(probe,f'link://127.0.0.1:{port}/sao','--two-players','--resume-test','--action','rest','rest_shrine','--duration-ms','800')
                assert report['maximumPlayers']==2 and report['resumed']
            finally:
                process.terminate()
                try: process.wait(timeout=5)
                except subprocess.TimeoutExpired: process.kill();process.wait(timeout=5)
    print('PASS shared LINK: strict bounded frames/JSON, Unicode, two players, action queue, resume, fragmentation, cancellation, secret-safe rejection'+(', real RPG server' if options.server else ''))

if __name__=='__main__': main()
