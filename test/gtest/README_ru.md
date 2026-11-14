Тесты ядра scmRTOS на GoogleTest / GoogleMock
=============================================

([English version](README.md))

Комплект проверяет ядро scmRTOS (каталог `core/`): планировщик, `sleep()` и
пробуждения, запуск процессов, системный таймер, прерывания и `TISRW`,
критические секции, перезапуск процессов, сервисы `TEventFlag`, `TMutex`,
`channel`, `message` и кольцевой буфер `usr::ring_buffer`. Ядро работает
по‑настоящему, на тестовом порте Linux (`port/linux/gcc`) в режиме ручных
тиков, поэтому каждый сценарий детерминирован с точностью до тика.

Сборка и запуск
---------------

```sh
cmake -S test/gtest -B build/gtest
cmake --build build/gtest -j
ctest --test-dir build/gtest -j8                 # все конфигурации, ≈10 с
ctest --test-dir build/gtest -L s1_o0_r1         # одна конфигурация
ctest --test-dir build/gtest -L known-defect     # только известные дефекты

build/gtest/scmrtos_gtest_s0_o0_r0                          # одна программа напрямую
build/gtest/scmrtos_gtest_s0_o0_r0 --gtest_filter='Mutex.*'
build/gtest/scmrtos_gtest_s0_o0_r0 --gtest_output=xml:report.xml
```

Откуда берётся GoogleTest (первый подходящий вариант):

| Способ | Параметр |
| --- | --- |
| исходники на диске (сборка без сети) | `-DSCMTEST_GTEST_DIR=/path/to/googletest` |
| установленный пакет с GMock | ничего, `find_package(GTest)` |
| загрузка с GitHub | ничего, `FetchContent`, тег `-DSCMTEST_GTEST_TAG=v1.15.2` |

Другие параметры: `-DSCMRTOS_DIR=<дерево scmRTOS>` — проверить другое дерево
(по умолчанию этот репозиторий), `-DSCMTEST_SANITIZE=ON` — ASan и UBSan,
`-DSCMTEST_COVERAGE=ON` — `--coverage`. Переменные окружения при запуске:
`SCMTEST_TIMEOUT_MS` — предел времени одного сценария (по умолчанию 10000),
`SCMTEST_VERBOSE=1` — печатать вывод сценария и для прошедших тестов.

Требования: Linux, GCC или Clang с C++17, CMake 3.22+.

Конфигурации
------------

Собирается восемь программ `scmrtos_gtest_s<S>_o<O>_r<R>`, по одной на
сочетание:

| Буква | Макрос | Значения |
| --- | --- | --- |
| `s` | `scmRTOS_CONTEXT_SWITCH_SCHEME` | 0 — прямое переключение, 1 — через программное прерывание (как PendSV на Cortex‑M) |
| `o` | `scmRTOS_PRIORITY_ORDER` | 0 — `pr0` = 0, 1 — `pr0` = старший номер |
| `r` | `scmRTOS_PROCESS_RESTART_ENABLE` | 0 или 1; тесты `Restart.*` есть только при 1 |

Каждый тест каждой программы — отдельный тест CTest с префиксом
конфигурации (`s1_o0_r1.Mutex.TryLock`) и меткой конфигурации.

Как устроен тест
----------------

Сценарный тест — обычный `TEST_F` на фикстуре `scmtest::RtosTest`:

```cpp
class Mutex : public scmtest::RtosTest { };

TEST_F(Mutex, WaitersGetTheMutexInPriorityOrder)
{
    static TMutex m;
    {
        InSequence seq;                             // ожидаемый порядок событий
        EXPECT_CALL(probe, Mark("P0"));
        EXPECT_CALL(probe, Mark("P1"));
        EXPECT_CALL(probe, Mark("P2"));
    }
    Process(0, [] { OS::sleep(3); TMutexLocker l(m); Mark("P0"); });
    Process(1, [] { OS::sleep(2); TMutexLocker l(m); Mark("P1"); });
    Process(2, [] { OS::sleep(1); TMutexLocker l(m); Mark("P2"); });
    Process(3, [] { m.lock(); OS::sleep(5); m.unlock(); });
    RunOS();
}
```

- Тело теста — подготовка. `Process(i, body)` задаёт тело процесса `P<i>`
  (5 процессов, `P0` — старший приоритет, `P4` объявлен `pssSuspended`).
  `RunOS()` запускает ядро.
