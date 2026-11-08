"""Codex: те же таблицы и порядок инициализации, временные массивы находятся в PSRAM."""
import re


def masked(source):
    return re.sub(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
                  lambda m: ' ' * len(m[0]), source)


def closing(source, start):
    code = masked(source)
    depth = 0
    for pos in range(start, len(code)):
        if code[pos] == '{':
            depth += 1
        elif code[pos] == '}':
            depth -= 1
            if not depth:
                return pos
    raise AssertionError('Codex: незавершённый инициализатор')


def transform(name, source):
    if name == 'monster_info.cpp':
        pattern = r'        (const [\w:* ]+?) (\w+)\[Monster::MONSTER_COUNT\]\s*=\s*\{'
        found = list(re.finditer(pattern, source))
        assert len(found) == 5
        for match in reversed(found):
            end = closing(source, match.end() - 1)
            assert source[end + 1] == ';'
            kind, variable = match[1], match[2]
            original = source[match.start():end + 2]
            replacement = (f'        const std::unique_ptr<{kind}[]> {variable}(new {kind}[Monster::MONSTER_COUNT]'
                           + source[match.end() - 1:end + 1] + ');')
            source = (source[:match.start()] + '#ifdef HMM2_PACKED\n'
                      + '        // Codex: временная таблица освобождается при выходе, стек задачи не расходуется.\n'
                      + replacement + '\n#else\n' + original + '\n#endif' + source[end + 2:])
        return '#include <memory>\n' + source
    if name == 'artifact_info.cpp':
        match = re.search(r'        artifactData\s*=\s*\{', source)
        assert match
        begin, end = match.end() - 1, closing(source, match.end() - 1)
        code = masked(source)
        entries = []
        pos = begin + 1
        while pos < end:
            if code[pos] == '{':
                last = closing(source, pos)
                entries.append(source[pos:last + 1])
                pos = last
            pos += 1
        assert len(entries) > 90
        original = source[match.start():end + 2]
        replacement = ('        // Codex: строим те же записи по одной, без временного initializer_list на стеке.\n'
                       + f'        artifactData.reserve({len(entries)});\n'
                       + '\n'.join('        artifactData.push_back(fheroes2::ArtifactData' + e + ');' for e in entries))
        return source[:match.start()] + '#ifdef HMM2_PACKED\n' + replacement + '\n#else\n' + original + '\n#endif' + source[end + 2:]
    return source
