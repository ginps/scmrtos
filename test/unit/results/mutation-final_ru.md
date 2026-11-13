# Итоговый мутационный прогон

Дерево: ветка develop, коммит `0206821` (до трёх тестов из `280b9ba`); перепроверка выживших — на `280b9ba` под ASan и UBSan. Прогон: `mutate.py --fuzz-seeds 30`, 13.11.2026.

## Итог по файлам

| Файл | Убито | Убито при перепроверке | Выжило | Не компилируется | Доля убитых |
| --- | --- | --- | --- | --- | --- |
| `core/os_kernel.cpp` | 40 | 1 | 1 | 5 | 97.6% |
| `core/os_kernel.h` | 54 | 0 | 3 | 9 | 94.7% |
| `core/os_services.cpp` | 63 | 0 | 0 | 2 | 100.0% |
| `core/os_services.h` | 147 | 0 | 13 | 6 | 91.9% |
| `core/usrlib.h` | 61 | 0 | 1 | 0 | 98.4% |
| `ext/event-flags/event_flags.cpp` | 41 | 1 | 2 | 0 | 95.5% |
| `ext/event-flags/event_flags.h` | 10 | 0 | 0 | 0 | 100.0% |
| `ext/launch/launch.h` | 16 | 0 | 2 | 5 | 88.9% |
| `ext/profiler/profiler.h` | 32 | 4 | 2 | 0 | 94.7% |
| `ext/recursive-mutex/recursive_mutex.cpp` | 54 | 0 | 3 | 0 | 94.7% |
| `ext/recursive-mutex/recursive_mutex.h` | 16 | 0 | 5 | 0 | 76.2% |
| `ext/round-robin/round-robin.h` | 41 | 1 | 2 | 0 | 95.5% |
| `ext/safe-run/safe_run.h` | 21 | 0 | 3 | 2 | 87.5% |
| `ext/semaphore/semaphore.cpp` | 29 | 0 | 2 | 0 | 93.5% |
| `ext/semaphore/semaphore.h` | 3 | 0 | 0 | 0 | 100.0% |
| `ext/watchdog/watch_dog_os_ext.h` | 34 | 0 | 6 | 0 | 85.0% |
| **Всего** | **662** | **7** | **45** | **29** | **93.7%** |

Без мутантов, которые в этом порте убить нельзя в принципе (эквивалентные, `*_isr`, чтение одного слова, особенность порта — 28), доля убитых — **97.5%** (669 из 686).

## Выжившие по группам

| Группа | Число | Почему выжили |
| --- | --- | --- |
| Эквивалентные | 16 | изменение не меняет поведения ни при какой реализации: повторное обнуление уже нулевого таймаута, `<` вместо `<=` при выборе минимума, `DUMMY_INSTR()`, счётчик вложенности при свободном мьютексе, условия `safe_run`, которые всё равно отсекает итоговое сравнение множеств, критическая секция внутри обработчика таймера |
| Критические секции в `*_isr` | 9 | обработчики прерываний в порте не вкладываются, поэтому внутри `*_isr` прерывания и так запрещены. На целях с вложенными прерываниями (Cortex‑M) эти секции нужны — убрать их нельзя |
| Критические секции вокруг чтения одного слова | 2 | `is_sleeping()` и `is_suspended()` читают одно слово, на x86‑64 это атомарно. На 8‑битном AVR чтение 16‑битного `timeout_t` неатомарно — секции нужны |
| Особенность порта | 1 | сохранение указателя стека в `context_switch_hook`: в порте «указатель стека» — адрес `ucontext_t`, он не меняется |
| Скрыты известными дефектами | 7 | поведение уже неверно из‑за дефекта, тест помечен как известный дефект; мутант `if(!(pool.get_count()))` в `try_pop_back` и `for(int i = 1; ...)` в конструкторе round‑robin — это исправления N1 и N8. Будут убиты после исправления дефектов |
| Окна гонок без точки подачи прерывания | 5 | убрана критическая секция вокруг «чтение‑изменение‑запись» без вызова планировщика: `TRecursiveMutex::try_lock()`, снимок счётчиков профайлера, `asleep()`/`alive()` watchdog. Внутри окна нет точки, где фреймворк подаёт прерывание; асинхронный стресс попадает в него редко. Нужен пошаговый перебор по инструкциям |
| Защитные остановки | 4 | вечный цикл при неправильном использовании (`launch_precheck`, проверка испорченного таймаута в watchdog); через публичный API недостижимо |
| Выход за массив в выравнивающие байты | 1 | `i <= proc_count` в конструкторе round‑robin пишет за массив `put_off` в выравнивающие байты объекта; ни тесты, ни ASan/UBSan этого не видят |

