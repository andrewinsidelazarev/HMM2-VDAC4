"""Codex: локальный патч загрузчика; исходная прошивка и плата не изменяются."""
from pathlib import Path
import difflib,hashlib,json
R=Path(__file__).resolve().parents[1]
F=Path('E:/zx/Tang Nano 20k VDAC/VDAC4/ESP32 S3 Zero N8R8/Firmware')
P=R/'patches/slot0_autounload'
def main():
    names=['include/zifi/apps/module_app.hpp','src/apps/module_app.cpp','src/main.cpp'];diff=[];checks={}
    for name in names:
        source=F/name;old=source.read_text(encoding='utf-8');s=old
        if name.endswith('.hpp'):
            s=s.replace('#include "zifi/zm_cache.hpp"','#include "zifi/zm_cache.hpp"\n#include "zifi/ft_detach_guard.hpp"',1)
            s=s.replace('  void pollNetworkCore(uint32_t now) override;','  void pollNetworkCore(uint32_t now) override;\n  // Codex: владельцу канала разрешено выгрузить ELF, даже когда Z80 уже ушёл.\n  void pollCore1(uint32_t now);',1)
            s=s.replace('  bool ftOpened_ = false;','  bool ftOpened_ = false;\n  FtDetachGuard ftDetach_;',1)
        elif name.endswith('module_app.cpp'):
            a='void ModuleApp::pollNetworkCore(uint32_t now) {'
            b='''// Codex: только владелец канала; appLock_ в stopApp исключает одновременный poll ELF.
void ModuleApp::pollCore1(uint32_t now) {
#if ZIFI_HAS_SPI_LINK
  if (app_ == nullptr || !ftDetach_.due(now)) return;
  uint32_t state[3] = {};
  if (!tang::ready() || !tang::read(0, state, 3) ||
      !ftDetach_.observe(state[0], state[1], state[2])) return;
  bringupLog(kTag, "app %s: M_SEL=0, slot0, unloading", appName_);
  stopApp();
#else
  (void)now;
#endif
}

'''
            assert a in s;s=s.replace(a,b+a,1)
            a='  instance_->ftOpened_ = true;';assert a in s;s=s.replace(a,a+'\n  uint32_t ftState[3] = {};\n  const bool selected = tang::read(0, ftState, 3) && ftState[0] == 0x56444334u &&\n                        (ftState[1] >> 24) == 2 && (ftState[2] & (1u << 12));\n  instance_->ftDetach_.opened(selected);',1)
            # Codex: reset только после остановки задач: opened() больше не вызовется старым ELF.
            a='  freeAllocations();\n  unloadElf(appElf_);';assert a in s;s=s.replace(a,a+'\n  ftDetach_.reset();',1)
        else:
            a='void Application::pollLink() {';assert a in s
            s=s.replace(a,a+'\n#if ZIFI_HAS_AUDIO\n  // Codex: проверять потерю FT до VFS и без ожидания новой команды Z80.\n  moduleApp_.pollCore1(millis());\n#endif',1)
        assert old!=s
        before=P/'before'/name;after=P/'after'/name;before.parent.mkdir(parents=True,exist_ok=True);after.parent.mkdir(parents=True,exist_ok=True)
        before.write_text(old,encoding='utf-8');after.write_text(s,encoding='utf-8')
        checks[name]=dict(before=hashlib.sha256(source.read_bytes()).hexdigest(),after=hashlib.sha256(after.read_bytes()).hexdigest())
        diff.extend(difflib.unified_diff(old.splitlines(keepends=True),s.splitlines(keepends=True),fromfile='a/'+name,tofile='b/'+name))
    name='include/zifi/ft_detach_guard.hpp';s=(P/'after'/name).read_text(encoding='utf-8')
    diff.extend(difflib.unified_diff([],s.splitlines(keepends=True),fromfile='/dev/null',tofile='b/'+name))
    (P/'slot0_autounload.patch').write_text(''.join(diff),encoding='utf-8')
    (P/'sources.json').write_text(json.dumps(checks,indent=2),encoding='utf-8');print(P/'slot0_autounload.patch')
if __name__=='__main__':main()
