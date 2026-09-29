import ctypes as c
from pathlib import Path
k=c.WinDLL('kernel32',use_last_error=True)
k.LoadLibraryExW.argtypes=[c.c_wchar_p,c.c_void_p,c.c_uint];k.LoadLibraryExW.restype=c.c_void_p
k.FindResourceW.argtypes=[c.c_void_p,c.c_void_p,c.c_void_p];k.FindResourceW.restype=c.c_void_p
k.LoadResource.argtypes=[c.c_void_p,c.c_void_p];k.LoadResource.restype=c.c_void_p
k.LockResource.argtypes=[c.c_void_p];k.LockResource.restype=c.c_void_p
k.SizeofResource.argtypes=[c.c_void_p,c.c_void_p];k.SizeofResource.restype=c.c_uint
k.FreeLibrary.argtypes=[c.c_void_p]
h=k.LoadLibraryExW(str(Path('releases/ARGENT-Alpha-Test-r239/ArgentLauncher.exe').resolve()),None,2|32);assert h
try:
 for rid,name in {101:'04B_30__.TTF',102:'intro-radiation.mod',104:'Copper_35.png',105:'Razor1911_32.png',106:'038_32.png',107:'logo.png'}.items():
  r=k.FindResourceW(h,rid,10);assert r,name
  data=c.string_at(k.LockResource(k.LoadResource(h,r)),k.SizeofResource(h,r))
  assert data==Path('assets/intro',name).read_bytes(),name
 print('PASS: all six embedded intro resources match original assets')
finally:k.FreeLibrary(h)