## Убиты при перепроверке

| Мутант | Чем убит |
| --- | --- |
| `core/os_kernel.cpp:85` [DEL] `;` | channel.items_are_copied_inside_critical_sections_after_blocking |
| `ext/event-flags/event_flags.cpp:73` [CONST] `cur_proc_timeout() = 1;` | event_flags.failed_timed_wait_leaves_no_timeout |
| `ext/profiler/profiler.h:88` [ROR] `for(uint_fast8_t i = 0; i <= OS::PROCESS_COUNT; ++i)` | profiler.shares_in_hundredths_of_percent |
| `ext/profiler/profiler.h:103` [ROR] `for(uint_fast8_t i = 0; i <= OS::PROCESS_COUNT; ++i)` | profiler.shares_in_hundredths_of_percent |
| `ext/profiler/profiler.h:148` [ROR] `for(uint_fast8_t i = 0; i <= OS::PROCESS_COUNT; ++i)` | profiler.generic_float_percent |
| `ext/profiler/profiler.h:161` [ROR] `for(uint_fast8_t i = 0; i <= OS::PROCESS_COUNT; ++i)` | profiler.generic_float_percent |
| `ext/round-robin/round-robin.h:105` [ROR] `for(int i = 0; i <= reg_idx; ++i)` | round_robin.nobody_eligible_keeps_the_turn_where_it_was |

`ext/safe-run/safe_run.h:117` при перепроверке «погиб» из‑за таймаута теста `atomic` под ASan (тест медленнее 1,5 с); мутант эквивалентный и засчитан как выживший. В `mutate.py` лимит времени для сборок с санитайзерами поднят до 6 с.

## Список выживших

### Эквивалентные

- `core/os_kernel.cpp:84` [DEL]: `DUMMY_INSTR();` → `;`
- `core/os_services.h:555` [ROR]: `S qty = free < count ? free : count;` → `S qty = free <= count ? free : count;`
- `core/os_services.h:589` [ROR]: `S count = avail < max_size ? avail : max_size;` → `S count = avail <= max_size ? avail : max_size;`
- `core/usrlib.h:147` [ROR]: `S nItems = cnt <= Count ? cnt : Count;` → `S nItems = cnt < Count ? cnt : Count;`
- `ext/event-flags/event_flags.cpp:73` [DEL]: `cur_proc_timeout() = 0;` → `;`
- `ext/recursive-mutex/recursive_mutex.cpp:100` [CONST]: `NestCount = 0;` → `NestCount = 1;`
- `ext/recursive-mutex/recursive_mutex.cpp:100` [DEL]: `NestCount = 0;` → `;`
- `ext/recursive-mutex/recursive_mutex.h:62` [CONST]: `TRecursiveMutex(): ProcessMap(0), ValueTag(0), NestCount(0) { }` → `TRecursiveMutex(): ProcessMap(0), ValueTag(0), NestCount(1) { }`
- `ext/recursive-mutex/recursive_mutex.h:107` [CONST]: `NestCount = 0;` → `NestCount = 1;`
- `ext/recursive-mutex/recursive_mutex.h:107` [DEL]: `NestCount = 0;` → `;`
- `ext/safe-run/safe_run.h:116` [ROR]: `&& pr < static_cast<int>(PROCESS_COUNT)` → `&& pr <= static_cast<int>(PROCESS_COUNT)`
- `ext/safe-run/safe_run.h:116` [LCR]: `&& pr < static_cast<int>(PROCESS_COUNT)` → `|| pr < static_cast<int>(PROCESS_COUNT)`
- `ext/safe-run/safe_run.h:117` [LCR]: `&& pr != static_cast<int>(prIDLE);` → `|| pr != static_cast<int>(prIDLE);`
- `ext/semaphore/semaphore.cpp:66` [DEL]: `cur_proc_timeout() = 0;` → `;`
- `ext/watchdog/watch_dog_os_ext.h:118` [CS]: `TCritSect cs;` → `;`
- `ext/watchdog/watch_dog_os_ext.h:126` [CS]: `TCritSect cs;` → `;`

