#!/usr/bin/env python3
# Logging proxy: TCP 127.0.0.1:PORT -> Postgres unix socket. Logs every
# frontend message (type, and SQL text for Q/P) and backend message types.
import socket, struct, sys, threading, json
PORT=int(sys.argv[1]); LOG=sys.argv[2]; SOCK='/var/run/postgresql/.s.PGSQL.5432'
lock=threading.Lock(); conn_no=[0]
def log(rec):
    with lock:
        with open(LOG,'a') as f: f.write(json.dumps(rec)+'\n')
def cstr(b,i):
    j=b.index(b'\0',i); return b[i:j].decode('utf-8','replace'), j+1
def front(cs, ss, n):
    buf=b''; started=False
    def need(k):
        nonlocal buf
        while len(buf)<k:
            d=cs.recv(65536)
            if not d: raise EOFError
            buf+=d
    try:
        while True:
            if not started:
                need(4); L=struct.unpack('!I',buf[:4])[0]; need(L); m=buf[:L]; buf=buf[L:]
                code=struct.unpack('!I',m[4:8])[0]
                if code==80877103: log({'c':n,'dir':'F','t':'SSLRequest'})
                elif code==80877104: log({'c':n,'dir':'F','t':'GSSENCRequest'})
                else:
                    kv=m[8:].split(b'\0'); p={kv[i].decode():kv[i+1].decode() for i in range(0,len(kv)-1,2) if kv[i]}
                    log({'c':n,'dir':'F','t':'Startup','v':code,'params':p}); started=True
                ss.sendall(m); continue
            need(5); t=chr(buf[0]); L=struct.unpack('!I',buf[1:5])[0]; need(1+L); m=buf[:1+L]; buf=buf[1+L:]
            body=m[5:]; rec={'c':n,'dir':'F','t':t}
            if t=='Q': rec['sql'],_=cstr(body,0)
            elif t=='P':
                name,i=cstr(body,0); sql,i=cstr(body,i); np=struct.unpack('!H',body[i:i+2])[0]
                rec.update(name=name,sql=sql,oids=list(struct.unpack('!%dI'%np,body[i+2:i+2+4*np])))
            elif t=='B':
                portal,i=cstr(body,0); stmt,i=cstr(body,i); rec.update(stmt=stmt)
                nf=struct.unpack('!H',body[i:i+2])[0]; fmts=struct.unpack('!%dH'%nf,body[i+2:i+2+2*nf]); i+=2+2*nf
                npar=struct.unpack('!H',body[i:i+2])[0]; i+=2; vals=[]
                for _ in range(npar):
                    l=struct.unpack('!i',body[i:i+4])[0]; i+=4
                    if l<0: vals.append(None)
                    else: vals.append(body[i:i+l].decode('utf-8','replace') if not any(fmts) else body[i:i+l].hex()); i+=l
                nr=struct.unpack('!H',body[i:i+2])[0]; rfm=struct.unpack('!%dH'%nr,body[i+2:i+2+2*nr])
                rec.update(param_formats=list(fmts),params=vals,result_formats=list(rfm))
            elif t=='D': rec['what']=chr(body[0]); rec['name'],_=cstr(body,1)
            elif t=='E': rec['portal'],i=cstr(body,0); rec['max']=struct.unpack('!I',body[i:i+4])[0]
            elif t=='p': rec['auth']=True
            elif t=='d': rec['bytes']=len(body)
            log(rec); ss.sendall(m)
    except (EOFError, OSError): pass
    finally:
        try: ss.shutdown(socket.SHUT_WR)
        except OSError: pass
def back(cs, ss, n):
    buf=b''; first=True
    try:
        while True:
            d=ss.recv(65536)
            if not d: break
            cs.sendall(d); buf+=d
            while True:
                if first and buf[:1] in (b'N',b'S',b'G') and len(buf)>=1 and not buf[1:2]:
                    log({'c':n,'dir':'B','t':'SSLAnswer','a':buf[:1].decode()}); buf=b''; break
                first=False
                if len(buf)<5: break
                t=chr(buf[0]); L=struct.unpack('!I',buf[1:5])[0]
                if len(buf)<1+L: break
                m=buf[:1+L]; buf=buf[1+L:]; rec={'c':n,'dir':'B','t':t}
                body=m[5:]
                if t=='R': rec['auth']=struct.unpack('!I',body[:4])[0]
                elif t=='S': a,i=cstr(body,0); b,_=cstr(body,i); rec['param']=[a,b]
                elif t=='E' or t=='N':
                    f={}; i=0
                    while body[i]: k=chr(body[i]); v,i=cstr(body,i+1); f[k]=v
                    rec['fields']={k:f[k] for k in 'SCM' if k in f}
                elif t=='T':
                    nf=struct.unpack('!H',body[:2])[0]; i=2; cols=[]
                    for _ in range(nf):
                        nm,i=cstr(body,i); tbl,att,oid,sz,mod,fmt=struct.unpack('!IHIhiH',body[i:i+18]); i+=18; cols.append([nm,oid,fmt])
                    rec['cols']=cols
                elif t=='C': rec['tag'],_=cstr(body,0)
                elif t=='D': continue
                log(rec)
    except OSError: pass
    finally:
        try: cs.shutdown(socket.SHUT_WR)
        except OSError: pass
ls=socket.socket(); ls.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1); ls.bind(('127.0.0.1',PORT)); ls.listen(64)
while True:
    cs,_=ls.accept(); ss=socket.socket(socket.AF_UNIX); ss.connect(SOCK)
    conn_no[0]+=1; n=conn_no[0]
    threading.Thread(target=front,args=(cs,ss,n),daemon=True).start()
    threading.Thread(target=back,args=(cs,ss,n),daemon=True).start()
