"""Codex: добавление кампаний без перезаписи старых данных DAT; подготовка, проверка, применение и откат."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import shutil
import struct

MAGIC = 0x31465348
BLOCK = 1024 * 1024


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def region_digest(stream, start, length):
    stream.seek(start)
    result = hashlib.sha256()
    while length:
        data = stream.read(min(length, BLOCK))
        if not data:
            raise ValueError('Оборванный файл')
        result.update(data)
        length -= len(data)
    return result.hexdigest()


def catalog(path):
    size = path.stat().st_size
    with path.open('rb') as stream:
        header = stream.read(16)
        magic, version, count, pointer = struct.unpack('<4I', header)
        # Клод: FS2/FS4 — записи с 60-байтовым именем и префиксом заголовка карты (пакет на карте пользователя — FS2 с роликами).
        if magic != MAGIC or version not in (1, 2, 3, 4) or not 0 < count <= 2048:
            raise ValueError('Ожидается DAT FS1/FS2/FS3/FS4')
        at = pointer if version >= 3 else 16
        if at < 16 or at + count * 72 > size:
            raise ValueError('Каталог вне файла')
        stream.seek(at)
        table = stream.read(count * 72)
    entries = []
    names = set()
    for row in range(count):
        data = table[row * 72:(row + 1) * 72]
        width = 60 if version in (2, 4) else 64
        if b'\0' not in data[:width]:
            raise ValueError('Имя без терминатора')
        name = data[:width].split(b'\0')[0].decode('ascii')
        offset, length = struct.unpack_from('<II', data, 64)
        if name in names or offset < 16 or offset + length > size:
            raise ValueError('Повреждённая запись: ' + name)
        names.add(name)
        entries.append(dict(path=name, offset=offset, bytes=length))
    return header, table, entries


def prepare(base, maps, clips, out):
    header, old_table, entries = catalog(base)
    existing = {entry['path']: entry for entry in entries}
    files = [('maps/' + p.name.lower(), p) for p in sorted(maps.iterdir())
             if p.is_file() and p.suffix.lower() in ('.h2c', '.hxc')]
    if clips:
        files += [('video/' + p.name.lower(), p) for p in sorted(clips.iterdir())
                  if p.is_file() and p.suffix.lower() in ('.smk', '.ogg')]
    if not files:
        raise ValueError('Нет файлов кампаний')
    if any(name in existing for name, _ in files) or len({name for name, _ in files}) != len(files):
        raise ValueError('Записи уже существуют или имена повторяются')
    if len(entries) + len(files) > 2048:
        raise ValueError('Превышен предел записей движка')
    out.mkdir(parents=True, exist_ok=True)
    size = base.stat().st_size
    table = bytearray(old_table)
    fs2 = struct.unpack_from('<I', header, 4)[0] in (2, 4)
    added = []
    with (out / 'campaign.append').open('wb') as stream:
        at = size
        for name, path in files:
            alignment = 512 if name.startswith('video/') else 4
            aligned = (at + alignment - 1) & -alignment
            stream.write(bytes(aligned - at))
            length = path.stat().st_size
            if name.endswith('.smk'):
                with path.open('rb') as check:
                    if check.read(4) != b'HMV1':
                        raise ValueError('Требуется преобразованный HMV1: ' + str(path))
            encoded = name.encode('ascii')
            if len(encoded) >= (60 if fs2 else 64):
                raise ValueError('Слишком длинное имя')
            # Клод: у новой записи FS2 префикса нет (0) — движок читает заголовок карты из самого файла.
            if fs2:
                table += encoded.ljust(60, b'\0') + struct.pack('<III', 0, aligned, length)
            else:
                table += encoded.ljust(64, b'\0') + struct.pack('<II', aligned, length)
            with path.open('rb') as source:
                shutil.copyfileobj(source, stream, BLOCK)
            added.append(dict(path=name, offset=aligned, bytes=length, sha256=digest(path)))
            at = aligned + length
        count = len(entries) + len(added)
        first = min(e['offset'] for e in entries)
        if struct.unpack_from('<I', header, 4)[0] == 1 and 16 + len(table) <= first:
            prefix = struct.pack('<4I', MAGIC, 1, count, 0) + table
            version, pointer = 1, 16
        else:
            # Codex: FS3 — те же 72-байтовые записи FS1, адрес таблицы в последнем слове заголовка.
            # Клод: FS4 — то же для записей FS2 (пакет с префиксами заголовков карт).
            pointer = (at + 3) & ~3
            stream.write(bytes(pointer - at))
            stream.write(table)
            at = pointer + len(table)
            version = 4 if fs2 else 3
            prefix = struct.pack('<4I', MAGIC, version, count, pointer)
        if at > 0xffffffff:
            raise ValueError('DAT превышает 32-битный диапазон')
    with base.open('rb') as stream:
        before = stream.read(len(prefix))
        body_sha = region_digest(stream, len(prefix), size - len(prefix))
        target = hashlib.sha256(prefix)
        stream.seek(len(prefix))
        while data := stream.read(BLOCK):
            target.update(data)
    with (out / 'campaign.append').open('rb') as stream:
        while data := stream.read(BLOCK):
            target.update(data)
    (out / 'prefix.before').write_bytes(before)
    (out / 'prefix.after').write_bytes(prefix)
    report = dict(format=1, base_bytes=size, base_sha256=digest(base),
                  unchanged_sha256=body_sha, target_bytes=at, target_sha256=target.hexdigest(),
                  version=version, catalog_offset=pointer, old_entries=len(entries), entries=count,
                  prefix_bytes=len(prefix), append_bytes=at-size, files=added)
    report['artifacts'] = {n: digest(out / n) for n in ('prefix.before', 'prefix.after', 'campaign.append')}
    (out / 'campaign.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps({k: v for k, v in report.items() if k not in ('files', 'artifacts')}, indent=2))


def load_bundle(bundle):
    report = json.loads((bundle / 'campaign.json').read_text(encoding='utf-8'))
    if report['format'] != 1:
        raise ValueError('Неизвестный формат обновления')
    for name in ('prefix.before', 'prefix.after', 'campaign.append'):
        if digest(bundle / name) != report['artifacts'][name]:
            raise ValueError('Повреждён файл обновления: ' + name)
    before = (bundle / 'prefix.before').read_bytes()
    after = (bundle / 'prefix.after').read_bytes()
    if not len(before) == len(after) == report['prefix_bytes']:
        raise ValueError('Неверный размер заголовка')
    if (bundle / 'campaign.append').stat().st_size != report['target_bytes'] - report['base_bytes']:
        raise ValueError('Неверный размер дополнения')
    return report, before, after


def validate(stream, report, before, after, bundle):
    size = stream.seek(0, 2)
    if size == report['target_bytes'] and region_digest(stream, 0, size) == report['target_sha256']:
        return 'complete'
    if size == report['base_bytes'] and region_digest(stream, 0, size) == report['base_sha256']:
        return 'original'
    if not report['base_bytes'] <= size <= report['target_bytes']:
        raise ValueError('Размер не соответствует исходному или прерванному обновлению')
    # Codex: возобновление разрешено только при точном старом теле и уже записанном начале дополнения.
    if region_digest(stream, len(before), report['base_bytes'] - len(before)) != report['unchanged_sha256']:
        raise ValueError('SHA256 исходных данных не совпадает; запись запрещена')
    stream.seek(0)
    head = stream.read(len(before))
    if any(value not in (old, new) for value, old, new in zip(head, before, after)):
        raise ValueError('Неизвестный заголовок; запись запрещена')
    with (bundle / 'campaign.append').open('rb') as source:
        stream.seek(report['base_bytes'])
        left = size - report['base_bytes']
        while left:
            count = min(left, BLOCK)
            if stream.read(count) != source.read(count):
                raise ValueError('Повреждён хвост незавершённого обновления; запись запрещена')
            left -= count
    return 'interrupted'


def sync(stream):
    stream.flush()
    os.fsync(stream.fileno())


def apply(path, bundle, check=False, rollback=False):
    report, before, after = load_bundle(bundle)
    with path.open('rb' if check else 'r+b') as stream:
        state = validate(stream, report, before, after, bundle)
        print('Состояние:', state)
        if check:
            return
        if rollback:
            if state == 'original':
                return
            stream.seek(0)
            stream.write(before)
            sync(stream)
            stream.truncate(report['base_bytes'])
            sync(stream)
            expected = report['base_sha256']
        else:
            if state == 'complete':
                return
            size = stream.seek(0, 2)
            if shutil.disk_usage(path.parent).free < report['target_bytes'] - size:
                raise ValueError('Недостаточно свободного места')
            # Codex: каталог публикуется только после записи и синхронизации всего хвоста.
            with (bundle / 'campaign.append').open('rb') as source:
                source.seek(size - report['base_bytes'])
                shutil.copyfileobj(source, stream, BLOCK)
            sync(stream)
            stream.seek(0)
            stream.write(after)
            sync(stream)
            expected = report['target_sha256']
        if region_digest(stream, 0, stream.seek(0, 2)) != expected:
            raise ValueError('Итоговый SHA256 не совпал')
    print('Проверено SHA256:', expected)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    subs = parser.add_subparsers(dest='mode', required=True)
    prep = subs.add_parser('prepare')
    prep.add_argument('base', type=Path)
    prep.add_argument('maps', type=Path)
    prep.add_argument('out', type=Path)
    prep.add_argument('--clips-dir', type=Path)
    for name in ('check', 'apply', 'rollback'):
        command = subs.add_parser(name)
        command.add_argument('dat', type=Path)
        command.add_argument('--bundle', type=Path, default=Path(__file__).resolve().parent)
    args = parser.parse_args()
    try:
        if args.mode == 'prepare':
            prepare(args.base, args.maps, args.clips_dir, args.out)
        else:
            apply(args.dat, args.bundle, check=args.mode == 'check', rollback=args.mode == 'rollback')
    except (ValueError, OSError, KeyError, struct.error) as error:
        parser.exit(1, str(error) + '\n')


if __name__ == '__main__':
    main()
