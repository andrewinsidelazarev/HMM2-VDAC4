"""Codex: миллисекундные часы порта не нуждаются в промежуточных наносекундах."""


def patch(source):
    source = '#include "hmm_platform.h"\n' + source
    source = source.replace('std::chrono::steady_clock::now()', 'hmm_millis()')
    for field in ('_startTime', '_prevTime'):
        needle = f'std::chrono::time_point<std::chrono::steady_clock> {field};'
        assert source.count(needle) == 1
        source = source.replace(needle, f'uint64_t {field};')
    needle = '            const auto time = std::chrono::duration_cast<std::chrono::milliseconds>( hmm_millis() - _startTime );\n            return time.count();'
    assert source.count(needle) == 1
    source = source.replace(needle, '''            // Codex: исходные часы уже округлены до миллисекунд; сохраняем их точность и одно чтение.
            return hmm_millis() - _startTime;''')
    needle = '            const auto time = std::chrono::duration_cast<std::chrono::milliseconds>( hmm_millis() - _prevTime );\n            const uint64_t passedMs = time.count();'
    assert source.count(needle) == 1
    source = source.replace(needle, '            const uint64_t passedMs = hmm_millis() - _prevTime;')
    source = source.replace('hmm_millis() - std::chrono::milliseconds( 2 * _delayMs )', 'hmm_millis() - 2 * _delayMs')
    needle = '            const std::chrono::duration<double> time = hmm_millis() - _startTime;'
    assert source.count(needle) == 1
    source = source.replace(needle, '''            // Codex: редкий запрос секунд сохраняет исходное преобразование и округление double.
            const auto elapsed = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                std::chrono::milliseconds( static_cast<int64_t>( hmm_millis() - _startTime ) ) );
            const std::chrono::duration<double> time = elapsed;''')
    return source
