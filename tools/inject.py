#!/usr/bin/env python3
"""Inject libtool.so into any APK. Requires: apktool, zipalign, apksigner, java.
Usage: python3 inject.py app.apk --libs ./libs --out patched.apk
libs dir must contain arm64-v8a/libtool.so and/or armeabi-v7a/libtool.so"""
import argparse, os, re, shutil, subprocess, sys, tempfile, glob

def run(cmd): subprocess.check_call(cmd)

def main():
  ap = argparse.ArgumentParser()
  ap.add_argument('apk'); ap.add_argument('--libs', required=True)
  ap.add_argument('--out', default='patched.apk')
  ap.add_argument('--ks', default=os.path.expanduser('~/.android/debug.keystore'))
  ap.add_argument('--ks-pass', default='android')
  a = ap.parse_args()
  work = tempfile.mkdtemp(prefix='toolinject_')
  dec = os.path.join(work, 'dec')
  run(['apktool', 'd', '-f', '-o', dec, a.apk])
  # find launcher activity
  man = open(os.path.join(dec, 'AndroidManifest.xml'), encoding='utf-8').read()
  m = re.search(r'<activity[^>]*android:name="([^"]+)"[^>]*>(?:(?!</activity>).)*?LAUNCHER', man, re.S)
  if not m:
    m2 = re.search(r'<activity[^>]*android:name="([^"]+)"', man)
    if not m2: sys.exit('no activity found')
    name = m2.group(1)
  else: name = m.group(1)
  pkg = re.search(r'package="([^"]+)"', man).group(1)
  if name.startswith('.'): name = pkg + name
  smali = os.path.join(dec, 'smali*', *[])
  cands = glob.glob(os.path.join(dec, 'smali*', name.replace('.', os.sep) + '.smali'))
  if not cands: sys.exit('activity smali not found: ' + name)
  path = cands[0]
  src = open(path, encoding='utf-8').read()
  inject = ('    const-string v0, "tool"\n'
            '    invoke-static {v0}, Ljava/lang/System;->loadLibrary(Ljava/lang/String;)V\n')
  om = re.search(r'(\.method[^\n]*onCreate\(Landroid/os/Bundle;\)V.*?\.locals (\d+))', src, re.S)
  if om:
    locals_n = int(om.group(2))
    if locals_n < 1:
      src = src.replace(om.group(1), om.group(1).replace('.locals 0', '.locals 1'), 1)
    idx = src.index(om.group(1)) + len(om.group(1))
    src = src[:idx] + '\n' + inject + src[idx:]
  else:
    src += ('\n.method public onCreate(Landroid/os/Bundle;)V\n    .locals 1\n'
            + inject +
            '    invoke-super {p0, p1}, Landroid/app/Activity;->onCreate(Landroid/os/Bundle;)V\n'
            '    return-void\n.end method\n')
  open(path, 'w', encoding='utf-8').write(src)
  for abi in ('arm64-v8a', 'armeabi-v7a'):
    so = os.path.join(a.libs, abi, 'libtool.so')
    if os.path.exists(so):
      dst = os.path.join(dec, 'lib', abi); os.makedirs(dst, exist_ok=True)
      shutil.copy(so, dst)
  built = os.path.join(work, 'built.apk')
  run(['apktool', 'b', '-f', '-o', built, dec])
  aligned = os.path.join(work, 'aligned.apk')
  run(['zipalign', '-f', '4', built, aligned])
  if not os.path.exists(a.ks):
    run(['keytool', '-genkeypair', '-keystore', a.ks, '-alias', 'k', '-keyalg', 'RSA',
         '-keysize', '2048', '-validity', '10000', '-storepass', a.ks_pass,
         '-keypass', a.ks_pass, '-dname', 'CN=tool'])
  run(['apksigner', 'sign', '--ks', a.ks, '--ks-pass', 'pass:' + a.ks_pass,
       '--out', a.out, aligned])
  print('OK ->', a.out)

if __name__ == '__main__': main()
