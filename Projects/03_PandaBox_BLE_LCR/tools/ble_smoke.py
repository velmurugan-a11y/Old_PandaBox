# BLE smoke test for the PandaBox (Windows / bleak): scan by name, connect, send commands, print replies,
# disconnect, check that the box advertises again.
# usage: python ble_smoke.py [cmd ...]      (default: BoxStatus BoxInfo RdPortLcrNode Start 1, GetData 1,0, Stop 1,)
import asyncio, time, sys
from bleak import BleakScanner, BleakClient
N="49535343-1e4d-4bd9-ba61-23c647249616"; W="49535343-8841-43f4-a8d4-ecbe34729bb3"
ADDR=None   # found by name
def ts(): return time.strftime("%H:%M:%S")
async def run():
    t=time.time(); got={}
    def cb(d,a):
        if d.name=="PandaBrainBLE" and 'dev' not in got: got['dev']=d
    async with BleakScanner(cb):
        while 'dev' not in got and time.time()-t<30: await asyncio.sleep(0.1)
    if 'dev' not in got: print(ts(),"not found"); return
    print(ts(),f"found {time.time()-t:.1f}s {got['dev'].address}"); t=time.time(); buf=[]
    c=BleakClient(got["dev"], winrt={"use_cached_services": False})
    try: await c.connect(timeout=40)
    except Exception as e: print(ts(),f"connect failed after {time.time()-t:.1f}s: {type(e).__name__} {e}"); return
    print(ts(),f"connected+services after {time.time()-t:.1f}s")
    await c.start_notify(N, lambda h,b: buf.append(b))
    for cmd in sys.argv[1:] or ["BoxStatus","BoxInfo","RdPortLcrNode","Start 1,","GetData 1,0,","Stop 1,"]:
        buf.clear(); await c.write_gatt_char(W,(cmd+"\r\n").encode(),response=False)
        await asyncio.sleep(1.5 if cmd.startswith("Start") else 0.8)
        print(f"{cmd!r:18} -> {b''.join(buf).decode(errors='replace').strip()!r}")
    await c.disconnect(); await asyncio.sleep(2)
    t=time.time(); d=await BleakScanner.find_device_by_name("PandaBrainBLE", timeout=30)
    print(ts(),f"re-advertising after disconnect: {d is not None} ({time.time()-t:.1f}s)")
asyncio.run(run())
