import os
GAME = os.environ.get('CLUE_GAME', 'game')  # the installed game: Clue.exe, the DLLs, Data/
WORK = os.environ.get('CLUE_WORK', '.')  # caches and outputs
import pickle,sys
base,secs,ins=pickle.load(open(os.path.join(WORK, 'eagle.pkl'),'rb'))
a=int(sys.argv[1],16); n=int(sys.argv[2])
k=next(i for i,(x,_,_) in enumerate(ins) if x>=a)
for x in ins[k:k+n]: print('   %08x %s %s'%x)
