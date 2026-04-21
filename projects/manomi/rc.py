import os,re,json,gzip,shutil,sqlite3,time,sys

R='raw_motifs.txt'
H='index.html'
D='dist'
DB='motifs.sqlite'
GZ=os.path.join(D,'motifs.sqlite.gz')
OK=set("ABCDEFGHJKLMNPQRSTUVWXZ")
TOC=[("A","Mythological Motifs"),("B","Animal Motifs"),("C","Motifs of Tabu"),("D","Magic"),("E","The Dead"),("F","Marvels"),("G","Ogres"),("H","Tests"),("J","The Wise and the Foolish"),("K","Deceptions"),("L","Reversals of Fortune"),("M","Ordaining the Future"),("N","Chance and Fate"),("P","Society"),("Q","Rewards and Punishments"),("R","Captives and Fugitives"),("S","Unnatural Cruelty"),("T","Sex"),("U","The Nature of Life"),("V","Religion"),("W","Traits of Character"),("X","Humor"),("Z","Miscellaneous Groups of Motifs")]
CP=re.compile(r'^([A-Z](?:[0-9Ol]+(?:[-—][A-Z]?[0-9Ol]+)?)?(?:\.[0-9Ol]+)*)\.\s+')
XR=re.compile(r'\(\s*(?i:cf\.?|see)\s+([^)]+)\)')

def fxc(s): return s.replace('O','0').replace('l','1')

def par(c):
    if '-' in c or '—' in c: return c[0] if c else None
    if '.' in c: return c.rsplit('.',1)[0]
    if len(c)>1: return c[0]
    return None if c in OK else "UNKNOWN"

def blocks(fn):
    with open(fn,'r',encoding='utf-8') as f: ls=f.readlines()
    out,b=[],[]
    for ln in ls:
        ln=ln.strip()
        if not ln or ln.startswith('[[[...]]]'): continue
        m=CP.match(ln)
        if m:
            rc=m.group(1)
            if any(c.isdigit() or c in 'Ol' for c in rc):
                if b: out.append(" ".join(b))
                c=fxc(rc)
                ln=ln[m.end():].strip()
                m2=CP.match(ln)
                if m2 and fxc(m2.group(1))==c: ln=ln[m2.end():].strip()
                b=[f"{c} {ln}"]
                continue
        if b: b.append(ln)
    if b: out.append(" ".join(b))
    return out

def core(bs):
    out=[]
    for blk in bs:
        p=blk.split(' ',1)
        if len(p)<2: continue
        c=p[0].rstrip('.')
        pc=par(c)
        if pc=="UNKNOWN": continue
        t=p[1]
        if '—' in t: body,tail=t.split('—',1)
        else:
            i=t.find(':')
            if i!=-1:
                j=t.rfind('. ',0,i)
                body,tail=(t[:j],t[j+2:]) if j!=-1 else (t,"")
            else: body,tail=t,""
        body=body.strip().rstrip('.')
        if '. ' in body: title,desc=body.split('. ',1)
        else: title,desc=body,None
        out.append({"code":c,"parent_code":pc,"title":title.strip(),"description":desc.strip() if desc else None,"unparsed_tail":tail.strip(),"raw_text":blk})
    return out

def xrf(data):
    for r in data:
        r['crossrefs']=[]
        def ex(txt):
            if not txt: return txt
            ms=XR.findall(txt)
            for m in ms:
                for c in [x.strip(' .') for x in m.split(',')]:
                    c=c.replace('ff','').strip()
                    if c: r['crossrefs'].append({"from_code":r["code"],"to_code":c,"relation":"cf"})
            return re.sub(r'\s+',' ',XR.sub('',txt)).strip(' .')
        r['title']=ex(r.get('title'))
        r['description']=ex(r.get('description'))
        r['unparsed_tail']=ex(r.get('unparsed_tail'))
    return data

def att(data):
    for r in data:
        r['attestations']=[]
        tail=r.pop('unparsed_tail','')
        if not tail: continue
        for cl in [c.strip() for c in re.split(r'[;—]',tail) if c.strip()]:
            m=re.match(r'^([^:]+):\s*(.*)$',cl)
            culture,citation=(m.group(1).strip(),m.group(2).strip()) if m else ("General",cl)
            if citation: r['attestations'].append({"culture":culture,"raw_citation":citation})
    return data