### Критические секции в `*_isr`

- `core/os_services.h:363` [CS]: `TCritSect cs;` → `;`
- `core/os_services.h:370` [CS]: `TCritSect cs;` → `;`
- `core/os_services.h:552` [CS]: `TCritSect cs;` → `;`
- `core/os_services.h:586` [CS]: `TCritSect cs;` → `;`
- `core/os_services.h:613` [CS]: `TCritSect cs;` → `;`
- `ext/event-flags/event_flags.cpp:108` [CS]: `TCritSect cs;` → `;`
- `ext/recursive-mutex/recursive_mutex.h:84` [CS]: `TCritSect cs;` → `;`
- `ext/recursive-mutex/recursive_mutex.h:103` [CS]: `TCritSect cs;` → `;`
- `ext/semaphore/semaphore.cpp:99` [CS]: `TCritSect cs;` → `;`

### Критические секции вокруг чтения одного слова

- `core/os_kernel.h:631` [CS]: `TCritSect cs;` → `;`
- `core/os_kernel.h:637` [CS]: `TCritSect cs;` → `;`

### Особенность порта

- `core/os_kernel.h:615` [DEL]: `ProcessTable[CurProcPriority]->StackPointer = sp;` → `;`

### Скрыты известными дефектами

- `core/os_services.h:516` [CS]: `TCritSect cs;` → `;`
- `core/os_services.h:518` [NEG]: `if(pool.get_count())` → `if(!(pool.get_count()))`
- `core/os_services.h:519` [CONST]: `return false;` → `return true;`
- `core/os_services.h:520` [DEL]: `item = pool.pop_back();` → `;`
- `core/os_services.h:521` [DEL]: `resume_all(ProducersProcessMap);` → `;`
- `core/os_services.h:522` [CONST]: `return true;` → `return false;`
- `ext/round-robin/round-robin.h:65` [CONST]: `for(int i = 0; i < proc_count; ++i)` → `for(int i = 1; i < proc_count; ++i)`

### Окна гонок без точки подачи прерывания

- `ext/profiler/profiler.h:87` [CS]: `TCritSect cs;` → `;`
- `ext/profiler/profiler.h:147` [CS]: `TCritSect cs;` → `;`
- `ext/recursive-mutex/recursive_mutex.cpp:110` [CS]: `TCritSect cs;` → `;`
- `ext/watchdog/watch_dog_os_ext.h:80` [CS]: `TCritSect cs;` → `;`
- `ext/watchdog/watch_dog_os_ext.h:95` [CS]: `TCritSect cs;` → `;`

### Защитные остановки

- `ext/launch/launch.h:195` [CONST]: `for (int pr = 0; pr < static_cast<int>(PROCESS_COUNT); ++pr)` → `for (int pr = 1; pr < static_cast<int>(PROCESS_COUNT); ++pr)`
- `ext/launch/launch.h:263` [DEL]: `detail::launch_precheck();` → `;`
- `ext/watchdog/watch_dog_os_ext.h:115` [LCR]: `if (it.timeout == 0 || // may be some process corrupt this variable?` → `if (it.timeout == 0 && // may be some process corrupt this variable?`
- `ext/watchdog/watch_dog_os_ext.h:116` [LCR]: `it.initialTimeout == 0 || // or this variable has been affected` → `it.initialTimeout == 0 && // or this variable has been affected`

### Выход за массив в выравнивающие байты

- `ext/round-robin/round-robin.h:65` [ROR]: `for(int i = 0; i < proc_count; ++i)` → `for(int i = 0; i <= proc_count; ++i)`