- `RunOS()` делает `fork()`. В дочернем процессе стартует ядро. Любой отказ
  GoogleTest или GoogleMock там — в процессе ОС, в обработчике прерывания, в
  хуке, невыполненное ожидание — сразу передаётся родителю и повторно
  сообщается с исходным файлом и строкой. Поэтому вывод, IDE и
  `--gtest_output=xml` показывают его как обычный отказ. Зависание (предел
  времени), аварийное завершение и сценарий, у которого кончились тики,
  тоже считаются отказом. Заодно каждый сценарий получает свежее ядро:
  `OS::run()` не возвращает управление.
- Сценарий заканчивается, когда все тела процессов вернулись или когда
  вызван `Finish()`. Системный таймер тикает, только когда все процессы
  заблокированы (из процесса idle), или по вызову `Tick()`.
- `ASSERT_*` в дочернем процессе останавливает сценарий.
- **Код после `RunOS()` в теле теста выполняется в родителе и не видит
  результатов сценария.** Итоговые проверки — в `AtEnd()` или в телах процессов.
- Тест без `RunOS()` — обычный тест GoogleTest; ядро не запускается
  (так проверяются `ring_buffer` и карты процессов).

Объекты GoogleMock в фикстуре:

| Объект | Тип | Кто вызывает |
| --- | --- | --- |
| `probe` | `StrictMock<MockProbe>` | тесты: `Mark("...")` (в критической секции); порядок задаётся `InSequence`, неожиданный вызов — отказ |
| `hooks` | `NiceMock<MockKernelHooks>` | ядро: `SystemTimer()` из `system_timer_user_hook()`, `ContextSwitch()` из `context_switch_user_hook()` |
| `irq` | `StrictMock<MockIrq>` | обработчик прерывания `n`, установленный `Irq(n, body)`: `Handler(n)` после тела, внутри `TISRW` |

Действия (`Invoke`) в ожиданиях `hooks` и `irq` выполняются в контексте
прерывания и могут будить процессы, подавать сигналы и т. д. Блокироваться
в них нельзя. Объекты mock, объявленные в самом тесте, передаются в
`VerifyAtEnd(&mock)`: их ожидания проверяются в конце сценария.

Остальное API фикстуры: `Irq(n, body)` / `RawIrq(n, handler)` (без `TISRW`),
`Raise(n)`, `Tick(n)`, `Ticks()`, `Switches()`, `P(i)`, `Prio(i)`, `Done(i)`,
`MaxTicks(n)` (по умолчанию 5000), `Terminate(i, body)` при
`scmRTOS_PROCESS_RESTART_ENABLE == 1`. Режимы процесса: `Mode::MustFinish`
(по умолчанию), `Mode::Daemon` (сценарий его не ждёт), `Mode::Suspended`
(только `P4`: остаётся в `pssSuspended`).

Состав
------

| Файл | Наборы | Что проверяется |
| --- | --- | --- |
| `tests/harness_selftest.cpp` | `Harness` (15) | сама обвязка: отказ в процессе приходит с файлом, строкой и тиком; отказы в обработчике и `AtEnd`; невыполненное, неожиданное и неупорядоченное ожидание; неожиданное прерывание; `ASSERT` останавливает сценарий; зависание, аварийное завершение, исчерпание тиков; `Daemon`, `Finish`, `VerifyAtEnd` |
| `tests/kernel_start_test.cpp` | `KernelStatic` (4), `KernelStart` (9) | `os_running()`, таблица процессов, имена и стеки, значения приоритетов, порядок старта, `pssSuspended` и `start()`, заполнение стека шаблоном |
| `tests/kernel_scheduling_test.cpp` | `Scheduling` (16), `SleepFor` (12) | вытеснение при пробуждении старшего, `sleep(n)`, порядок истечения таймаутов, одновременные таймауты, `wake_up()` / `force_wake_up()`, точное число переключений контекста (GMock `Times(6)`), отсутствие лишних переключений; `sleep(n)` для n от 1 до 65535 с проверкой числа тиков через хук |
| `tests/kernel_timer_isr_test.cpp` | `SystemTimer` (8), `Interrupts` (13), `CriticalSection` (3) | хук таймера: число вызовов, контекст, порядок относительно счётчика тиков, пробуждение из хука; тик в критической секции; `lock_system_timer()`; обработчики прерываний, переключение на выходе из `TISRW`, вложенные `TISRW`, обработчик без `TISRW`; прерывание из критической секции; регрессии потерянного пробуждения (upstream `aa4e99d`); вложенные критические секции и состояние прерываний у процессов |
| `tests/kernel_restart_test.cpp` | `ProcessMap` (3), `Restart` (5) | `get_prio_tag()`, `highest_priority()`, `set/clr_prio_tag()`; `terminate()` с новой функцией и без, сброс таймаута, удаление из карты ожидания флага и мьютекса |
| `tests/services_sync_test.cpp` | `EventFlag` (13+1), `Mutex` (12), `Message` (7) | защёлкивание, пробуждение всех ожидающих по приоритету, таймауты и отсутствие «хвостов» после них, `force_wake_up()`/`wake_up()` во время ожидания, `waiting_for()`, `*_isr`, сигнал из хука таймера; передача мьютекса по приоритету и только одному ожидающему, чужой `unlock()`, `try_lock`, `TMutexLocker`, взаимоисключение под вытеснением |
| `tests/services_channel_test.cpp` | `ChannelData` (6 × 4 типа), `Channel` (16) | типизированные тесты для `int`, `uint8_t`, структуры и индекса `uint16_t`: FIFO, оба конца, «заворачивание», `write`/`read`, блокировки; таймауты и регрессия дефекта 2 (таймаут не теряется, когда элемент забрал другой процесс), `read`/`write` нескольких элементов, `try_*`, `flush`, конкурирующие потребители, `write_isr`/`read_isr`, нагрузочный тест |
| `tests/usrlib_ring_buffer_test.cpp` | `RingBuffer` (8 × 7 типов), `RingBufferModel` (4 × 5 seed‑ов) | размеры 1…300, индексы `uint8_t`/`uint16_t`; сверка со `std::deque` на случайных последовательностях |
| `tests/known_defects_test.cpp` | `KnownDefect` (6), `KnownDefectStatic` (2) | см. ниже |