def pack(data):
    motifs,atts,xrs=[],[],[]
    for c,t in TOC:
        motifs.append({"code":c,"parent_code":None,"title":t,"description":None,"raw_text":f"{c}. {t}"})
    for r in data:
        c=r['code']
        if len(c)==1 and c.isalpha(): continue
        motifs.append({"code":c,"parent_code":r['parent_code'],"title":r.get('title'),"description":r.get('description'),"raw_text":r.get('raw_text')})
        for a in r.get('attestations',[]): atts.append({"motif_code":c,"culture":a['culture'],"raw_citation":a['raw_citation']})
        for x in r.get('crossrefs',[]): xrs.append({"from_code":x['from_code'],"to_code":x['to_code'],"relation":x['relation']})
    u={}
    for m in motifs:
        c=m['code']
        if c not in u or len(m.get('raw_text',''))>len(u[c].get('raw_text','')): u[c]=m
    return list(u.values()),atts,xrs

def mkdb(db,motifs,atts,xrs):
    if os.path.exists(db): os.remove(db)
    cn=sqlite3.connect(db)
    cu=cn.cursor()
    cu.execute("PRAGMA foreign_keys=ON;")
    cu.executescript("""
    CREATE TABLE motifs(code TEXT PRIMARY KEY,parent_code TEXT,title TEXT,description TEXT,raw_text TEXT);
    CREATE TABLE attestations(id INTEGER PRIMARY KEY AUTOINCREMENT,motif_code TEXT,culture TEXT,raw_citation TEXT,FOREIGN KEY(motif_code) REFERENCES motifs(code) ON DELETE CASCADE);
    CREATE TABLE crossrefs(id INTEGER PRIMARY KEY AUTOINCREMENT,from_code TEXT,to_code TEXT,relation TEXT,FOREIGN KEY(from_code) REFERENCES motifs(code) ON DELETE CASCADE);
    """)
    cu.executemany("INSERT OR REPLACE INTO motifs(code,parent_code,title,description,raw_text) VALUES(?,?,?,?,?)",[(m['code'],m['parent_code'],m['title'],m['description'],m['raw_text']) for m in motifs])
    cu.executemany("INSERT INTO attestations(motif_code,culture,raw_citation) VALUES(?,?,?)",[(a['motif_code'],a['culture'],a['raw_citation']) for a in atts])
    cu.executemany("INSERT INTO crossrefs(from_code,to_code,relation) VALUES(?,?,?)",[(x['from_code'],x['to_code'],x['relation']) for x in xrs])
    cu.execute("CREATE VIRTUAL TABLE motifs_fts USING fts4(code,title,description,raw_text,tokenize=porter);")
    cu.execute("INSERT INTO motifs_fts(code,title,description,raw_text) SELECT code,title,description,raw_text FROM motifs;")
    cn.commit()
    cu.execute("ANALYZE;")
    cn.commit()
    cu.execute("VACUUM;")
    cn.close()

def dist():
    if os.path.exists(D): shutil.rmtree(D)
    os.makedirs(D)
    for f in (H,DB):
        if not os.path.exists(f):
            print(f"Missing: {f}")
            sys.exit(1)
        shutil.copy2(f,os.path.join(D,os.path.basename(f)))
    src=os.path.join(D,DB)
    with open(src,'rb') as fi,gzip.open(GZ,'wb',compresslevel=9) as fo: shutil.copyfileobj(fi,fo)

def main():
    t=time.time()
    if not os.path.exists(R):
        print(f"Missing input: {R}")
        sys.exit(1)
    if not os.path.exists(H):
        print(f"Missing site file: {H}")
        sys.exit(1)
    b=blocks(R)
    d=att(xrf(core(b)))
    motifs,atts,xrs=pack(d)
    mkdb(DB,motifs,atts,xrs)
    dist()
    print(f"Blocks: {len(b)}")
    print(f"Motifs: {len(motifs)}")
    print(f"Attestations: {len(atts)}")
    print(f"Crossrefs: {len(xrs)}")
    print(f"Done in {time.time()-t:.2f}s")
    print(f"Output: {D}/")
    print(f"Compressed DB: {GZ}")

if __name__=="__main__": main()