"""Codex: пакет патча загрузчика после положительного и отрицательного контролей."""
from pathlib import Path
import hashlib,json,shutil,zipfile
R=Path(__file__).resolve().parents[1];P=R/'patches/slot0_autounload';E=Path('E:/zx/cache/tmp/hmm2_esp/slot0_autounload');D=R/'dist/HMM2_slot0_autounload_patch'
def main():
    assert not D.exists();cases=[]
    for name in ['joint_reset_result','joint_reset_before_poll_result','joint_reset_loading_result']:
        r=json.loads((E/(name+'.json')).read_text());first=r['first'];life=first['lifecycle']
        assert r['auto_unload'] and life['loaded']==0 and life['done']==1 and life['state']==3 and life['stops']==1
        assert r['application_unloads']==2 and r['esp_transport_restarts']==0 and first['reset_stop_s']<3
        cases.append(dict(case=name,stop_s=first['reset_stop_s'],module_unloaded=True,restart=True))
    stock=json.loads((E/'stock_reset_result.json').read_text());assert not stock['auto_unload'] and stock['first']['lifecycle']['loaded']==1
    compilation=json.loads((E/'compile_result.json').read_text());assert len(compilation)==2 and all(x['exit']==0 for x in compilation)
    qemu=json.loads((E/'qemu_results.json').read_text());assert len(qemu)==2 and all(x['exit']==0 and x['loading_scope_entered'] and x['no_status_query'] for x in qemu)
    for name in ['map','save']:
        text=(E/('qemu_'+name+'.log')).read_text(encoding='utf-8');assert 'exit verified no_status_query=1' in text
    assert 'all cases passed' in (E/'guard_result.txt').read_text()
    D.mkdir();shutil.copytree(P,D/'patches/slot0_autounload')
    for name in ['apply_slot0_patch.py','prepare_slot0_patch.py','compile_slot0_patch.py']:
        (D/'tools').mkdir(exist_ok=True);shutil.copy2(R/'tools'/name,D/'tools'/name)
    (D/'evidence').mkdir()
    for name in ['compile_result.json','guard_result.txt','joint_reset_result.json','joint_reset_before_poll_result.json','joint_reset_loading_result.json','stock_reset_result.json','qemu_results.json','qemu_map.log','qemu_save.log']:
        shutil.copy2(E/name,D/'evidence'/name)
    shutil.copy2(R/'licenses/VDAC4-MIT.txt',D/'LICENSE')
    result=dict(abi_unchanged=True,firmware_sources_unchanged=True,board_flashed=False,joint=cases,qemu=qemu,compile=compilation,
        elf_sha256=hashlib.sha256(Path('E:/zx/cache/tmp/hmm2_esp/stage1/module/HMM2.ELF').read_bytes()).hexdigest())
    (D/'verification.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
    (D/'README.md').write_text('''# Полная выгрузка ELF при M_SEL=0 и slot0

Это отдельный патч загрузчика ESP. Исходная прошивка, плата, Tang, BL616 и ABI
не изменены. Для применения к исходникам нужно разрешение: исходное задание
HMM2 ESP прямо запрещает менять прошивку ESP (Docs/codex_task_fast_frame.md:68).

В ELF уже работает остановка по slot0: отменяется I/O, закрывается видео,
завершается задача и C++ runtime. Текущий ModuleApp принимает app_done только
из command; когда Z80 ушёл, освобождение ELF ожидает новую команду.

Патч добавляет проверку в основной цикл владельца канала. После успешного
ft_open и наблюдения выбранного FT812 сторож опрашивает LINK раз в 100 мс.
Подтверждённые VDAC4/protocol 1, slot0 и M_SEL=0 запускают штатный stopApp:
stop приложения, завершение задач, удаление синхронизации, freeAllocations,
выгрузка модулей и ELF. appLock исключает выгрузку при выполнении poll.
Проверка выполняется до VFS и не требует команды, ответа или статуса от Z80.
Обычный core_back при M_SEL=1 не прерывает завершающий протокол SPG.
Ошибка LINK, неверная сигнатура и запуск до выбора FT812 не означают выход.
Новой задачи, дополнительного стека и новых полей ABI нет.

Изменяются три файла прошивки и добавляется ft_detach_guard.hpp. До изменения
apply_slot0_patch.py сверяет SHA256 исходных файлов, затем сохраняет резервную
копию на E:. По умолчанию скрипт только проверяет; --apply изменяет исходники,
но ничего не собирает, не устанавливает и не прошивает.

Проверки: обе изменённые единицы скомпилированы настоящим Xtensa-компилятором
с флагами и заголовками прошивки. На совместном стенде: из игры, до фонового
poll и при загрузке карты. Модуль выгружен, отслеживаемые ресурсы равны нулю,
повторный запуск работает без перезапуска ESP. Времена в модели указаны в verification.json.
Отрицательный контроль с исходным загрузчиком оставляет loaded=1.
Настоящий Xtensa ELF в QEMU завершает задачу при обрыве загрузки карты и SAV;
после перехода Z80 не исполняется и запрос статуса ему не посылается.
Это проверки модели, не замеры освобождения памяти на физической плате.
''',encoding='utf-8')
    sums={p.relative_to(D).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in D.rglob('*') if p.is_file()}
    (D/'SHA256SUMS.txt').write_text(''.join(v+'  '+k+'\n' for k,v in sorted(sums.items())),encoding='ascii')
    z=R/'dist/HMM2_slot0_autounload_patch.zip'
    with zipfile.ZipFile(z,'w',zipfile.ZIP_DEFLATED) as out:
        for p in D.rglob('*'):
            if p.is_file():out.write(p,p.relative_to(D))
    with zipfile.ZipFile(z) as out:assert out.testzip() is None
    print(json.dumps(dict(zip=str(z),checks=cases,bytes=z.stat().st_size),indent=2))
if __name__=='__main__':main()