Итого 245 тестов в программе с `r = 1` (237 обычных и 8 известных дефектов)
и 241 при `r = 0` (вместо `Restart.*` — один пропускаемый тест‑заглушка);
во всей матрице 1944 теста CTest.

Известные дефекты
-----------------

Тесты наборов `KnownDefect*` проверяют правильное поведение там, где его
нарушает известный дефект дерева, поэтому на текущем дереве они не проходят.
CTest регистрирует их с `WILL_FAIL` и меткой `known-defect`: «passed»
означает «дефект на месте», «failed» — «дефект исправлен, перенесите тест в
обычный набор». Посмотреть сами отказы:
`scmrtos_gtest_s0_o0_r0 --gtest_filter='KnownDefect*'`.

| Тест | Дефект |
| --- | --- |
| `N9_OsRunningInAnEarlyGlobalConstructor` | N9: `os_running()` истинно в глобальном конструкторе, выполненном раньше конструктора `OS::Kernel` |
| `N1_TryPopBackOnAnEmptyChannel`, `N1_TryPopBackOnAFilledChannel` | N1: условие в `channel::try_pop_back()` перевёрнуто |
| `D6_TryLockTimeoutIsTotalAcrossHandovers` | 6: таймаут `try_lock(timeout)` начинается заново при каждой передаче мьютекса |
| `N7_ForcedWakeUpLeavesNoStaleMutexWaiter`, `N7_ForcedWakeUpLeavesNoStaleProducer` | N7: `force_wake_up()` оставляет метку процесса в карте ожидания мьютекса/канала |
| `D4_SizeAboveTheIndexRange` | 4: `ring_buffer` с `Size`, который не помещается в тип индекса |
| `D5_IndexAcrossTheWrapWithSize200` | 5: переполнение в `ring_buffer::operator[]` |

Номера — из отчётов по анализу v5.2.0 (1–15) и ветки develop (N1–N9).

Проверка самого комплекта
-------------------------

- `Harness.*` проверяет, что отказы из любого места сценария доходят до
  GoogleTest с правильными файлом и строкой.
- В ядро вносились 22 ошибки вручную (перевёрнутые условия, пропущенные
  пробуждения, сдвиг на тик, `resume_next_ready` будит всех, `resume_all_isr`
  будит одного, откат исправления дефекта 2, `try_lock` не сбрасывает таймаут
  и т. п.). Комплект замечает все 22 хотя бы в одной конфигурации. Одна ошибка
  (`ISR_Exit` планирует при вложенных `TISRW`) в схеме 1 эквивалентна
  исходному коду: там переключение всё равно откладывается до выхода из
  внешнего обработчика.
- Матрица проходит под GCC 13 и Clang 18, а также с `-DSCMTEST_SANITIZE=ON`
  (ASan печатает предупреждение о `__asan_handle_no_return` из‑за
  собственных стеков процессов, это ожидаемо).

Связь с `test/unit`
-------------------

`test/unit` — более глубокий набор на собственном минимальном фреймворке
(перебор точек вытеснения, мутационное тестирование, 16‑битные цели,
расширения `ext/`). Этот комплект покрывает ядро в форме, привычной для
GoogleTest/GoogleMock: стандартные макросы, CTest, XML‑отчёты, интеграция с
IDE и CI. Оба набора используют один порт Linux.
