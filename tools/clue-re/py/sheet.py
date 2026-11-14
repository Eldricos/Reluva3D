# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

import sys,glob,os
from PIL import Image, ImageDraw
d,step,cols,rows,prefix=sys.argv[1],float(sys.argv[2]),4,4,sys.argv[3]
fs=sorted(glob.glob(d+'/f_*.png')); n=cols*rows
for k in range(0,len(fs),n):
    ims=[Image.open(f) for f in fs[k:k+n]]; w,h=ims[0].size
    sh=Image.new('RGB',(w*cols,h*rows))
    for i,im in enumerate(ims):
        x,y=(i%cols)*w,(i//cols)*h; sh.paste(im,(x,y))
        t=(k+i)*step; ImageDraw.Draw(sh).text((x+4,y+4),'%d:%02d'%(t//60,t%60),fill=(255,255,0))
    sh.save('%s_%02d.png'%(prefix,k//n))
