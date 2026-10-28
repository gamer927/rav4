#!/usr/bin/env python3
# SPDX-License-Identifier: CC-BY-NC-SA-4.0
# Copyright (c) 2026 gamer927. RAV4 TCU, https://github.com/gamer927/rav4
"""Renders the screen snapshots from `make screens` (test/system) into one
picture. Usage: python3 tools/render_screens.py build/screens docs/oled-screens.png
"""
from PIL import Image, ImageDraw, ImageFont
import sys
S=sys.argv[1].rstrip('/')+'/'
OUT=sys.argv[2]
def load(n):
    t=open(S+n+'.pbm').read().split()
    w,h=int(t[1]),int(t[2]); px=t[3:]
    im=Image.new('RGB',(w,h),(0,0,0))
    for i,v in enumerate(px):
        if v=='1': im.putpixel((i%w,i//w),(235,245,255))
    return im
shots=[('1_main_parked','Parked, ON/OFF off: stock shifting'),('8_main_driving_sport','Driving in SPORT, 4th gear'),
       ('9_main_corner_hold','Cornering: holding the gear'),('11_mode_error','ECO and SPORT both on: error, stock'),
       ('2_sensors','SENSORS page (tap TEST)'),('6_ibit_prompt','Self-test asking you to do something'),
       ('7_ibit_done_with_fault','Self-test result: a broken wire found'),('4_test_refused_engine_running','Self-test refused: engine running')]
sc=4; pad=24; cw=128*sc+2*pad; ch=64*sc+2*pad+40
out=Image.new('RGB',(cw*4, ch*2),(250,250,250)); d=ImageDraw.Draw(out)
f=ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf',18)
for i,(n,cap) in enumerate(shots):
    x=(i%4)*cw; y=(i//4)*ch
    d.rounded_rectangle([x+pad-10,y+pad-10,x+pad+128*sc+10,y+pad+64*sc+10],8,fill=(20,20,20))
    out.paste(load(n).resize((128*sc,64*sc),Image.NEAREST),(x+pad,y+pad))
    d.text((x+pad,y+pad+64*sc+16),cap,fill=(0,0,0),font=f)
out.save(OUT,optimize=True)
print('wrote', OUT)
